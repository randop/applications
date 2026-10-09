//! HTTP/2 request adapter using monoio-http on the mandatory Monoio io_uring runtime.
use std::{collections::HashMap, future::poll_fn};

use bytes::Bytes;
use http::{header, Response, StatusCode};
use monoio::io::{AsyncReadRent, AsyncWriteRent};
use monoio_http::h2::{server, RecvStream};
use tracing::debug;

use crate::{
    http_types::{HttpRequest, HttpResponse},
    quic_server::AppRuntime,
};

const MAX_CONCURRENT_STREAMS: u32 = 128;

pub async fn serve_connection<T>(stream: T, app: AppRuntime, quic_port: u16) -> anyhow::Result<()>
where
    T: AsyncReadRent + AsyncWriteRent + Unpin + 'static,
{
    let mut builder = server::Builder::new();
    builder
        .max_concurrent_streams(MAX_CONCURRENT_STREAMS)
        .max_header_list_size(64 * 1024)
        .max_send_buffer_size(64 * 1024);
    let mut h2 = builder
        .handshake(stream)
        .await
        .map_err(|error| anyhow::anyhow!("HTTP/2 handshake failed: {error}"))?;

    while let Some(result) = h2.accept().await {
        match result {
            Ok((request, responder)) => {
                let app = app.clone();
                monoio::spawn(async move {
                    if let Err(error) = handle_request(request, responder, app, quic_port).await {
                        debug!(%error, "HTTP/2 request stream ended with an error");
                    }
                });
            }
            Err(error) => {
                debug!(%error, "HTTP/2 stream rejected by protocol layer");
            }
        }
    }

    // monoio-http requires the connection to remain driven until it reports closed;
    // this also flushes connection-level frames after the peer stops opening streams.
    if let Err(error) = poll_fn(|cx| h2.poll_closed(cx)).await {
        debug!(%error, "HTTP/2 connection closed with a protocol error");
    }
    Ok(())
}

async fn handle_request(
    request: http::Request<RecvStream>,
    mut responder: server::SendResponse<Bytes>,
    app: AppRuntime,
    quic_port: u16,
) -> anyhow::Result<()> {
    let (parts, mut incoming) = request.into_parts();
    let Some(guard) = app.acquire_request() else {
        send_response(
            &mut responder,
            HttpResponse::error(503, "server request capacity reached"),
            false,
            quic_port,
        )
        .await?;
        return Ok(());
    };

    let authority = parts
        .uri
        .authority()
        .map(|authority| authority.as_str().to_owned())
        .or_else(|| {
            parts
                .headers
                .get(header::HOST)
                .and_then(|value| value.to_str().ok())
                .map(ToOwned::to_owned)
        })
        .unwrap_or_default();
    if authority.is_empty() {
        send_response(
            &mut responder,
            HttpResponse::error(400, "HTTP/2 request requires :authority or Host"),
            false,
            quic_port,
        )
        .await?;
        drop(guard);
        return Ok(());
    }

    let raw_path = parts
        .uri
        .path_and_query()
        .map(|value| value.as_str())
        .unwrap_or("/");
    if !raw_path.starts_with('/')
        || raw_path.starts_with("//")
        || raw_path.contains('#')
        || raw_path.contains('\\')
    {
        send_response(
            &mut responder,
            HttpResponse::error(400, "HTTP/2 :path must be origin-form"),
            false,
            quic_port,
        )
        .await?;
        drop(guard);
        return Ok(());
    }
    let (path, query) = match raw_path.split_once('?') {
        Some((path, query)) => (path.to_owned(), Some(query.to_owned())),
        None => (raw_path.to_owned(), None),
    };

    let mut headers = HashMap::new();
    for (name, value) in parts.headers.iter() {
        let value = value
            .to_str()
            .map(ToOwned::to_owned)
            .unwrap_or_else(|_| String::from_utf8_lossy(value.as_bytes()).into_owned());
        headers.insert(name.as_str().to_ascii_lowercase(), value);
    }

    let body_limit = app.max_request_body_bytes();
    let mut body = Vec::with_capacity(body_limit.min(16 * 1024));
    let mut body_too_large = false;
    while let Some(next) = incoming.data().await {
        let chunk = match next {
            Ok(chunk) => chunk,
            Err(error) => {
                send_response(
                    &mut responder,
                    HttpResponse::error(400, "could not read HTTP/2 request body"),
                    false,
                    quic_port,
                )
                .await?;
                debug!(%error, "HTTP/2 request body read failed");
                drop(guard);
                return Ok(());
            }
        };
        if body.len().saturating_add(chunk.len()) > body_limit {
            body_too_large = true;
            break;
        }
        body.extend_from_slice(&chunk);
        let _ = incoming.flow_control().release_capacity(chunk.len());
    }

    let method = parts.method.as_str().to_owned();
    let mut response = if body_too_large {
        HttpResponse::error(413, "request body exceeds configured limit")
    } else {
        app.dispatch(HttpRequest {
            method: method.clone(),
            authority,
            path,
            query,
            headers,
            body,
            body_too_large: false,
        })
        .await
    };
    if method.eq_ignore_ascii_case("HEAD") {
        response.head_only = true;
    }
    send_response(
        &mut responder,
        response,
        method.eq_ignore_ascii_case("HEAD"),
        quic_port,
    )
    .await?;
    drop(guard);
    Ok(())
}

