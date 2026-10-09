//! HTTP/3 over QUIC on a Monoio runtime pinned to its io_uring driver.
//! No Tokio executor, epoll driver, or non-io_uring transport is linked by this crate.

use std::{
    cell::Cell,
    collections::{HashMap, VecDeque},
    net::SocketAddr,
    rc::Rc,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    time::Duration,
};

use anyhow::{Context, Result};
use futures::{channel::mpsc, SinkExt, StreamExt};
use monoio::net::udp::UdpSocket;
use quiche::h3::NameValue;
use ring::rand::SecureRandom;
use tracing::{debug, error, info, warn};

use crate::{
    config::Config,
    controllers::ControllerRuntime,
    http_types::{HttpRequest, HttpResponse},
    static_site,
};

const UDP_READ_BUFFER: usize = 65_535;
const QUIC_PACKET_BUFFER: usize = 1350;
const TIMER_TICK: Duration = Duration::from_millis(5);
const RESPONSE_CHUNK: usize = 16 * 1024;
const MAX_ACTIVE_REQUESTS: usize = 16;
const MAX_CONNECTIONS: usize = 512;

#[derive(Clone)]
pub(crate) struct AppRuntime {
    config: Rc<Config>,
    controllers: ControllerRuntime,
    active_requests: Rc<Cell<usize>>,
}

pub(crate) struct RequestGuard {
    active_requests: Rc<Cell<usize>>,
}

impl Drop for RequestGuard {
    fn drop(&mut self) {
        self.active_requests
            .set(self.active_requests.get().saturating_sub(1));
    }
}

impl AppRuntime {
    pub(crate) fn acquire_request(&self) -> Option<RequestGuard> {
        let current = self.active_requests.get();
        if current >= MAX_ACTIVE_REQUESTS {
            return None;
        }
        self.active_requests.set(current + 1);
        Some(RequestGuard {
            active_requests: self.active_requests.clone(),
        })
    }

    pub(crate) async fn dispatch(&self, request: HttpRequest) -> HttpResponse {
        if request.body_too_large || request.body.len() > self.config.max_request_body_bytes {
            return HttpResponse::error(413, "request body exceeds configured limit");
        }
        let Some(vhost) = self.config.resolve_host(&request.authority) else {
            return HttpResponse::error(421, "no virtual host is configured for this authority");
        };

        match (&vhost.static_site, &vhost.controller) {
            (Some(site), None) => {
                static_site::serve(site, &request, self.config.max_static_file_bytes).await
            }
            (None, Some(controller)) => self.controllers.dispatch(controller, request).await,
            _ => HttpResponse::error(500, "invalid virtual-host configuration"),
        }
    }

    pub(crate) fn max_request_body_bytes(&self) -> usize {
        self.config.max_request_body_bytes
    }
}

struct PendingRequest {
    method: String,
    authority: String,
    path: String,
    query: Option<String>,
    headers: HashMap<String, String>,
    body: Vec<u8>,
    body_too_large: bool,
    over_capacity: bool,
    guard: Option<RequestGuard>,
}

impl PendingRequest {
    fn into_parts(self) -> (HttpRequest, Option<RequestGuard>, bool) {
        let request = HttpRequest {
            method: self.method,
            authority: self.authority,
            path: self.path,
            query: self.query,
            headers: self.headers,
            body: self.body,
            body_too_large: self.body_too_large,
        };
        (request, self.guard, self.over_capacity)
    }
}

struct OutgoingResponse {
    stream_id: u64,
    status: u16,
    content_type: String,
    cache_control: Option<String>,
    body: Vec<u8>,
    offset: usize,
    head_only: bool,
    headers_sent: bool,
    _guard: Option<RequestGuard>,
}

struct Session {
    conn: quiche::Connection,
    h3: Option<quiche::h3::Connection>,
    pending_requests: HashMap<u64, PendingRequest>,
    outgoing: VecDeque<OutgoingResponse>,
}

