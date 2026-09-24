//! Server-side request handling (pure logic + I/O glue, no CLI).

use std::net::SocketAddr;
use std::time::SystemTime;

use tokio::net::UdpSocket;

use crate::ntp::{NtpPacket, NtpTimestamp, NTP_PACKET_SIZE};

pub type AnyError = Box<dyn std::error::Error + Send + Sync>;

/// Runtime configuration for answering NTP requests.
#[derive(Debug, Clone, Copy)]
pub struct ServerConfig {
    pub stratum: u8,
    pub ref_id: [u8; 4],
    pub precision: i8,
    pub root_delay: u32,
    pub root_dispersion: u32,
    /// Reference timestamp advertised to clients (server boot time).
    pub boot_ref_time: NtpTimestamp,
}

impl ServerConfig {
    pub fn new(
        stratum: u8,
        ref_id: [u8; 4],
        precision: i8,
        root_delay: u32,
        root_dispersion: u32,
    ) -> Self {
        Self {
            stratum,
            ref_id,
            precision,
            root_delay,
            root_dispersion,
            boot_ref_time: NtpTimestamp::now(),
        }
    }
}

/// Convert seconds to unsigned 16.16 fixed point, saturating negatives to 0.
pub fn secs_to_fixed_16_16(secs: f64) -> u32 {
    (secs.max(0.0) * 65536.0) as u32
}

pub fn log(prefix: &str, msg: &str) {
    let ts = SystemTime::now();
    eprintln!("[{ts:?}] {prefix} {msg}");
}

/// Parse `--ref-id`: exactly 4 ASCII chars.
pub fn parse_ref_id(s: &str) -> Result<[u8; 4], AnyError> {
    let b = s.as_bytes();
    if b.len() != 4 {
        return Err(format!("--ref-id must be exactly 4 ASCII chars, got {s:?}").into());
    }
    if !b.is_ascii() {
        return Err("--ref-id must be ASCII".into());
    }
    Ok([b[0], b[1], b[2], b[3]])
}

