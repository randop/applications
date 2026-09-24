mod config;
mod oauth;
mod protocol;
mod session;
mod store;
mod tls;
mod watcher;

use anyhow::{Context, Result};
use config::Config;
use oauth::WorkosAuthenticator;
use session::Session;
use std::sync::Arc;
use tokio::net::TcpListener;
use tracing::{error, info};
use rustls::crypto::ring;
use store::DirectoryStore;

#[tokio::main]
async fn main() -> Result<()> {
    ring::default_provider()
        .install_default()
        .expect("failed to install rustls crypto provider");

    if std::env::args().any(|a| a == "--version") {
        println!("imap-server {}", env!("CARGO_PKG_VERSION"));
        return Ok(());
    }

    tracing_subscriber::fmt()
        .with_env_filter("info")
        .init();

    let config_path = std::env::var("IMAP_CONFIG").unwrap_or_else(|_| "config.toml".into());
    let cfg = Arc::new(Config::load(&config_path)?);

    std::fs::create_dir_all(&cfg.storage.directory)
        .with_context(|| format!("create {}", cfg.storage.directory.display()))?;

    let store = Arc::new(DirectoryStore::open(&cfg.storage.directory)?);
    let _watcher = store.start_watcher()?;
    let auth = Arc::new(WorkosAuthenticator::new(cfg.auth.workos.clone())?);
    let tls = tls::load_server_config(
        &cfg.server.tls.certificate,
        &cfg.server.tls.private_key,
    )?;

    let listener = TcpListener::bind(&cfg.server.listen).await?;
    info!(listen = %cfg.server.listen, storage = %cfg.storage.directory.display(), "IMAP server listening");

    loop {
        let (socket, peer) = listener.accept().await?;
        let tls = tls.clone();
        let store = store.clone();
        let auth = auth.clone();
        let cfg = cfg.clone();

        tokio::spawn(async move {
            match tokio_rustls::TlsAcceptor::from(tls).accept(socket).await {
                Ok(stream) => {
                    if let Err(e) = Session::run(stream, peer, store, auth, cfg).await {
                        error!(%peer, error = %e, "session failed");
                    }
                }
                Err(e) => error!(%peer, error = %e, "TLS handshake failed"),
            }
        });
    }
}
