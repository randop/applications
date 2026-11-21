mod auth;
mod config;
mod dto;
mod error;
mod graphql;
mod handlers;
mod models;
mod repo;
mod routes;
mod state;

use std::net::{Ipv4Addr, Ipv6Addr, SocketAddr};
use std::sync::Arc;
use std::time::Duration;

use axum::http::StatusCode;
use socket2::{Domain, Protocol, Socket, Type};
use tower_http::compression::CompressionLayer;
use tower_http::cors::{Any, CorsLayer};
use tower_http::limit::RequestBodyLimitLayer;
use tower_http::timeout::TimeoutLayer;
use tower_http::trace::TraceLayer;

use crate::config::{init_db, Config};
use crate::state::AppState;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    dotenvy::dotenv().ok();

    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "ats=info,tower_http=info,sqlx=warn".into()),
        )
        .init();

    let config = Config::from_env()?;
    let db = init_db(&config.database_url).await?;

    let state = AppState {
        db,
        jwt_secret: config.jwt_secret,
        jwt_ttl_hours: config.jwt_ttl_hours,
        // Build the GraphQL schema once and share it across all requests.
        schema: Arc::new(crate::graphql::build_schema()),
    };

    let cors = CorsLayer::new()
        .allow_origin(Any)
        .allow_methods(Any)
        .allow_headers(Any);

    // Layer order (outermost last): trace wraps everything for accurate latency,
    // the body limit rejects oversized payloads early, the timeout bounds handler
    // latency, and compression shrinks JSON responses.
    let app = routes::api_routes()
        .layer(cors)
        .layer(CompressionLayer::new())
        .layer(TimeoutLayer::with_status_code(
            StatusCode::REQUEST_TIMEOUT,
            Duration::from_secs(30),
        ))
        .layer(RequestBodyLimitLayer::new(2 * 1024 * 1024))
        .layer(TraceLayer::new_for_http())
        .with_state(state);

    // Bind every resolved address. By default this is both IPv4 (0.0.0.0) and
    // IPv6 ([::]); the IPv6 socket is marked V6ONLY so both can share the port.
    let bind_addrs = resolve_bind_addrs(config.port)?;
    let mut listeners = Vec::new();
    for addr in bind_addrs {
        match bind_listener(addr) {
            Ok(listener) => {
                tracing::info!("ATS API listening on http://{addr}");
                listeners.push(listener);
            }
            // IPv6 is unavailable in some environments (e.g. containers with it
            // disabled); fall back to the remaining addresses instead of failing.
            Err(err) if addr.is_ipv6() => {
                tracing::warn!("could not bind IPv6 address {addr}: {err}");
            }
            Err(err) => return Err(err.into()),
        }
    }

    if listeners.is_empty() {
        anyhow::bail!("no listen addresses could be bound");
    }

    let mut tasks = Vec::new();
    for listener in listeners {
        let app = app.clone();
        tasks.push(tokio::spawn(async move {
            if let Err(err) = axum::serve(listener, app).await {
                tracing::error!("server exited with error: {err}");
            }
        }));
    }

    for task in tasks {
        task.await?;
    }

    Ok(())
}

/// Resolve which socket addresses to listen on.
///
/// Priority:
///   1. `BIND_ADDRS` — comma-separated socket addresses, e.g. `0.0.0.0:8080,[::]:8080`
///   2. `BIND_ADDR`  — a single socket address, e.g. `127.0.0.1:8080`
///   3. Default      — both `0.0.0.0:<port>` and `[::]:<port>`
fn resolve_bind_addrs(port: u16) -> anyhow::Result<Vec<SocketAddr>> {
    if let Ok(raw) = std::env::var("BIND_ADDRS") {
        let mut addrs = Vec::new();
        for part in raw.split(',') {
            let part = part.trim();
            if part.is_empty() {
                continue;
            }
            let addr = part
                .parse::<SocketAddr>()
                .map_err(|e| anyhow::anyhow!("invalid BIND_ADDRS entry '{part}': {e}"))?;
            addrs.push(addr);
        }
        if addrs.is_empty() {
            anyhow::bail!("BIND_ADDRS is set but contains no addresses");
        }
        return Ok(addrs);
    }

    if let Ok(raw) = std::env::var("BIND_ADDR") {
        let addr = raw
            .trim()
            .parse::<SocketAddr>()
            .map_err(|e| anyhow::anyhow!("invalid BIND_ADDR '{raw}': {e}"))?;
        return Ok(vec![addr]);
    }

    Ok(vec![
        SocketAddr::from((Ipv4Addr::UNSPECIFIED, port)),
        SocketAddr::from((Ipv6Addr::UNSPECIFIED, port)),
    ])
}

/// Bind a single TCP listener, setting `IPV6_V6ONLY` on IPv6 sockets so an IPv4
/// listener can bind the same port independently.
fn bind_listener(addr: SocketAddr) -> std::io::Result<tokio::net::TcpListener> {
    let domain = match addr {
        SocketAddr::V4(_) => Domain::IPV4,
        SocketAddr::V6(_) => Domain::IPV6,
    };
    let socket = Socket::new(domain, Type::STREAM, Some(Protocol::TCP))?;
    socket.set_reuse_address(true)?;
    if addr.is_ipv6() {
        socket.set_only_v6(true)?;
    }
    socket.bind(&addr.into())?;
    socket.listen(1024)?;
    socket.set_nonblocking(true)?;

    let std_listener: std::net::TcpListener = socket.into();
    tokio::net::TcpListener::from_std(std_listener)
}
