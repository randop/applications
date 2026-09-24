use anyhow::{bail, Context, Result};
use serde::Deserialize;
use std::path::PathBuf;

#[derive(Debug, Clone, Deserialize)]
pub struct Config {
    pub server: ServerConfig,
    pub storage: StorageConfig,
    pub auth: AuthConfig,
    pub limits: LimitsConfig,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ServerConfig {
    pub listen: String,
    pub tls: TlsConfig,
}

#[derive(Debug, Clone, Deserialize)]
pub struct TlsConfig {
    pub certificate: PathBuf,
    pub private_key: PathBuf,
}

#[derive(Debug, Clone, Deserialize)]
pub struct StorageConfig {
    pub directory: PathBuf,
}

#[derive(Debug, Clone, Deserialize)]
pub struct AuthConfig {
    pub oauth_enabled: bool,
    pub workos: WorkosConfig,
}

#[derive(Debug, Clone, Deserialize)]
pub struct WorkosConfig {
    pub issuer: String,
    pub audience: String,
    pub jwks_url: String,
}

#[derive(Debug, Clone, Deserialize)]
pub struct LimitsConfig {
    pub max_command_bytes: usize,
    pub max_literal_bytes: usize,
}

impl Config {
    pub fn load(path: &str) -> Result<Self> {
        let cfg = config::Config::builder()
            .add_source(config::File::with_name(path))
            .add_source(config::Environment::with_prefix("IMAP").separator("__"))
            .build()
            .context("load configuration")?;
        let cfg: Self = cfg.try_deserialize().context("deserialize configuration")?;
        cfg.validate()?;
        Ok(cfg)
    }

    pub fn validate(&self) -> Result<()> {
        if !self.auth.oauth_enabled {
            bail!("auth.oauth_enabled must be true; this server supports OAuth authentication only");
        }
        if self.auth.workos.issuer.trim().is_empty()
            || self.auth.workos.audience.trim().is_empty()
            || self.auth.workos.jwks_url.trim().is_empty()
        {
            bail!("auth.workos issuer, audience, and jwks_url are required when OAuth is enabled");
        }
        if self.limits.max_command_bytes < 1024 {
            bail!("limits.max_command_bytes must be at least 1024");
        }
        if self.limits.max_literal_bytes == 0 {
            bail!("limits.max_literal_bytes must be greater than zero");
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn valid() -> Config {
        Config {
            server: ServerConfig {
                listen: "127.0.0.1:0".into(),
                tls: TlsConfig {
                    certificate: "cert.pem".into(),
                    private_key: "key.pem".into(),
                },
            },
            storage: StorageConfig { directory: "mail".into() },
            auth: AuthConfig {
                oauth_enabled: true,
                workos: WorkosConfig {
                    issuer: "https://example.com".into(),
                    audience: "client".into(),
                    jwks_url: "https://example.com/oauth2/jwks".into(),
                },
            },
            limits: LimitsConfig {
                max_command_bytes: 16384,
                max_literal_bytes: 1024,
            },
        }
    }

    #[test]
    fn oauth_is_mandatory() {
        let mut c = valid();
        c.auth.oauth_enabled = false;
        assert!(c.validate().is_err());
    }
}