impl Session {
    fn new(conn: quiche::Connection) -> Self {
        Self {
            conn,
            h3: None,
            pending_requests: HashMap::new(),
            outgoing: VecDeque::new(),
        }
    }
}

struct CompletedRequest {
    stream_id: u64,
    request: HttpRequest,
    guard: Option<RequestGuard>,
    over_capacity: bool,
}

enum ServerEvent {
    Datagram {
        bytes: Vec<u8>,
        peer: SocketAddr,
    },
    Response {
        connection_id: Vec<u8>,
        stream_id: u64,
        response: HttpResponse,
        guard: Option<RequestGuard>,
    },
    Tick,
    Shutdown,
}

pub fn make_quic_config(config: &Config) -> Result<quiche::Config> {
    let mut quic = quiche::Config::new(quiche::PROTOCOL_VERSION)
        .context("could not create QUIC configuration")?;
    quic.set_application_protos(&[b"h3"])
        .context("could not configure HTTP/3 ALPN")?;
    let cert_path = config
        .cert_path
        .to_str()
        .context("tls_cert path is not valid UTF-8")?;
    let key_path = config
        .key_path
        .to_str()
        .context("tls_key path is not valid UTF-8")?;
    quic.load_cert_chain_from_pem_file(cert_path)
        .with_context(|| {
            format!(
                "could not load certificate chain {}",
                config.cert_path.display()
            )
        })?;
    quic.load_priv_key_from_pem_file(key_path)
        .with_context(|| format!("could not load private key {}", config.key_path.display()))?;
    quic.set_max_idle_timeout(30_000);
    quic.set_max_recv_udp_payload_size(QUIC_PACKET_BUFFER);
    quic.set_max_send_udp_payload_size(QUIC_PACKET_BUFFER);
    quic.set_initial_max_data(4 * 1024 * 1024);
    quic.set_initial_max_stream_data_bidi_local(1024 * 1024);
    quic.set_initial_max_stream_data_bidi_remote(1024 * 1024);
    quic.set_initial_max_stream_data_uni(1024 * 1024);
    quic.set_initial_max_streams_bidi(128);
    quic.set_initial_max_streams_uni(16);
    quic.set_disable_active_migration(true);
    Ok(quic)
}

