//! System + hardware (RTC) clock synchronization.
//!
//! Linux model:
//! - System clock = kernel software clock (`CLOCK_REALTIME`). Set via
//!   `clock_settime(2)` — requires `CAP_SYS_TIME` (normally: run as root).
//! - Hardware clock = RTC (`/dev/rtc0`), persistent across reboots. Synced
//!   from the system clock via `hwclock --systohc` (util-linux).
//!
//! Typical flow for `--sync-hwclock`:
//! 1. NTP query -> offset
//! 2. `clock_settime(CLOCK_REALTIME, now + offset)`
//! 3. `hwclock --systohc`

use std::process::Command;

/// Target time = current system time corrected by the NTP offset.
pub fn corrected_now_unix(offset_secs: f64) -> f64 {
    crate::ntp::system_now_unix() + offset_secs
}

/// Set the system clock (`CLOCK_REALTIME`) to the given Unix timestamp.
/// Requires CAP_SYS_TIME; returns a friendly error on EPERM.
pub fn set_system_time(unix_secs: f64) -> anyhow::Result<()> {
    #[cfg(not(target_os = "linux"))]
    {
        let _ = unix_secs;
        anyhow::bail!("--set-system-time is only supported on Linux");
    }

    #[cfg(target_os = "linux")]
    {
        let secs = unix_secs.floor() as libc::time_t;
        let nanos = ((unix_secs - unix_secs.floor()) * 1e9).round() as libc::c_long;
        let ts = libc::timespec {
            tv_sec: secs,
            tv_nsec: nanos,
        };
        // SAFETY: plain clock_settime syscall wrapper with a valid timespec.
        let rc = unsafe { libc::clock_settime(libc::CLOCK_REALTIME, &ts) };
        if rc != 0 {
            let err = std::io::Error::last_os_error();
            if err.raw_os_error() == Some(libc::EPERM) {
                anyhow::bail!(
                    "clock_settime: operation not permitted — need root / CAP_SYS_TIME (try sudo). OS error: {err}"
                );
            }
            anyhow::bail!("clock_settime failed: {err}");
        }
        Ok(())
    }
}

/// Write the current system time into the hardware clock (RTC).
/// Delegates to `hwclock --systohc` from util-linux.
pub fn sync_hwclock() -> anyhow::Result<()> {
    #[cfg(not(target_os = "linux"))]
    {
        anyhow::bail!("--sync-hwclock is only supported on Linux");
    }

    #[cfg(target_os = "linux")]
    {
        let out = Command::new("hwclock")
            .arg("--systohc")
            .output()
            .map_err(|e| {
                anyhow::anyhow!("failed to run `hwclock --systohc` (is util-linux installed?): {e}")
            })?;
        if !out.status.success() {
            let stderr = String::from_utf8_lossy(&out.stderr);
            anyhow::bail!("`hwclock --systohc` failed: {}", stderr.trim());
        }
        Ok(())
    }
}

/// Format a Unix timestamp as RFC3339 UTC for display.
pub fn format_unix(unix_secs: f64) -> String {
    let whole = unix_secs.floor() as i64;
    let nanos = ((unix_secs - unix_secs.floor()) * 1e9).round() as u32;
    chrono::DateTime::from_timestamp(whole, nanos)
        .map(|dt| dt.to_rfc3339_opts(chrono::SecondsFormat::Millis, true))
        .unwrap_or_else(|| format!("unix={unix_secs}"))
}
