use crate::config::WorkosConfig;
use anyhow::{Context, Result, bail};
use jsonwebtoken::{Algorithm, DecodingKey, Validation, decode, decode_header};
use reqwest::Client;
use serde::Deserialize;
use std::sync::Arc;
use tokio::sync::RwLock;

#[derive(Clone)]
pub struct WorkosAuthenticator {
    cfg: WorkosConfig,
    client: Client,
    jwks: Arc<RwLock<Option<Jwks>>>,
}

#[derive(Debug, Clone)]
pub struct Identity {
    pub subject: String,
    #[allow(dead_code)]
    pub email: Option<String>,
}

#[derive(Debug, Clone, Deserialize)]
struct Jwks {
    keys: Vec<Jwk>,
}

#[derive(Debug, Clone, Deserialize)]
struct Jwk {
    kty: String,
    kid: String,
    n: String,
    e: String,
    #[serde(default)]
    alg: Option<String>,
}

#[derive(Debug, Deserialize)]
struct Claims {
    sub: String,
    #[serde(default)]
    email: Option<String>,
}

impl WorkosAuthenticator {
    pub fn new(cfg: WorkosConfig) -> Result<Self> {
        let client = Client::builder()
            .https_only(true)
            .build()
            .context("build OAuth HTTP client")?;
        Ok(Self {
            cfg,
            client,
            jwks: Arc::new(RwLock::new(None)),
        })
    }

    async fn refresh_jwks(&self) -> Result<Jwks> {
        let response = self
            .client
            .get(&self.cfg.jwks_url)
            .send()
            .await
            .context("WorkOS JWKS request")?
            .error_for_status()
            .context("WorkOS JWKS request failed")?;
        let jwks: Jwks = response.json().await.context("decode WorkOS JWKS")?;
        if jwks.keys.is_empty() {
            bail!("WorkOS JWKS contains no signing keys");
        }
        *self.jwks.write().await = Some(jwks.clone());
        Ok(jwks)
    }

    async fn current_jwks(&self) -> Result<Jwks> {
        if let Some(jwks) = self.jwks.read().await.clone() {
            return Ok(jwks);
        }
        self.refresh_jwks().await
    }

    pub async fn validate(&self, token: &str) -> Result<Identity> {
        let header = decode_header(token).context("decode JWT header")?;
        if header.alg != Algorithm::RS256 {
            bail!("unsupported OAuth JWT signing algorithm");
        }
        let kid = header.kid.context("OAuth JWT has no key id")?;

        let mut jwks = self.current_jwks().await?;
        let mut key = jwks.keys.iter().find(|k| k.kid == kid && k.kty == "RSA");
        if key.is_none() {
            jwks = self.refresh_jwks().await?;
            key = jwks.keys.iter().find(|k| k.kid == kid && k.kty == "RSA");
        }
        let key = key.context("OAuth JWT signing key not found")?;
        if let Some(alg) = &key.alg {
            if alg != "RS256" {
                bail!("WorkOS JWKS key is not RS256");
            }
        }

        let decoding_key =
            DecodingKey::from_rsa_components(&key.n, &key.e).context("build RSA decoding key")?;
        let mut validation = Validation::new(Algorithm::RS256);
        validation.set_issuer(&[self.cfg.issuer.as_str()]);

        if !self.cfg.audience.trim().is_empty() {
            validation.set_audience(&[self.cfg.audience.as_str()]);
        } else {
            validation.validate_aud = false;
        }

        let claims = decode::<Claims>(token, &decoding_key, &validation)
            .context("validate OAuth JWT")?
            .claims;

        Ok(Identity {
            subject: claims.sub,
            email: claims.email,
        })
    }
}

pub type SharedAuthenticator = Arc<WorkosAuthenticator>;