pub async fn serve(
    config: Config,
    mut quic_config: quiche::Config,
    shutdown: Arc<AtomicBool>,
) -> Result<()> {
    let socket =
        Rc::new(UdpSocket::bind(config.listen).with_context(|| {
            format!("could not bind io_uring UDP listener at {}", config.listen)
        })?);
    let local_addr = socket
        .local_addr()
        .context("could not inspect QUIC socket address")?;
    let h3_config = quiche::h3::Config::new().context("could not create HTTP/3 configuration")?;
    let controllers = ControllerRuntime::new(&config.controllers, config.max_request_body_bytes)?;
    let http1_secure_enabled = config.http1_secure_enabled;
    let http1_plain_enabled = config.http1_plain_enabled;
    let http2_secure_enabled = config.http2_secure_enabled;
    let http2_plain_enabled = config.http2_plain_enabled;
    let secure_http_enabled = http1_secure_enabled || http2_secure_enabled;
    let plain_http_enabled = http1_plain_enabled || http2_plain_enabled;
    let secure_listen = config.listen;
    let plain_listen = config.plain_listen;
    let app = AppRuntime {
        config: Rc::new(config),
        controllers,
        active_requests: Rc::new(Cell::new(0)),
    };

    if secure_http_enabled {
        let tls_config = crate::http1_server::make_tls_config(
            &app.config.cert_path,
            &app.config.key_path,
            http1_secure_enabled,
            http2_secure_enabled,
        )?;
        let listener = monoio::net::TcpListener::bind(secure_listen).with_context(|| {
            format!("could not bind io_uring secure HTTP TCP listener at {secure_listen}")
        })?;
        info!(
            listen = %secure_listen,
            http1_secure_enabled,
            http2_secure_enabled,
            "secure HTTP listener started on Monoio io_uring with TLS ALPN"
        );
        monoio::spawn(crate::http1_server::serve(
            listener,
            tls_config,
            app.clone(),
            secure_listen.port(),
            http1_secure_enabled,
        ));
    }

    if plain_http_enabled {
        let listener = monoio::net::TcpListener::bind(plain_listen).with_context(|| {
            format!("could not bind io_uring plain HTTP TCP listener at {plain_listen}")
        })?;
        info!(
            listen = %plain_listen,
            http1_plain_enabled,
            http2_plain_enabled,
            "plain HTTP listener started on Monoio io_uring"
        );
        monoio::spawn(crate::http1_server::serve_plain(
            listener,
            app.clone(),
            secure_listen.port(),
            http1_plain_enabled,
            http2_plain_enabled,
        ));
    }

    // A bounded channel provides backpressure between the io_uring receive loop,
    // the QUIC/HTTP3 state machine, and independently scheduled Rust request handlers.
    let (event_tx, mut event_rx) = mpsc::channel::<ServerEvent>(1024);
    let request_event_tx = event_tx.clone();

    let recv_socket = socket.clone();
    let mut recv_tx = event_tx.clone();
    monoio::spawn(async move {
        let mut buffer = vec![0u8; UDP_READ_BUFFER];
        loop {
            let (result, returned) = recv_socket.recv_from(buffer).await;
            buffer = returned;
            match result {
                Ok((read, peer)) => {
                    let event = ServerEvent::Datagram {
                        bytes: buffer[..read].to_vec(),
                        peer,
                    };
                    if recv_tx.send(event).await.is_err() {
                        break;
                    }
                }
                Err(error) => {
                    warn!(%error, "io_uring UDP receive failed");
                    monoio::time::sleep(Duration::from_millis(2)).await;
                }
            }
        }
    });

    let mut timer_tx = event_tx.clone();
    let timer_shutdown = shutdown.clone();
    monoio::spawn(async move {
        loop {
            monoio::time::sleep(TIMER_TICK).await;
            if timer_shutdown.load(Ordering::Relaxed) {
                let _ = timer_tx.send(ServerEvent::Shutdown).await;
                break;
            }
            if timer_tx.send(ServerEvent::Tick).await.is_err() {
                break;
            }
        }
    });
    drop(event_tx);

    let mut sessions: HashMap<Vec<u8>, Session> = HashMap::new();
    // Initial packets use a client-chosen original destination CID until the server's
    // source CID is observed. Keep that original CID as an alias for Initial retries.
    let mut cid_aliases: HashMap<Vec<u8>, Vec<u8>> = HashMap::new();
    info!(
        listen = %local_addr,
        runtime = "monoio/IoUringDriver",
        protocol = "HTTP/3",
        http1_secure_enabled,
        http1_plain_enabled,
        http2_secure_enabled,
        http2_plain_enabled,
        secure_listen = %secure_listen,
        plain_listen = %plain_listen,
        "websrv started"
    );

    while let Some(event) = event_rx.next().await {
        match event {
            ServerEvent::Datagram { bytes, peer } => {
                handle_datagram(
                    bytes,
                    peer,
                    local_addr,
                    &mut quic_config,
                    &h3_config,
                    &mut sessions,
                    &mut cid_aliases,
                    &app,
                    request_event_tx.clone(),
                );
                pump_all(&socket, &mut sessions).await;
            }
            ServerEvent::Response {
                connection_id,
                stream_id,
                response,
                guard,
            } => {
                if let Some(session) = sessions.get_mut(&connection_id) {
                    queue_response(session, stream_id, response, guard);
                    pump_outgoing(session);
                }
                // If the connection closed while the Rust handler was running, its guard is
                // dropped here and the response is discarded without touching another client.
                pump_all(&socket, &mut sessions).await;
            }
            ServerEvent::Tick => {
                tick_sessions(&mut sessions, &h3_config);
                pump_all(&socket, &mut sessions).await;
                sessions.retain(|_, session| !session.conn.is_closed());
                cid_aliases.retain(|_, session_id| sessions.contains_key(session_id));
            }
            ServerEvent::Shutdown => {
                info!(
                    connections = sessions.len(),
                    "shutdown requested; closing QUIC connections"
                );
                for session in sessions.values_mut() {
                    session.conn.close(true, 0x100, b"server shutdown").ok();
                }
                pump_all(&socket, &mut sessions).await;
                break;
            }
        }
    }

    Ok(())
}

