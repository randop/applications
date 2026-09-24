mod clock;
mod config;
mod ntp;

use clap::Parser;
use config::{Cli, Config};

fn main() -> anyhow::Result<()> {
    let cli = Cli::parse();
    let cfg = Config::resolve(&cli)?;

    if cli.print_config {
        println!(
            "server = {}\nport = {}\ntimeout_secs = {}\nretries = {}\nntp_version = {}\nformat = {}\nset_system_time = {}\nsync_hwclock = {}\ndry_run = {}\nmax_offset_secs = {}\nconfig_file = {}",
            cfg.server,
            cfg.port,
            cfg.timeout.as_secs_f64(),
            cfg.retries,
            cfg.ntp_version,
            cfg.format,
            cfg.set_system_time,
            cfg.sync_hwclock,
            cfg.dry_run,
            cfg.max_offset_secs,
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
            if cfg.set_system_time || cfg.sync_hwclock || cfg.dry_run {
                apply_clock_sync(&cfg, &m)?;
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

/// Step the system clock to the NTP-corrected time, and optionally the RTC.
fn apply_clock_sync(cfg: &Config, m: &ntp::NtpMeasurement) -> anyhow::Result<()> {
    let offset = m.offset_secs.ok_or_else(|| {
        anyhow::anyhow!("cannot sync clock: server did not provide usable timestamps")
    })?;

    if cfg.max_offset_secs > 0.0 && offset.abs() > cfg.max_offset_secs {
        anyhow::bail!(
            "refusing to step clock: |offset| {:.3}s exceeds --max-offset-secs {:.3}s",
            offset,
            cfg.max_offset_secs
        );
    }

    // Recompute target as close to the set call as possible.
    let target = clock::corrected_now_unix(offset);
    let target_str = clock::format_unix(target);

    if cfg.dry_run {
        println!(
            "dry-run: would set system clock to {target_str} (offset {offset:+.6}s){}",
            if cfg.sync_hwclock {
                " and run `hwclock --systohc`"
            } else {
                ""
            }
        );
        return Ok(());
    }

    clock::set_system_time(target)?;
    println!("system clock set to {target_str} (offset {offset:+.6}s)");

    if cfg.sync_hwclock {
        clock::sync_hwclock()?;
        println!("hardware clock synced (`hwclock --systohc`)");
    }
    Ok(())
}
