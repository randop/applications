//! Optional HTTPS/1.1 protocol fallback. The listener and stream I/O use Monoio's
//! mandatory IoUringDriver; rustls only implements TLS record/handshake logic.
use std::{
    cell::Cell,
    collections::HashMap,
    fs::File,
    io::{self, BufReader, Cursor, Read, Write},
    net::SocketAddr,
    path::Path,
    rc::Rc,
    sync::Arc,
};

use anyhow::{Context, Result};
use monoio::{
    io::{AsyncReadRent, AsyncWriteRentExt},
    net::{TcpListener, TcpStream},
};
use rustls::{ServerConfig, ServerConnection};
use tracing::{debug, info, warn};

use crate::{
    http_types::{HttpRequest, HttpResponse},
    quic_server::{AppRuntime, RequestGuard},
};

const MAX_TCP_CONNECTIONS: usize = 128;
const MAX_HEADER_BYTES: usize = 64 * 1024;
const SOCKET_READ_BYTES: usize = 16 * 1024;
const TLS_PLAINTEXT_CHUNK: usize = 16 * 1024;

pub fn make_tls_config(cert_path: &Path, key_path: &Path) -> Result<Arc<ServerConfig>> {
    let mut cert_reader = BufReader::new(
        File::open(cert_path)
            .with_context(|| format!("cannot open TLS certificate {}", cert_path.display()))?,
    );
    let certs = rustls_pemfile::certs(&mut cert_reader)
        .collect::<std::result::Result<Vec<_>, _>>()
        .with_context(|| format!("cannot parse TLS certificate chain {}", cert_path.display()))?;
    anyhow::ensure!(
        !certs.is_empty(),
        "TLS certificate chain {} is empty",
        cert_path.display()
    );

    let mut key_reader = BufReader::new(
        File::open(key_path)
            .with_context(|| format!("cannot open TLS private key {}", key_path.display()))?,
    );
    let key = rustls_pemfile::private_key(&mut key_reader)
        .with_context(|| format!("cannot parse TLS private key {}", key_path.display()))?
        .context("TLS key PEM does not contain a supported private key")?;

    let provider = Arc::new(rustls::crypto::ring::default_provider());
    let mut tls = ServerConfig::builder_with_provider(provider)
        .with_safe_default_protocol_versions()
        .context("no safe TLS protocol versions are available")?
        .with_no_client_auth()
        .with_single_cert(certs, key)
        .context("TLS certificate and private key do not match or are unsupported")?;
    tls.alpn_protocols = vec![b"http/1.1".to_vec()];
    Ok(Arc::new(tls))
}

pub async fn serve(
    listener: TcpListener,
    tls_config: Arc<ServerConfig>,
    app: AppRuntime,
    quic_port: u16,
) {
    let active_connections = Rc::new(Cell::new(0usize));
    info!(
        protocol = "HTTPS/1.1",
        runtime = "monoio/IoUringDriver",
        "io_uring TCP protocol fallback listening"
    );

    loop {
        match listener.accept().await {
            Ok((stream, peer)) => {
                if active_connections.get() >= MAX_TCP_CONNECTIONS {
                    debug!(%peer, limit = MAX_TCP_CONNECTIONS, "TCP connection limit reached");
                    drop(stream);
                    continue;
                }
                active_connections.set(active_connections.get() + 1);
                let guard = ConnectionGuard(active_connections.clone());
                let app = app.clone();
                let tls_config = tls_config.clone();
                monoio::spawn(async move {
                    let _connection_guard = guard;
                    if let Err(error) =
                        serve_connection(stream, peer, tls_config, app, quic_port).await
                    {
                        debug!(%peer, %error, "HTTPS/1.1 connection ended");
                    }
                });
            }
            Err(error) => {
                warn!(%error, "io_uring TCP accept failed");
                monoio::time::sleep(std::time::Duration::from_millis(25)).await;
            }
        }
    }
}

struct ConnectionGuard(Rc<Cell<usize>>);
impl Drop for ConnectionGuard {
    fn drop(&mut self) {
        self.0.set(self.0.get().saturating_sub(1));
    }
}

struct RequestHead {
    method: String,
    authority: String,
    path: String,
    query: Option<String>,
    headers: HashMap<String, String>,
    content_length: usize,
    header_len: usize,
    close_after: bool,
}