#[allow(clippy::too_many_arguments)] // This is the single-threaded QUIC state-machine boundary.
fn handle_datagram(
    mut bytes: Vec<u8>,
    peer: SocketAddr,
    local_addr: SocketAddr,
    quic_config: &mut quiche::Config,
    h3_config: &quiche::h3::Config,
    sessions: &mut HashMap<Vec<u8>, Session>,
    cid_aliases: &mut HashMap<Vec<u8>, Vec<u8>>,
    app: &AppRuntime,
    event_tx: mpsc::Sender<ServerEvent>,
) {
    // QUIC routing is by destination connection ID, not just the UDP peer tuple: a
    // browser can multiplex several QUIC connections through one UDP socket. This build
    // explicitly disables active migration, but CID routing avoids conflating connections.
    let (packet_type, packet_dcid) =
        match quiche::Header::from_slice(&mut bytes, quiche::MAX_CONN_ID_LEN) {
            Ok(header) => (header.ty, header.dcid.as_ref().to_vec()),
            Err(_) => return,
        };

    let connection_id = if let Some((key, _)) = sessions.get_key_value(&packet_dcid) {
        key.clone()
    } else if let Some(key) = cid_aliases.get(&packet_dcid) {
        key.clone()
    } else {
        if packet_type != quiche::Type::Initial {
            return;
        }
        if sessions.len() >= MAX_CONNECTIONS {
            debug!(%peer, connections = sessions.len(), "connection limit reached; ignoring new QUIC peer");
            return;
        }
        let mut server_cid = [0u8; quiche::MAX_CONN_ID_LEN];
        if ring::rand::SystemRandom::new()
            .fill(&mut server_cid)
            .is_err()
        {
            error!(%peer, "secure random generation failed while creating QUIC connection id");
            return;
        }
        let scid = quiche::ConnectionId::from_ref(&server_cid);
        let key = server_cid.to_vec();
        match quiche::accept(&scid, None, local_addr, peer, quic_config) {
            Ok(conn) => {
                sessions.insert(key.clone(), Session::new(conn));
                if !packet_dcid.is_empty() {
                    cid_aliases.insert(packet_dcid, key.clone());
                }
                key
            }
            Err(error) => {
                debug!(%peer, %error, "rejected QUIC Initial packet");
                return;
            }
        }
    };

    let Some(session) = sessions.get_mut(&connection_id) else {
        return;
    };
    let recv_info = quiche::RecvInfo {
        from: peer,
        to: local_addr,
    };
    if let Err(error) = session.conn.recv(&mut bytes, recv_info) {
        debug!(%peer, %error, "discarding invalid QUIC datagram");
        return;
    }
    if session.h3.is_none() && session.conn.is_established() {
        match quiche::h3::Connection::with_transport(&mut session.conn, h3_config) {
            Ok(h3) => {
                session.h3 = Some(h3);
                info!(%peer, "HTTP/3 connection established");
            }
            Err(error) => {
                warn!(%peer, %error, "could not initialize HTTP/3 on established QUIC connection");
                session
                    .conn
                    .close(true, 0x102, b"HTTP/3 initialization failed")
                    .ok();
                return;
            }
        }
    }

    let completed = drain_h3_events(session, app);
    for completed in completed {
        let CompletedRequest {
            stream_id,
            request,
            guard,
            over_capacity,
        } = completed;
        if over_capacity || guard.is_none() {
            queue_response(
                session,
                stream_id,
                HttpResponse::error(503, "server request capacity reached"),
                guard,
            );
            continue;
        }
        let mut tx = event_tx.clone();
        let app = app.clone();
        let connection_id = connection_id.clone();
        monoio::spawn(async move {
            let response = app.dispatch(request).await;
            if tx
                .send(ServerEvent::Response {
                    connection_id,
                    stream_id,
                    response,
                    guard,
                })
                .await
                .is_err()
            {
                debug!(%peer, stream_id, "response discarded because QUIC event loop stopped");
            }
        });
    }
    pump_outgoing(session);
}

