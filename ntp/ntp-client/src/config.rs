use clap::Parser;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::time::Duration;

/// Configurable NTP client.
///
/// Configuration precedence (highest first):
/// 1. CLI flags  2. environment variables  3. TOML config file  4. built-in defaults
#[derive(Debug, Parser, Clone)]
#[command(
    name = "ntp-client",
    version,
    about = "Minimal configurable NTP client"
)]
pub struct Cli {
    /// NTP server hostname or IP
    #[arg(long, env = "NTP_SERVER")]
    pub server: Option<String>,

    /// NTP server UDP port (standard is 123)
    #[arg(long, env = "NTP_PORT")]
    pub port: Option<u16>,

    /// Timeout per request in seconds (can be fractional, e.g. 2.5)
    #[arg(long, env = "NTP_TIMEOUT")]
    pub timeout: Option<f64>,

    /// Additional retries after the first attempt (0 = try once)
    #[arg(long, env = "NTP_RETRIES")]
    pub retries: Option<u32>,

    /// NTP protocol version to request (3 or 4)
    #[arg(long, env = "NTP_VERSION")]
    pub ntp_version: Option<u8>,

    /// Output format: text or json
    #[arg(long, env = "NTP_FORMAT")]
    pub format: Option<OutputFormat>,

    /// Path to TOML config file. If omitted, ./ntp-client.toml then $NTP_CLIENT_CONFIG are tried.
    #[arg(long, env = "NTP_CLIENT_CONFIG")]
    pub config: Option<PathBuf>,

    /// Print merged configuration and exit
    #[arg(long, default_value_t = false)]
    pub print_config: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, clap::ValueEnum, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum OutputFormat {
    Text,
    Json,
}

impl std::fmt::Display for OutputFormat {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            OutputFormat::Text => write!(f, "text"),
            OutputFormat::Json => write!(f, "json"),
        }
    }
}

/// File + defaults representation.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct FileConfig {
    pub server: Option<String>,
    pub port: Option<u16>,
    pub timeout_secs: Option<f64>,
    pub retries: Option<u32>,
    pub ntp_version: Option<u8>,
    pub format: Option<OutputFormat>,
}

/// Fully resolved runtime configuration.
#[derive(Debug, Clone)]
pub struct Config {
    pub server: String,
    pub port: u16,
    pub timeout: Duration,
    pub retries: u32,
    pub ntp_version: u8,
    pub format: OutputFormat,
    pub config_file_used: Option<PathBuf>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            server: "127.0.0.1".into(),
            port: 123,
            timeout: Duration::from_secs(5),
            retries: 2,
            ntp_version: 4,
            format: OutputFormat::Text,
            config_file_used: None,
        }
    }
}

impl Config {
    pub fn resolve(cli: &Cli) -> anyhow::Result<Self> {
        let mut base = Config::default();

        // 1) config file (lowest priority above defaults)
        let candidate = cli
            .config
            .clone()
            .or_else(|| std::env::var_os("NTP_CLIENT_CONFIG").map(PathBuf::from))
            .or_else(|| {
                let p = PathBuf::from("ntp-client.toml");
                p.exists().then_some(p)
            });

        let mut used: Option<PathBuf> = None;
        if let Some(path) = candidate {
            if path.exists() {
                let text = std::fs::read_to_string(&path).map_err(|e| {
                    anyhow::anyhow!("failed to read config {}: {e}", path.display())
                })?;
                let file: FileConfig = toml::from_str(&text).map_err(|e| {
                    anyhow::anyhow!("failed to parse config {}: {e}", path.display())
                })?;
                if let Some(v) = file.server {
                    base.server = v;
                }
                if let Some(v) = file.port {
                    base.port = v;
                }
                if let Some(v) = file.timeout_secs {
                    base.timeout = secs_to_duration(v)?;
                }
                if let Some(v) = file.retries {
                    base.retries = v;
                }
                if let Some(v) = file.ntp_version {
                    base.ntp_version = v;
                }
                if let Some(v) = file.format {
                    base.format = v;
                }
                used = Some(path);
            } else if cli.config.is_some() {
                anyhow::bail!("config file not found: {}", path.display());
            }
        }
        base.config_file_used = used;

        // 2) CLI flags already include env vars via clap(env=...), so they win.
        if let Some(v) = &cli.server {
            base.server = v.clone();
        }
        if let Some(v) = cli.port {
            base.port = v;
        }
        if let Some(v) = cli.timeout {
            base.timeout = secs_to_duration(v)?;
        }
        if let Some(v) = cli.retries {
            base.retries = v;
        }
        if let Some(v) = cli.ntp_version {
            base.ntp_version = v;
        }
        if let Some(v) = cli.format {
            base.format = v;
        }

        if !(3..=4).contains(&base.ntp_version) {
            anyhow::bail!("ntp_version must be 3 or 4, got {}", base.ntp_version);
        }
        if base.timeout.is_zero() {
            anyhow::bail!("timeout must be > 0");
        }

        Ok(base)
    }
}

fn secs_to_duration(secs: f64) -> anyhow::Result<Duration> {
    if !secs.is_finite() || secs <= 0.0 {
        anyhow::bail!("timeout must be a positive number of seconds, got {secs}");
    }
    Ok(Duration::from_secs_f64(secs))
}