enum HeadParse {
    Incomplete,
    Complete(RequestHead),
    Reject(u16, &'static str),
}

async fn serve_connection(
    mut stream: TcpStream,
    peer: SocketAddr,
    tls_config: Arc<ServerConfig>,
    app: AppRuntime,
    quic_port: u16,
) -> io::Result<()> {
    let connection = ServerConnection::new(tls_config)
        .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?;
    let mut tls = connection;
    let mut socket_buffer = vec![0u8; SOCKET_READ_BYTES];
    let mut plaintext = Vec::<u8>::new();
    let mut pending: Option<(RequestHead, RequestGuard)> = None;

    loop {
        let peer_closed = drain_tls_plaintext(&mut tls, &mut plaintext)?;

        loop {
            if pending.is_none() {
                match parse_request_head(&plaintext, app.max_request_body_bytes()) {
                    HeadParse::Incomplete => break,
                    HeadParse::Reject(status, message) => {
                        let mut response = HttpResponse::error(status, message);
                        response.head_only = false;
                        write_http1_response(&mut stream, &mut tls, &response, true, quic_port)
                            .await?;
                        finish_tls_close(&mut stream, &mut tls).await?;
                        return Ok(());
                    }
                    HeadParse::Complete(head) => {
                        plaintext.drain(..head.header_len);
                        let Some(guard) = app.acquire_request() else {
                            let response =
                                HttpResponse::error(503, "server request capacity reached");
                            write_http1_response(&mut stream, &mut tls, &response, true, quic_port)
                                .await?;
                            finish_tls_close(&mut stream, &mut tls).await?;
                            return Ok(());
                        };
                        pending = Some((head, guard));
                    }
                }
            }

            let Some((head, _guard)) = pending.as_ref() else {
                continue;
            };
            if plaintext.len() < head.content_length {
                break;
            }

            let (head, guard) = pending.take().expect("pending request was checked above");
            let body = plaintext.drain(..head.content_length).collect::<Vec<_>>();
            let request = HttpRequest {
                method: head.method.clone(),
                authority: head.authority,
                path: head.path,
                query: head.query,
                headers: head.headers,
                body,
                body_too_large: false,
            };
            let is_head = request.method.eq_ignore_ascii_case("HEAD");
            let close_after = head.close_after;
            let mut response = app.dispatch(request).await;
            if is_head {
                response.head_only = true;
            }
            write_http1_response(&mut stream, &mut tls, &response, close_after, quic_port).await?;
            drop(guard);
            if close_after {
                finish_tls_close(&mut stream, &mut tls).await?;
                return Ok(());
            }
        }

        if tls.wants_write() {
            flush_tls(&mut stream, &mut tls).await?;
        }
        // rustls 0.23 reports a received close_notify as Ok(0) from its
        // plaintext reader once buffered plaintext has been drained.
        if peer_closed {
            return Ok(());
        }

        let (result, returned_buffer) = stream.read(socket_buffer).await;
        socket_buffer = returned_buffer;
        let amount = match result {
            Ok(0) => return Ok(()),
            Ok(amount) => amount,
            Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
            Err(error) => return Err(error),
        };
        let mut cursor = Cursor::new(&socket_buffer[..amount]);
        tls.read_tls(&mut cursor)?;
        tls.process_new_packets()
            .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?;
        if tls.wants_write() {
            flush_tls(&mut stream, &mut tls).await?;
        }
    }
}

fn drain_tls_plaintext(tls: &mut ServerConnection, output: &mut Vec<u8>) -> io::Result<bool> {
    let mut buffer = vec![0u8; TLS_PLAINTEXT_CHUNK];
    loop {
        match tls.reader().read(&mut buffer) {
            // In rustls 0.23, a non-empty read returning zero means the peer sent
            // close_notify and all buffered plaintext has now been consumed.
            Ok(0) => return Ok(true),
            Ok(amount) => output.extend_from_slice(&buffer[..amount]),
            Err(error) if error.kind() == io::ErrorKind::WouldBlock => return Ok(false),
            Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
            Err(error) => return Err(error),
        }
    }
}

fn parse_request_head(bytes: &[u8], max_body_bytes: usize) -> HeadParse {
    let Some(separator) = bytes.windows(4).position(|window| window == b"\r\n\r\n") else {
        return if bytes.len() > MAX_HEADER_BYTES {
            HeadParse::Reject(431, "request headers too large")
        } else {
            HeadParse::Incomplete
        };
    };
    let header_len = separator + 4;
    if header_len > MAX_HEADER_BYTES {
        return HeadParse::Reject(431, "request headers too large");
    }
    let text = match std::str::from_utf8(&bytes[..separator]) {
        Ok(text) => text,
        Err(_) => return HeadParse::Reject(400, "request headers must be valid ASCII/UTF-8"),
    };
    let mut lines = text.split("\r\n");
    let Some(request_line) = lines.next() else {
        return HeadParse::Reject(400, "missing request line");
    };
    let pieces = request_line.split(' ').collect::<Vec<_>>();
    if pieces.len() != 3 || pieces.iter().any(|piece| piece.is_empty()) {
        return HeadParse::Reject(400, "malformed request line");
    }
    let method = pieces[0];
    if !is_http_token(method) {
        return HeadParse::Reject(400, "invalid HTTP method");
    }
    if pieces[2] != "HTTP/1.1" {
        return HeadParse::Reject(505, "only HTTP/1.1 is supported on the TCP listener");
    }
    let target = pieces[1];
    if !target.starts_with('/')
        || target.starts_with("//")
        || target.contains('\\')
        || target.contains('#')
        || target.chars().any(char::is_control)
    {
        return HeadParse::Reject(400, "request target must be origin-form");
    }
    let (path, query) = match target.split_once('?') {
        Some((path, query)) => (path, Some(query.to_owned())),
        None => (target, None),
    };
    if path.is_empty() {
        return HeadParse::Reject(400, "empty request path");
    }

    let mut headers = HashMap::new();
    for line in lines {
        let Some((name, value)) = line.split_once(':') else {
            return HeadParse::Reject(400, "malformed request header");
        };
        if !is_http_token(name) {
            return HeadParse::Reject(400, "invalid HTTP header name");
        }
        let value = value.trim_matches(|ch| ch == ' ' || ch == '\t');
        if value
            .bytes()
            .any(|byte| (byte < 0x20 && byte != b'\t') || byte == 0x7f)
        {
            return HeadParse::Reject(400, "invalid HTTP header value");
        }
        let name = name.to_ascii_lowercase();
        if headers.insert(name, value.to_owned()).is_some() {
            return HeadParse::Reject(400, "duplicate request header");
        }
    }
    let Some(authority) = headers.get("host").map(|value| value.trim().to_owned()) else {
        return HeadParse::Reject(400, "HTTP/1.1 requires a Host header");
    };
    if authority.is_empty()
        || authority
            .bytes()
            .any(|byte| byte.is_ascii_whitespace() || byte == b'/' || byte == b'\\')
    {
        return HeadParse::Reject(400, "invalid Host header");
    }
    if headers.contains_key("expect") {
        return HeadParse::Reject(417, "Expect is not supported");
    }
    if headers.contains_key("transfer-encoding") {
        return HeadParse::Reject(
            501,
            "Transfer-Encoding is not supported; use Content-Length",
        );
    }
    let content_length = match headers.get("content-length") {
        Some(value) => match value.parse::<usize>() {
            Ok(length) => length,
            Err(_) => return HeadParse::Reject(400, "invalid Content-Length"),
        },
        None => 0,
    };
    if content_length > max_body_bytes {
        return HeadParse::Reject(413, "request body exceeds configured limit");
    }
    let close_after = headers.get("connection").is_some_and(|value| {
        value
            .split(',')
            .any(|token| token.trim().eq_ignore_ascii_case("close"))
    });

    HeadParse::Complete(RequestHead {
        method: method.to_owned(),
        authority,
        path: path.to_owned(),
        query,
        headers,
        content_length,
        header_len,
        close_after,
    })
}

fn is_http_token(value: &str) -> bool {
    !value.is_empty()
        && value.bytes().all(|byte| {
            byte.is_ascii_alphanumeric()
                || matches!(
                    byte,
                    b'!' | b'#'
                        | b'$'
                        | b'%'
                        | b'&'
                        | b'\''
                        | b'*'
                        | b'+'
                        | b'-'
                        | b'.'
                        | b'^'
                        | b'_'
                        | b'`'
                        | b'|'
                        | b'~'
                )
        })
}

async fn write_http1_response(
    stream: &mut TcpStream,
    tls: &mut ServerConnection,
    response: &HttpResponse,
    close_after: bool,
    quic_port: u16,
) -> io::Result<()> {
    let reason = reason_phrase(response.status);
    let content_length = if matches!(response.status, 204 | 304) {
        0
    } else {
        response.body.len()
    };
    let connection = if close_after { "close" } else { "keep-alive" };
    let safe_content_type = if response.content_type.chars().any(char::is_control) {
        "application/octet-stream"
    } else {
        response.content_type.as_str()
    };
    let mut headers = format!(
        "HTTP/1.1 {} {}\r\nserver: websrv\r\ncontent-type: {}\r\ncontent-length: {}\r\nconnection: {}\r\nx-content-type-options: nosniff\r\nalt-svc: h3=\":{}\"; ma=86400\r\n",
        response.status, reason, safe_content_type, content_length, connection, quic_port,
    );
    if let Some(cache_control) = &response.cache_control {
        if !cache_control
            .chars()
            .any(|ch| ch.is_control() && ch != '\t')
        {
            headers.push_str(&format!("cache-control: {cache_control}\r\n"));
        }
    }
    headers.push_str("\r\n");
    write_tls_plaintext(stream, tls, headers.as_bytes()).await?;
    if !response.head_only && !matches!(response.status, 204 | 304) {
        write_tls_plaintext(stream, tls, &response.body).await?;
    }
    Ok(())
}

async fn finish_tls_close(stream: &mut TcpStream, tls: &mut ServerConnection) -> io::Result<()> {
    tls.send_close_notify();
    flush_tls(stream, tls).await
}

async fn write_tls_plaintext(
    stream: &mut TcpStream,
    tls: &mut ServerConnection,
    mut bytes: &[u8],
) -> io::Result<()> {
    while !bytes.is_empty() {
        let amount = bytes.len().min(TLS_PLAINTEXT_CHUNK);
        {
            let mut writer = tls.writer();
            writer.write_all(&bytes[..amount])?;
        }
        flush_tls(stream, tls).await?;
        bytes = &bytes[amount..];
    }
    Ok(())
}

async fn flush_tls(stream: &mut TcpStream, tls: &mut ServerConnection) -> io::Result<()> {
    while tls.wants_write() {
        let mut record = Vec::with_capacity(TLS_PLAINTEXT_CHUNK + 2048);
        let written = tls.write_tls(&mut record)?;
        if written == 0 {
            break;
        }
        let (result, _returned_record) = stream.write_all(record).await;
        result?;
    }
    Ok(())
}

fn reason_phrase(status: u16) -> &'static str {
    match status {
        200 => "OK",
        201 => "Created",
        202 => "Accepted",
        204 => "No Content",
        206 => "Partial Content",
        301 => "Moved Permanently",
        302 => "Found",
        304 => "Not Modified",
        400 => "Bad Request",
        401 => "Unauthorized",
        403 => "Forbidden",
        404 => "Not Found",
        405 => "Method Not Allowed",
        408 => "Request Timeout",
        413 => "Content Too Large",
        414 => "URI Too Long",
        417 => "Expectation Failed",
        421 => "Misdirected Request",
        429 => "Too Many Requests",
        431 => "Request Header Fields Too Large",
        500 => "Internal Server Error",
        501 => "Not Implemented",
        503 => "Service Unavailable",
        505 => "HTTP Version Not Supported",
        _ => "Response",
    }
}