fn drain_h3_events(session: &mut Session, app: &AppRuntime) -> Vec<CompletedRequest> {
    let mut completed = Vec::new();

    loop {
        let polled = match session.h3.as_mut() {
            Some(h3) => h3.poll(&mut session.conn),
            None => break,
        };
        let (stream_id, event) = match polled {
            Ok(value) => value,
            Err(quiche::h3::Error::Done) => break,
            Err(error) => {
                debug!(%error, "HTTP/3 event polling failed");
                break;
            }
        };
        match event {
            quiche::h3::Event::Headers { list, .. } => {
                // A later HEADERS event on the same stream is a trailer block; do not
                // replace the original request or double-count its request guard.
                if session.pending_requests.contains_key(&stream_id) {
                    continue;
                }
                let guard = app.acquire_request();
                let over_capacity = guard.is_none();
                let mut request = PendingRequest {
                    method: String::new(),
                    authority: String::new(),
                    path: "/".to_owned(),
                    query: None,
                    headers: HashMap::new(),
                    body: Vec::new(),
                    body_too_large: false,
                    over_capacity,
                    guard,
                };
                for header in list {
                    let name = String::from_utf8_lossy(header.name()).to_ascii_lowercase();
                    let value = String::from_utf8_lossy(header.value()).into_owned();
                    match name.as_str() {
                        ":method" => request.method = value,
                        ":authority" => request.authority = value,
                        ":path" => {
                            let (path, query) = value
                                .split_once('?')
                                .map(|(path, query)| (path, Some(query.to_owned())))
                                .unwrap_or((value.as_str(), None));
                            request.path = path.to_owned();
                            request.query = query;
                        }
                        name if !name.starts_with(':') => {
                            request.headers.insert(name.to_owned(), value);
                        }
                        _ => {}
                    }
                }
                if request.authority.is_empty() {
                    request.authority = request.headers.get("host").cloned().unwrap_or_default();
                }
                if let Some(content_length) = request
                    .headers
                    .get("content-length")
                    .and_then(|v| v.parse::<usize>().ok())
                {
                    if content_length > app.config.max_request_body_bytes {
                        request.body_too_large = true;
                    }
                }
                session.pending_requests.insert(stream_id, request);
            }
            quiche::h3::Event::Data => {
                let mut buffer = vec![0u8; 16 * 1024];
                loop {
                    let read_result = match session.h3.as_mut() {
                        Some(h3) => h3.recv_body(&mut session.conn, stream_id, &mut buffer),
                        None => break,
                    };
                    match read_result {
                        Ok(0) | Err(quiche::h3::Error::Done) => break,
                        Ok(read) => {
                            if let Some(request) = session.pending_requests.get_mut(&stream_id) {
                                if !request.body_too_large && !request.over_capacity {
                                    if request.body.len().saturating_add(read)
                                        > app.config.max_request_body_bytes
                                    {
                                        request.body_too_large = true;
                                        request.body.clear();
                                    } else {
                                        request.body.extend_from_slice(&buffer[..read]);
                                    }
                                }
                            }
                        }
                        Err(error) => {
                            debug!(stream_id, %error, "could not read HTTP/3 request body");
                            if let Some(request) = session.pending_requests.get_mut(&stream_id) {
                                request.body_too_large = true;
                            }
                            break;
                        }
                    }
                }
            }
            quiche::h3::Event::Finished => {
                if let Some(request) = session.pending_requests.remove(&stream_id) {
                    let (request, guard, over_capacity) = request.into_parts();
                    completed.push(CompletedRequest {
                        stream_id,
                        request,
                        guard,
                        over_capacity,
                    });
                }
            }
            quiche::h3::Event::Reset(_) => {
                session.pending_requests.remove(&stream_id);
            }
            _ => {}
        }
    }
    completed
}

