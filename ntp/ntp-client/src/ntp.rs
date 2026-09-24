use serde::{Deserialize, Serialize};
use std::path::PathBuf;

pub const NTP_PACKET_SIZE: usize = 48;
/// Seconds between NTP epoch (1900-01-01) and Unix epoch (1970-01-01).
pub const NTP_UNIX_EPOCH_DIFF: u64 = 2_208_988_800;

/// Raw NTP packet (48 bytes, RFC 5905).
#[derive(Debug, Clone, Copy)]
pub struct NtpPacket {
    pub data: [u8; NTP_PACKET_SIZE],
}

impl NtpPacket {
    /// Build a client request packet for the given NTP version (3 or 4).
    pub fn new_request(version: u8) -> Self {
        assert!((3..=4).contains(&version), "version must be 3 or 4");
        let mut data = [0u8; NTP_PACKET_SIZE];
        // LI = 0 (no warning), VN = version, Mode = 3 (client)
        data[0] = (version << 3) | 3;
        // Transmit timestamp (bytes 40..48) filled in by caller via set_transmit_timestamp
        Self { data }
    }

    pub fn set_transmit_timestamp(&mut self, ntp_secs: u32, ntp_frac: u32) {
        self.data[40..44].copy_from_slice(&ntp_secs.to_be_bytes());
        self.data[44..48].copy_from_slice(&ntp_frac.to_be_bytes());
    }

    pub fn from_bytes(buf: &[u8]) -> anyhow::Result<Self> {
        if buf.len() < NTP_PACKET_SIZE {
            anyhow::bail!(
                "NTP response too short: got {} bytes, expected {NTP_PACKET_SIZE}",
                buf.len()
            );
        }
        let mut data = [0u8; NTP_PACKET_SIZE];
        data.copy_from_slice(&buf[..NTP_PACKET_SIZE]);
        Ok(Self { data })
    }

    pub fn leap_indicator(&self) -> u8 {
        (self.data[0] >> 6) & 0x03
    }
    pub fn version(&self) -> u8 {
        (self.data[0] >> 3) & 0x07
    }
    pub fn mode(&self) -> u8 {
        self.data[0] & 0x07
    }
    pub fn stratum(&self) -> u8 {
        self.data[1]
    }
    pub fn poll(&self) -> i8 {
        self.data[2] as i8
    }
    pub fn precision(&self) -> i8 {
        self.data[3] as i8
    }
    pub fn root_delay(&self) -> u32 {
        u32::from_be_bytes([self.data[4], self.data[5], self.data[6], self.data[7]])
    }
    pub fn root_dispersion(&self) -> u32 {
        u32::from_be_bytes([self.data[8], self.data[9], self.data[10], self.data[11]])
    }
    pub fn reference_id(&self) -> [u8; 4] {
        [self.data[12], self.data[13], self.data[14], self.data[15]]
    }
    fn timestamp_at(&self, offset: usize) -> (u32, u32) {
        let s = u32::from_be_bytes([
            self.data[offset],
            self.data[offset + 1],
            self.data[offset + 2],
            self.data[offset + 3],
        ]);
        let f = u32::from_be_bytes([
            self.data[offset + 4],
            self.data[offset + 5],
            self.data[offset + 6],
            self.data[offset + 7],
        ]);
        (s, f)
    }
    pub fn reference_timestamp(&self) -> (u32, u32) {
        self.timestamp_at(16)
    }
    pub fn originate_timestamp(&self) -> (u32, u32) {
        self.timestamp_at(24)
    }
    pub fn receive_timestamp(&self) -> (u32, u32) {
        self.timestamp_at(32)
    }
    pub fn transmit_timestamp(&self) -> (u32, u32) {
        self.timestamp_at(40)
    }
}

/// Convert NTP (secs, frac) to f64 seconds since NTP epoch.
pub fn ntp_to_f64(secs: u32, frac: u32) -> f64 {
    secs as f64 + frac as f64 / u32::MAX as f64
}

/// Convert NTP timestamp to Unix seconds as f64. Returns None on pre-1970 dates.
pub fn ntp_to_unix_f64(secs: u32, frac: u32) -> Option<f64> {
    if (secs as u64) < NTP_UNIX_EPOCH_DIFF {
        return None;
    }
    Some(secs as f64 - NTP_UNIX_EPOCH_DIFF as f64 + frac as f64 / u32::MAX as f64)
}

/// Current system time as NTP (secs, frac).
pub fn now_ntp() -> (u32, u32) {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .expect("system time before unix epoch");
    let secs = now.as_secs() + NTP_UNIX_EPOCH_DIFF;
    let frac = ((now.subsec_nanos() as u64 * (1u64 << 32)) / 1_000_000_000) as u32;
    (secs as u32, frac)
}

/// Format NTP timestamp as RFC3339/ISO8601 in UTC, if representable.
pub fn format_ntp_time(secs: u32, frac: u32) -> String {
    match ntp_to_unix_f64(secs, frac) {
        Some(unix) => {
            let whole = unix.floor() as i64;
            let nanos = ((unix - unix.floor()) * 1e9).round() as u32;
            chrono::DateTime::from_timestamp(whole, nanos)
                .map(|dt| dt.to_rfc3339_opts(chrono::SecondsFormat::Millis, true))
                .unwrap_or_else(|| format!("ntp={secs}.{frac}"))
        }
        None => format!("pre-1970 ntp={secs}.{frac}"),
    }
}