#[cfg(test)]
mod tests {
    use super::{parse_request_head, HeadParse};

    #[test]
    fn parses_content_length_and_query() {
        let raw = b"POST /echo?format=json HTTP/1.1\r\nHost: api.example.test\r\nContent-Length: 3\r\n\r\nabc";
        let HeadParse::Complete(head) = parse_request_head(raw, 1024) else {
            panic!("request should parse")
        };
        assert_eq!(head.method, "POST");
        assert_eq!(head.authority, "api.example.test");
        assert_eq!(head.path, "/echo");
        assert_eq!(head.query.as_deref(), Some("format=json"));
        assert_eq!(head.content_length, 3);
        assert_eq!(&raw[head.header_len..], b"abc");
    }

    #[test]
    fn rejects_chunked_bodies_and_oversize_content_length() {
        let chunked =
            b"POST / HTTP/1.1\r\nHost: example.test\r\nTransfer-Encoding: chunked\r\n\r\n";
        assert!(matches!(
            parse_request_head(chunked, 1024),
            HeadParse::Reject(501, _)
        ));
        let too_large = b"POST / HTTP/1.1\r\nHost: example.test\r\nContent-Length: 20\r\n\r\n";
        assert!(matches!(
            parse_request_head(too_large, 10),
            HeadParse::Reject(413, _)
        ));
    }
}