async fn send_response(
    responder: &mut server::SendResponse<Bytes>,
    mut response: HttpResponse,
    is_head: bool,
    quic_port: u16,
) -> anyhow::Result<()> {
    let no_body_status = matches!(response.status, 204 | 304);
    let content_length = if no_body_status {
        0
    } else {
        response.body.len()
    };
    let safe_content_type = if response.content_type.chars().any(char::is_control) {
        "application/octet-stream"
    } else {
        response.content_type.as_str()
    };
    let status = StatusCode::from_u16(response.status).unwrap_or(StatusCode::INTERNAL_SERVER_ERROR);
    let mut builder = Response::builder()
        .status(status)
        .header(header::SERVER, "websrv")
        .header(header::CONTENT_TYPE, safe_content_type)
        .header(header::CONTENT_LENGTH, content_length.to_string())
        .header("x-content-type-options", "nosniff")
        .header("alt-svc", format!("h3=\":{quic_port}\"; ma=86400"));
    if let Some(cache_control) = &response.cache_control {
        if !cache_control
            .chars()
            .any(|ch| ch.is_control() && ch != '\t')
        {
            builder = builder.header(header::CACHE_CONTROL, cache_control.as_str());
        }
    }
    // HTTP/2 forbids connection-specific fields; only end the stream, never the connection.
    let head_only = is_head || response.head_only;
    if no_body_status {
        response.body.clear();
    }
    let end_stream = head_only || no_body_status || response.body.is_empty();
    let head = builder
        .body(())
        .map_err(|error| anyhow::anyhow!("invalid HTTP/2 response headers: {error}"))?;
    let mut outgoing = responder
        .send_response(head, end_stream)
        .map_err(|error| anyhow::anyhow!("could not send HTTP/2 response headers: {error}"))?;
    if !end_stream {
        // Respect both the per-stream and connection-level HTTP/2 send windows. Never
        // hand an entire potentially large static file to monoio-http's unbounded queue.
        let body = Bytes::from(std::mem::take(&mut response.body));
        let body_len = body.len();
        let mut offset = 0usize;

        while offset < body_len {
            let desired = (body_len - offset).min(16 * 1024);
            outgoing.reserve_capacity(desired);
            let assigned = if outgoing.capacity() > 0 {
                outgoing.capacity()
            } else {
                poll_fn(|cx| outgoing.poll_capacity(cx))
                    .await
                    .ok_or_else(|| {
                        anyhow::anyhow!("HTTP/2 stream closed while waiting for send capacity")
                    })?
                    .map_err(|error| anyhow::anyhow!("HTTP/2 send flow control failed: {error}"))?
            };
            anyhow::ensure!(assigned > 0, "HTTP/2 send flow control made no progress");
            let chunk_len = assigned.min(desired).min(body_len - offset);
            let end_of_stream = offset + chunk_len == body_len;
            let chunk = body.slice(offset..offset + chunk_len);
            outgoing
                .send_data(chunk, end_of_stream)
                .map_err(|error| anyhow::anyhow!("could not send HTTP/2 response body: {error}"))?;
            offset += chunk_len;
        }
    }
    Ok(())
}
