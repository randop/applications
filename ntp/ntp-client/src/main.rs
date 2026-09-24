mod config;
mod ntp;

use clap::Parser;
use config::{Cli, Config};

fn main() -> anyhow::Result<()> {
    let cli = Cli::parse();
    let cfg = Config::resolve(&cli)?;

    if cli.print_config {
        println!(
            "server = {}\nport = {}\ntimeout_secs = {}\nretries = {}\nntp_version = {}\nformat = {}\nconfig_file = {}",
            cfg.server,
            cfg.port,
            cfg.timeout.as_secs_f64(),
            cfg.retries,
            cfg.ntp_version,
            cfg.format,
            cfg.config_file_used
                .as_ref()
                .map(|p| p.display().to_string())
                .unwrap_or_else(|| "(none, built-in defaults)".into()),
        );
        return Ok(());
    }

    match ntp::query_with_retries(
        &cfg.server,
        cfg.port,
        cfg.timeout,
        cfg.retries,
        cfg.ntp_version,
    ) {
        Ok(m) => {
            match cfg.format {
                config::OutputFormat::Json => {
                    println!("{}", serde_json::to_string_pretty(&m)?);
                }
                config::OutputFormat::Text => {
                    println!(
                        "NTP reply from {} (stratum {}, version {})",
                        m.server, m.stratum, m.version
                    );
                    if let Some(t) = &m.server_time_rfc3339 {
                        println!("server time : {t}");
                    }
                    if let Some(off) = m.offset_secs {
                        println!("clock offset: {:+.6} s (positive = server ahead)", off);
                    } else {
                        println!("clock offset: n/a (missing server timestamps)");
                    }
                    if let Some(d) = m.delay_secs {
                        println!("round-trip delay: {:.6} s", d);
                    }
                    println!("rtt wall    : {:.6} s (t4 - t1)", m.t4 - m.t1);
                }
            }
            Ok(())
        }
        Err(e) => {
            eprintln!(
                "error: NTP query to {}:{} failed: {:#}",
                cfg.server, cfg.port, e
            );
            std::process::exit(1);
        }
    }
}
