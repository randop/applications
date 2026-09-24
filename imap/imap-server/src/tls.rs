use anyhow::{Context, Result};
use rustls::{
    ServerConfig,
    pki_types::{CertificateDer, PrivateKeyDer},
};
use rustls_pemfile::{certs, private_key};
use std::sync::Arc;
use std::{fs::File, io::BufReader, path::Path};

pub fn load_server_config(cert: &Path, key: &Path) -> Result<Arc<ServerConfig>> {
    let mut cert_reader = BufReader::new(File::open(cert).context("open TLS certificate")?);
    let certificates: Vec<CertificateDer<'static>> =
        certs(&mut cert_reader).collect::<std::result::Result<_, _>>()?;
    anyhow::ensure!(!certificates.is_empty(), "TLS certificate chain is empty");

    let mut key_reader = BufReader::new(File::open(key).context("open TLS private key")?);
    let key: PrivateKeyDer<'static> =
        private_key(&mut key_reader)?.context("TLS private key not found")?;

    Ok(Arc::new(
        ServerConfig::builder()
            .with_no_client_auth()
            .with_single_cert(certificates, key)
            .context("build TLS server configuration")?,
    ))
}