/// Result of one NTP exchange, with RFC 5905 offset/delay computation.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NtpMeasurement {
    pub server: String,
    pub stratum: u8,
    pub version: u8,
    pub leap_indicator: u8,
    pub poll_secs: f64,
    pub precision_secs: f64,
    /// t1: client send, t2: server receive, t3: server transmit, t4: client receive (unix secs f64)
    pub t1: f64,
    pub t2: Option<f64>,
    pub t3: Option<f64>,
    pub t4: f64,
    /// Clock offset (seconds): ((t2-t1)+(t3-t4))/2. Positive => server ahead.
    pub offset_secs: Option<f64>,
    /// Round-trip delay (seconds): (t4-t1)-(t3-t2).
    pub delay_secs: Option<f64>,
    pub server_time_rfc3339: Option<String>,
}

impl NtpMeasurement {
    pub fn compute(
        t1: f64,
        t2: Option<f64>,
        t3: Option<f64>,
        t4: f64,
    ) -> (Option<f64>, Option<f64>) {
        match (t2, t3) {
            (Some(t2), Some(t3)) => {
                let offset = ((t2 - t1) + (t3 - t4)) / 2.0;
                let delay = (t4 - t1) - (t3 - t2);
                (Some(offset), Some(delay))
            }
            _ => (None, None),
        }
    }
}

/// Perform a single NTP request/response exchange over UDP.
pub fn query_once(
    server_addr: &str,
    timeout: std::time::Duration,
    version: u8,
) -> anyhow::Result<(NtpPacket, f64, f64)> {
    use std::net::UdpSocket;

    let socket = UdpSocket::bind("0.0.0.0:0")?;
    socket.set_read_timeout(Some(timeout))?;
    socket.set_write_timeout(Some(timeout))?;
    socket.connect(server_addr)?;

    let (tx_secs, tx_frac) = now_ntp();
    let mut req = NtpPacket::new_request(version);
    req.set_transmit_timestamp(tx_secs, tx_frac);

    let t1 = system_now_unix();
    socket.send(&req.data)?;

    let mut buf = [0u8; 512];
    let n = socket.recv(&mut buf)?;
    let t4 = system_now_unix();

    let packet = NtpPacket::from_bytes(&buf[..n])?;

    // Basic validation
    if packet.mode() != 4 {
        anyhow::bail!(
            "unexpected NTP mode {} (expected 4 = server)",
            packet.mode()
        );
    }
    if packet.stratum() == 0 {
        anyhow::bail!("server returned stratum 0 (kiss-of-death / unspecified)");
    }

    Ok((packet, t1, t4))
}

pub fn system_now_unix() -> f64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .expect("system time before unix epoch")
        .as_secs_f64()
}

/// Query with retries, returning a full measurement.
pub fn query_with_retries(
    host: &str,
    port: u16,
    timeout: std::time::Duration,
    retries: u32,
    version: u8,
) -> anyhow::Result<NtpMeasurement> {
    let server_addr = format!("{host}:{port}");
    let mut last_err = None;
    for attempt in 0..=retries {
        match query_once(&server_addr, timeout, version) {
            Ok((packet, t1, t4)) => {
                let (rx_s, rx_f) = packet.receive_timestamp();
                let (tx_s, tx_f) = packet.transmit_timestamp();
                let t2 = ntp_to_unix_f64(rx_s, rx_f);
                let t3 = ntp_to_unix_f64(tx_s, tx_f);
                let (offset, delay) = NtpMeasurement::compute(t1, t2, t3, t4);
                let server_time = ntp_to_unix_f64(tx_s, tx_f).map(|unix| {
                    let whole = unix.floor() as i64;
                    let nanos = ((unix - unix.floor()) * 1e9).round() as u32;
                    chrono::DateTime::from_timestamp(whole, nanos)
                        .map(|dt| dt.to_rfc3339_opts(chrono::SecondsFormat::Millis, true))
                        .unwrap_or_default()
                });
                return Ok(NtpMeasurement {
                    server: server_addr,
                    stratum: packet.stratum(),
                    version: packet.version(),
                    leap_indicator: packet.leap_indicator(),
                    poll_secs: 2f64.powi(packet.poll() as i32),
                    precision_secs: 2f64.powi(packet.precision() as i32),
                    t1,
                    t2,
                    t3,
                    t4,
                    offset_secs: offset,
                    delay_secs: delay,
                    server_time_rfc3339: server_time,
                });
            }
            Err(e) => {
                last_err = Some(e);
                if attempt < retries {
                    std::thread::sleep(std::time::Duration::from_millis(200));
                }
            }
        }
    }
    Err(last_err.unwrap_or_else(|| anyhow::anyhow!("no attempts made")))
}

#[allow(dead_code)]
pub fn default_config_path() -> Option<PathBuf> {
    std::env::var_os("NTP_CLIENT_CONFIG")
        .map(PathBuf::from)
        .or_else(|| {
            dirs_fallback().map(|mut p| {
                p.push("ntp-client.toml");
                p
            })
        })
}

#[allow(dead_code)]
fn dirs_fallback() -> Option<PathBuf> {
    std::env::var_os("HOME").map(PathBuf::from)
}