/// Handle one datagram: validate, and reply in server mode iff it is a
/// client request. Short / undecodable / non-client packets are logged and
/// dropped without a reply.
pub async fn handle_request(
    socket: &UdpSocket,
    datagram: &[u8],
    peer: SocketAddr,
    cfg: &ServerConfig,
) -> Result<(), AnyError> {
    if datagram.len() < NTP_PACKET_SIZE {
        log(
            "{req}",
            &format!("{peer} ignoring short datagram ({} bytes)", datagram.len()),
        );
        return Ok(());
    }

    let req = match NtpPacket::from_bytes(datagram) {
        Ok(p) => p,
        Err(e) => {
            log("{req}", &format!("{peer} ignoring undecodable packet: {e}"));
            return Ok(());
        }
    };

    if !req.is_client_request() {
        log(
            "{req}",
            &format!(
                "{peer} ignoring non-client packet (vn={} mode={})",
                req.version, req.mode
            ),
        );
        return Ok(());
    }

    // RFC 5905: capture receive time ASAP, transmit time as late as possible.
    let recv_time = NtpTimestamp::now();
    // Reference time: server boot time is stable; update is not tracked
    // against an upstream source in this minimal server.
    let tx_time = NtpTimestamp::now();

    let resp = NtpPacket::build_response(
        &req,
        recv_time,
        tx_time,
        cfg.stratum,
        cfg.ref_id,
        cfg.boot_ref_time,
        cfg.precision,
        cfg.root_delay,
        cfg.root_dispersion,
    );
    let out = resp.to_bytes();
    socket.send_to(&out, peer).await?;
    log(
        "{ntp}",
        &format!(
            "{peer} vn={} poll={} -> stratum={} ref={:?}",
            req.version,
            req.poll,
            cfg.stratum,
            String::from_utf8_lossy(&cfg.ref_id),
        ),
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ntp::{self, NtpTimestamp};

    #[test]
    fn ref_id_parsing() {
        assert_eq!(parse_ref_id("GPS.").unwrap(), *b"GPS.");
        assert_eq!(parse_ref_id("ATOM").unwrap(), *b"ATOM");
        assert!(parse_ref_id("TOOLONG").is_err());
        assert!(parse_ref_id("abc").is_err());
        assert!(parse_ref_id("").is_err());
    }

    #[test]
    fn ref_id_rejects_non_ascii() {
        // "aéb": 'a' + U+00E9 (2 bytes) + 'b' = 4 bytes, not ASCII.
        assert!(parse_ref_id("aéb").is_err());
    }

    #[test]
    fn ref_id_error_messages() {
        let err = parse_ref_id("abc").unwrap_err();
        assert!(err.to_string().contains("exactly 4 ASCII chars"));
        let err = parse_ref_id("aéb").unwrap_err();
        assert!(err.to_string().contains("ASCII"));
    }

    #[test]
    fn fixed_point_conversion() {
        assert_eq!(secs_to_fixed_16_16(1.0), 65536);
        assert_eq!(secs_to_fixed_16_16(0.0), 0);
        assert_eq!(secs_to_fixed_16_16(-5.0), 0);
    }

    #[test]
    fn fixed_point_fractional() {
        // 0.5s -> 0x8000, 1.5s -> 0x18000.
        assert_eq!(secs_to_fixed_16_16(0.5), 32768);
        assert_eq!(secs_to_fixed_16_16(1.5), 98304);
        // 1ms (default dispersion) -> ~65.5 truncated to 65.
        assert_eq!(secs_to_fixed_16_16(0.001), 65);
    }

    #[test]
    fn server_config_defaults() {
        let cfg = ServerConfig::new(1, *b"GPS.", -20, 0, 65);
        assert_eq!(cfg.stratum, 1);
        assert_eq!(cfg.ref_id, *b"GPS.");
        assert_eq!(cfg.precision, -20);
        assert_ne!(cfg.boot_ref_time, NtpTimestamp::ZERO);
    }

    #[tokio::test]
    async fn loopback_request_response() {
        // Bind two ephemeral sockets and exercise handle_request end to end.
        let server = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let client = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let server_addr = server.local_addr().unwrap();

        let mut req = NtpPacket::client_request(4);
        req.transmit_timestamp = NtpTimestamp {
            seconds: 0x1234_5678,
            fraction: 0x9ABC_DEF0,
        };
        let req_bytes = req.to_bytes();
        client.send_to(&req_bytes, server_addr).await.unwrap();

        let mut buf = [0u8; 512];
        let (len, peer) = tokio::time::timeout(
            std::time::Duration::from_secs(2),
            server.recv_from(&mut buf),
        )
        .await
        .expect("server recv timeout")
        .unwrap();

        let cfg = ServerConfig {
            stratum: 1,
            ref_id: *b"GPS.",
            precision: -20,
            root_delay: 0,
            root_dispersion: 6553,
            boot_ref_time: NtpTimestamp::now(),
        };
        handle_request(&server, &buf[..len], peer, &cfg)
            .await
            .unwrap();

        let mut resp_buf = [0u8; 512];
        let (rlen, _) = tokio::time::timeout(
            std::time::Duration::from_secs(2),
            client.recv_from(&mut resp_buf),
        )
        .await
        .expect("client recv timeout")
        .unwrap();
        assert!(rlen >= NTP_PACKET_SIZE);
        let resp = NtpPacket::from_bytes(&resp_buf[..rlen]).unwrap();
        assert_eq!(resp.mode, ntp::mode::SERVER);
        assert_eq!(resp.version, 4);
        assert_eq!(resp.stratum, 1);
        assert_eq!(resp.origin_timestamp, req.transmit_timestamp);
        assert_ne!(resp.receive_timestamp, NtpTimestamp::ZERO);
        assert_ne!(resp.transmit_timestamp, NtpTimestamp::ZERO);
    }

    #[tokio::test]
    async fn short_datagram_gets_no_reply() {
        let server = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let client = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let server_addr = server.local_addr().unwrap();

        client.send_to(&[0x1Bu8; 10], server_addr).await.unwrap();

        let mut buf = [0u8; 512];
        let (len, peer) = tokio::time::timeout(
            std::time::Duration::from_secs(2),
            server.recv_from(&mut buf),
        )
        .await
        .expect("server recv timeout")
        .unwrap();

        let cfg = ServerConfig::new(1, *b"GPS.", -20, 0, 0);
        handle_request(&server, &buf[..len], peer, &cfg)
            .await
            .unwrap();

        // No reply must arrive.
        let mut resp_buf = [0u8; 512];
        let res = tokio::time::timeout(
            std::time::Duration::from_millis(300),
            client.recv_from(&mut resp_buf),
        )
        .await;
        assert!(res.is_err(), "short datagram must not get a reply");
    }

    #[tokio::test]
    async fn non_client_mode_gets_no_reply() {
        let server = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let client = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let server_addr = server.local_addr().unwrap();

        // Mode 4 (server) packet: must be ignored, not answered.
        let mut pkt = NtpPacket::client_request(4).to_bytes();
        pkt[0] = (0 << 6) | (4 << 3) | ntp::mode::SERVER;
        client.send_to(&pkt, server_addr).await.unwrap();

        let mut buf = [0u8; 512];
        let (len, peer) = tokio::time::timeout(
            std::time::Duration::from_secs(2),
            server.recv_from(&mut buf),
        )
        .await
        .expect("server recv timeout")
        .unwrap();

        let cfg = ServerConfig::new(1, *b"GPS.", -20, 0, 0);
        handle_request(&server, &buf[..len], peer, &cfg)
            .await
            .unwrap();

        let mut resp_buf = [0u8; 512];
        let res = tokio::time::timeout(
            std::time::Duration::from_millis(300),
            client.recv_from(&mut resp_buf),
        )
        .await;
        assert!(res.is_err(), "server-mode packet must not get a reply");
    }

    #[tokio::test]
    async fn v3_request_gets_v3_reply_with_custom_config() {
        let server = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let client = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let server_addr = server.local_addr().unwrap();

        let req_bytes = NtpPacket::client_request(3).to_bytes();
        // Zero the transmit stamp to match the classic 0x1B client exactly.
        let mut raw = req_bytes;
        raw[40..48].copy_from_slice(&[0u8; 8]);
        client.send_to(&raw, server_addr).await.unwrap();

        let mut buf = [0u8; 512];
        let (len, peer) = tokio::time::timeout(
            std::time::Duration::from_secs(2),
            server.recv_from(&mut buf),
        )
        .await
        .expect("server recv timeout")
        .unwrap();

        let cfg = ServerConfig::new(2, *b"ATOM", -10, 65536, 32768);
        handle_request(&server, &buf[..len], peer, &cfg)
            .await
            .unwrap();

        let mut resp_buf = [0u8; 512];
        let (rlen, _) = tokio::time::timeout(
            std::time::Duration::from_secs(2),
            client.recv_from(&mut resp_buf),
        )
        .await
        .expect("client recv timeout")
        .unwrap();
        let resp = NtpPacket::from_bytes(&resp_buf[..rlen]).unwrap();
        assert_eq!(resp.version, 3);
        assert_eq!(resp.mode, ntp::mode::SERVER);
        assert_eq!(resp.stratum, 2);
        assert_eq!(resp.ref_id, *b"ATOM");
        assert_eq!(resp.precision, -10);
        assert_eq!(resp.root_delay, 65536);
        assert_eq!(resp.root_dispersion, 32768);
        assert_eq!(resp.origin_timestamp, NtpTimestamp::ZERO);
    }
}
