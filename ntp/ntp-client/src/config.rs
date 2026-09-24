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

    /// Set the system clock (CLOCK_REALTIME) to the NTP-corrected time.
    /// Requires root / CAP_SYS_TIME on Linux.
    #[arg(long, env = "NTP_SET_SYSTEM_TIME", num_args(0..=1), default_missing_value("true"), value_parser = clap::value_parser!(bool))]
    pub set_system_time: Option<bool>,

    /// Also write the system time to the hardware clock (RTC) via
    /// `hwclock --systohc`. Implies --set-system-time.
    #[arg(long, env = "NTP_SYNC_HWCLOCK", num_args(0..=1), default_missing_value("true"), value_parser = clap::value_parser!(bool))]
    pub sync_hwclock: Option<bool>,

    /// Show what time would be set without actually changing any clock.
    #[arg(long, env = "NTP_DRY_RUN", num_args(0..=1), default_missing_value("true"), value_parser = clap::value_parser!(bool))]
    pub dry_run: Option<bool>,

    /// Safety guard: refuse to step the clock if |offset| exceeds this many
    /// seconds (0 = no limit). Env NTP_MAX_OFFSET_SECS.
    #[arg(long, env = "NTP_MAX_OFFSET_SECS")]
    pub max_offset_secs: Option<f64>,
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
    pub set_system_time: Option<bool>,
    pub sync_hwclock: Option<bool>,
    pub dry_run: Option<bool>,
    pub max_offset_secs: Option<f64>,
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
    pub set_system_time: bool,
    pub sync_hwclock: bool,
    pub dry_run: bool,
    /// 0.0 = no limit
    pub max_offset_secs: f64,
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
            set_system_time: false,
            sync_hwclock: false,
            dry_run: false,
            max_offset_secs: 0.0,
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
                if let Some(v) = file.set_system_time {
                    base.set_system_time = v;
                }
                if let Some(v) = file.sync_hwclock {
                    base.sync_hwclock = v;
                }
                if let Some(v) = file.dry_run {
                    base.dry_run = v;
                }
                if let Some(v) = file.max_offset_secs {
                    base.max_offset_secs = v;
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
        if let Some(v) = cli.set_system_time {
            base.set_system_time = v;
        }
        if let Some(v) = cli.sync_hwclock {
            base.sync_hwclock = v;
        }
        if let Some(v) = cli.dry_run {
            base.dry_run = v;
        }
        if let Some(v) = cli.max_offset_secs {
            base.max_offset_secs = v;
        }
        // --sync-hwclock implies setting the system clock first.
        if base.sync_hwclock {
            base.set_system_time = true;
        }

        if !(3..=4).contains(&base.ntp_version) {
            anyhow::bail!("ntp_version must be 3 or 4, got {}", base.ntp_version);
        }
        if base.timeout.is_zero() {
            anyhow::bail!("timeout must be > 0");
        }
        if !(base.max_offset_secs >= 0.0 && base.max_offset_secs.is_finite()) {
            anyhow::bail!(
                "max_offset_secs must be >= 0 (0 = no limit), got {}",
                base.max_offset_secs
            );
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