fn queue_response(
    session: &mut Session,
    stream_id: u64,
    response: HttpResponse,
    guard: Option<RequestGuard>,
) {
    // Keep the response until both headers and body have been accepted by quiche.
    // In particular, a StreamBlocked result for headers must be retried on a later tick,
    // not silently turn into a hung HTTP/3 stream.
    session.outgoing.push_back(OutgoingResponse {
        stream_id,
        status: response.status,
        content_type: response.content_type,
        cache_control: response.cache_control,
        body: response.body,
        offset: 0,
        head_only: response.head_only,
        headers_sent: false,
        _guard: guard,
    });
}

fn pump_outgoing(session: &mut Session) {
    let mut blocked_in_a_row = 0usize;

    loop {
        let queue_len = session.outgoing.len();
        if queue_len == 0 || blocked_in_a_row >= queue_len {
            break;
        }

        // Copy response metadata out of the queue before mutably borrowing quiche.
        // Header values must stay alive for send_response, so keep them independent
        // of the queue entry we may update or pop immediately afterwards.
        let Some((
            stream_id,
            status_code,
            content_type,
            cache_control,
            body_len,
            head_only,
            headers_sent,
        )) = session.outgoing.front().map(|front| {
            (
                front.stream_id,
                front.status,
                front.content_type.clone(),
                front.cache_control.clone(),
                front.body.len(),
                front.head_only,
                front.headers_sent,
            )
        })
        else {
            break;
        };
        let no_body = head_only || body_len == 0 || matches!(status_code, 204 | 304);

        if !headers_sent {
            let status = status_code.to_string();
            let content_length = body_len.to_string();
            let mut headers = vec![
                quiche::h3::Header::new(b":status", status.as_bytes()),
                quiche::h3::Header::new(b"server", b"websrv"),
                quiche::h3::Header::new(b"content-type", content_type.as_bytes()),
            ];
            if !matches!(status_code, 204 | 304) {
                headers.push(quiche::h3::Header::new(
                    b"content-length",
                    content_length.as_bytes(),
                ));
            }
            if let Some(cache_control) = cache_control.as_deref() {
                headers.push(quiche::h3::Header::new(
                    b"cache-control",
                    cache_control.as_bytes(),
                ));
            }
            let result = match session.h3.as_mut() {
                Some(h3) => h3.send_response(&mut session.conn, stream_id, &headers, no_body),
                None => break,
            };
            match result {
                Ok(()) => {
                    blocked_in_a_row = 0;
                    if let Some(front) = session.outgoing.front_mut() {
                        front.headers_sent = true;
                    }
                    if no_body {
                        session.outgoing.pop_front();
                        continue;
                    }
                }
                Err(quiche::h3::Error::StreamBlocked | quiche::h3::Error::Done) => {
                    rotate_outgoing(session);
                    blocked_in_a_row += 1;
                    continue;
                }
                Err(error) => {
                    debug!(stream_id, %error, "failed to queue HTTP/3 response headers");
                    session.outgoing.pop_front();
                    blocked_in_a_row = 0;
                    continue;
                }
            }
        }

        // Refresh the current stream's body offset after possibly sending headers.
        let Some(front) = session.outgoing.front() else {
            break;
        };
        let start = front.offset;
        let end = (start + RESPONSE_CHUNK).min(front.body.len());
        let final_chunk = end == front.body.len();
        let chunk = front.body[start..end].to_vec();
        let result = match session.h3.as_mut() {
            Some(h3) => h3.send_body(&mut session.conn, stream_id, &chunk, final_chunk),
            None => break,
        };
        match result {
            Ok(0) => {
                // A zero-byte write means quiche accepted no body bytes. Keep the
                // response intact and give other queued streams a chance to progress.
                rotate_outgoing(session);
                blocked_in_a_row += 1;
            }
            Ok(written) => {
                blocked_in_a_row = 0;
                let accepted_end = start.saturating_add(written).min(body_len);
                if final_chunk && accepted_end == body_len {
                    session.outgoing.pop_front();
                } else if let Some(front) = session.outgoing.front_mut() {
                    front.offset = accepted_end;
                }
            }
            Err(quiche::h3::Error::StreamBlocked | quiche::h3::Error::Done) => {
                rotate_outgoing(session);
                blocked_in_a_row += 1;
            }
            Err(error) => {
                debug!(stream_id, %error, "failed to queue HTTP/3 response body");
                session.outgoing.pop_front();
                blocked_in_a_row = 0;
            }
        }
    }
}

fn rotate_outgoing(session: &mut Session) {
    if let Some(response) = session.outgoing.pop_front() {
        session.outgoing.push_back(response);
    }
}

fn tick_sessions(sessions: &mut HashMap<Vec<u8>, Session>, h3_config: &quiche::h3::Config) {
    for session in sessions.values_mut() {
        if session
            .conn
            .timeout()
            .map(|duration| duration.is_zero())
            .unwrap_or(false)
        {
            session.conn.on_timeout();
        }
        if session.h3.is_none() && session.conn.is_established() {
            match quiche::h3::Connection::with_transport(&mut session.conn, h3_config) {
                Ok(h3) => {
                    session.h3 = Some(h3);
                    info!("HTTP/3 connection established after timer event");
                }
                Err(error) => {
                    warn!(%error, "could not initialize HTTP/3 after QUIC timer event");
                    session
                        .conn
                        .close(true, 0x102, b"HTTP/3 initialization failed")
                        .ok();
                }
            }
        }
        pump_outgoing(session);
    }
}

async fn pump_all(socket: &Rc<UdpSocket>, sessions: &mut HashMap<Vec<u8>, Session>) {
    let mut packets = Vec::<(SocketAddr, Vec<u8>)>::new();
    for session in sessions.values_mut() {
        loop {
            let mut packet = vec![0u8; QUIC_PACKET_BUFFER];
            match session.conn.send(&mut packet) {
                Ok((written, info)) => {
                    packet.truncate(written);
                    packets.push((info.to, packet));
                }
                Err(quiche::Error::Done) => break,
                Err(error) => {
                    debug!(%error, "QUIC packet generation stopped for a connection");
                    break;
                }
            }
        }
    }
    for (peer, packet) in packets {
        let packet_len = packet.len();
        let (result, _buffer) = socket.send_to(packet, peer).await;
        match result {
            Ok(sent) if sent == packet_len => {}
            Ok(sent) => warn!(%peer, expected = packet_len, sent, "short UDP datagram send"),
            Err(error) => warn!(%peer, %error, "io_uring UDP send failed"),
        }
    }
}

pub fn install_shutdown_handler() -> Result<Arc<AtomicBool>> {
    let shutdown = Arc::new(AtomicBool::new(false));
    signal_hook::flag::register(signal_hook::consts::SIGINT, shutdown.clone())
        .context("could not install SIGINT handler")?;
    signal_hook::flag::register(signal_hook::consts::SIGTERM, shutdown.clone())
        .context("could not install SIGTERM handler")?;
    Ok(shutdown)
}

#[cfg(test)]
mod tests {
    use super::PendingRequest;

    #[test]
    fn request_builder_keeps_host_and_query_separate() {
        let request = PendingRequest {
            method: "GET".into(),
            authority: "example.com".into(),
            path: "/x".into(),
            query: Some("a=1".into()),
            headers: Default::default(),
            body: vec![],
            body_too_large: false,
            over_capacity: false,
            guard: None,
        }
        .into_parts()
        .0;
        assert_eq!(request.authority, "example.com");
        assert_eq!(request.path, "/x");
        assert_eq!(request.query.as_deref(), Some("a=1"));
    }
}
