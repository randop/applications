//! Integration tests: spawn the real `ntp-server` binary and speak NTP to it
//! over UDP, like `../ntp-client/client.ts` does.
//!
//! Each test boots its own server on an ephemeral loopback port, so tests
//! are isolated and can run in parallel.

use std::net::UdpSocket;
use std::process::{Child, Command};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use ntp_server::ntp::{mode, NtpPacket, NtpTimestamp, NTP_EPOCH_OFFSET, NTP_PACKET_SIZE};

/// A running server instance; killed on drop.
struct TestServer {
    child: Option<Child>,
    port: u16,
}

impl TestServer {
    fn start(extra_args: &[&str]) -> Self {
        // Retry loop guards against the (tiny) race of two tests grabbing
        // the same released ephemeral port.
        for _ in 0..3 {
            let port = free_port();
            let bin = env!("CARGO_BIN_EXE_ntp-server");
            let mut cmd = Command::new(bin);
            cmd.arg("--bind")
                .arg("127.0.0.1")
                .arg("--port")
                .arg(port.to_string());
            for a in extra_args {
                cmd.arg(a);
            }
            cmd.stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null());
            let mut child = cmd.spawn().expect("failed to spawn ntp-server");
            if wait_ready(port, &mut child) {
                return Self {
                    child: Some(child),
                    port,
                };
            }
            let _ = child.kill();
            let _ = child.wait();
        }
        panic!("ntp-server did not become ready");
    }
}

impl Drop for TestServer {
    fn drop(&mut self) {
        if let Some(mut child) = self.child.take() {
            let _ = child.kill();
            let _ = child.wait();
        }
    }
}

fn free_port() -> u16 {
    UdpSocket::bind("127.0.0.1:0")
        .unwrap()
        .local_addr()
        .unwrap()
        .port()
}

/// Probe until the server answers a client request (or the child exits).
fn wait_ready(port: u16, child: &mut Child) -> bool {
    let deadline = SystemTime::now() + Duration::from_secs(10);
    let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
    sock.set_read_timeout(Some(Duration::from_millis(200)))
        .unwrap();
    let mut probe = [0u8; NTP_PACKET_SIZE];
    probe[0] = 0x1B;
    while SystemTime::now() < deadline {
        if child.try_wait().unwrap().is_some() {
            return false; // exited (e.g. port clash) — caller retries
        }
        let _ = sock.send_to(&probe, ("127.0.0.1", port));
        let mut buf = [0u8; 512];
        if let Ok((n, _)) = sock.recv_from(&mut buf) {
            if n >= NTP_PACKET_SIZE && buf[0] & 0x07 == mode::SERVER {
                return true;
            }
        }
    }
    false
}

/// Build a client request with a distinctive transmit stamp.
fn client_request(version: u8, poll: i8) -> [u8; NTP_PACKET_SIZE] {
    let mut pkt = NtpPacket::client_request(version);
    pkt.poll = poll;
    pkt.transmit_timestamp = NtpTimestamp {
        seconds: 0x1234_5678,
        fraction: 0x9ABC_DEF0,
    };
    pkt.to_bytes()
}

fn query(port: u16, req: &[u8], timeout: Duration) -> Option<Vec<u8>> {
    let sock = UdpSocket::bind("127.0.0.1:0").ok()?;
    sock.set_read_timeout(Some(timeout)).ok()?;
    sock.send_to(req, ("127.0.0.1", port)).ok()?;
    let mut buf = [0u8; 512];
    let (n, _) = sock.recv_from(&mut buf).ok()?;
    Some(buf[..n].to_vec())
}

/// Local time as f64 NTP seconds (for sanity-checking server timestamps).
fn now_ntp() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs_f64()
        + NTP_EPOCH_OFFSET as f64
}

#[test]
fn v3_client_gets_server_response() {
    let srv = TestServer::start(&[]);
    let raw = query(srv.port, &client_request(3, 0), Duration::from_secs(2))
        .expect("no reply to v3 client request");
    assert_eq!(raw.len(), NTP_PACKET_SIZE);

    let resp = NtpPacket::from_bytes(&raw).unwrap();
    assert_eq!(resp.leap, 0);
    assert_eq!(resp.version, 3);
    assert_eq!(resp.mode, mode::SERVER);
    assert_eq!(resp.stratum, 1);
    assert_eq!(resp.ref_id, *b"GPS.");
    // Origin must echo the client's transmit stamp.
    assert_eq!(
        resp.origin_timestamp,
        NtpTimestamp {
            seconds: 0x1234_5678,
            fraction: 0x9ABC_DEF0
        }
    );
    // Server clock must be sane (within 10s of ours).
    let tx = resp.transmit_timestamp.as_f64();
    assert!(
        (tx - now_ntp()).abs() < 10.0,
        "server time {tx} far from local clock"
    );
}

#[test]
fn v4_client_gets_v4_response() {
    let srv = TestServer::start(&[]);
    let raw = query(srv.port, &client_request(4, 6), Duration::from_secs(2))
        .expect("no reply to v4 client request");
    let resp = NtpPacket::from_bytes(&raw).unwrap();
    assert_eq!(resp.version, 4);
    assert_eq!(resp.mode, mode::SERVER);
    assert_eq!(resp.poll, 6, "poll must be echoed");
}

#[test]
fn timestamps_are_ordered_and_nonzero() {
    let srv = TestServer::start(&[]);
    let raw = query(srv.port, &client_request(4, 0), Duration::from_secs(2)).expect("no reply");
    let resp = NtpPacket::from_bytes(&raw).unwrap();
    assert_ne!(resp.receive_timestamp, NtpTimestamp::ZERO);
    assert_ne!(resp.transmit_timestamp, NtpTimestamp::ZERO);
    assert_ne!(resp.ref_timestamp, NtpTimestamp::ZERO);
    assert!(
        resp.transmit_timestamp.as_f64() >= resp.receive_timestamp.as_f64(),
        "transmit must not precede receive"
    );
}

#[test]
fn short_datagram_gets_no_reply() {
    let srv = TestServer::start(&[]);
    let res = query(srv.port, &[0x1Bu8; 10], Duration::from_millis(500));
    assert!(res.is_none(), "short datagram must be dropped, got {res:?}");
}

#[test]
fn empty_datagram_gets_no_reply() {
    let srv = TestServer::start(&[]);
    let res = query(srv.port, &[], Duration::from_millis(500));
    assert!(res.is_none(), "empty datagram must be dropped");
}

#[test]
fn non_client_modes_get_no_reply() {
    let srv = TestServer::start(&[]);
    for m in [
        mode::RESERVED,
        mode::SYMMETRIC_ACTIVE,
        mode::SYMMETRIC_PASSIVE,
        mode::SERVER,
        mode::BROADCAST,
    ] {
        let mut req = client_request(4, 0);
        req[0] = (0 << 6) | (4 << 3) | m;
        let res = query(srv.port, &req, Duration::from_millis(500));
        assert!(res.is_none(), "mode {m} must not be answered");
    }
}

#[test]
fn unsupported_version_gets_no_reply() {
    let srv = TestServer::start(&[]);
    let mut req = client_request(4, 0);
    req[0] = (0 << 6) | (1 << 3) | mode::CLIENT; // VN 1
    let res = query(srv.port, &req, Duration::from_millis(500));
    assert!(res.is_none(), "version 1 must not be answered");
}

#[test]
fn custom_flags_are_advertised() {
    let srv = TestServer::start(&[
        "--stratum",
        "2",
        "--ref-id",
        "ATOM",
        "--precision",
        "-10",
        "--root-delay-ms",
        "1000",
        "--root-dispersion-ms",
        "500",
    ]);
    let raw = query(srv.port, &client_request(4, 0), Duration::from_secs(2)).expect("no reply");
    let resp = NtpPacket::from_bytes(&raw).unwrap();
    assert_eq!(resp.stratum, 2);
    assert_eq!(resp.ref_id, *b"ATOM");
    assert_eq!(resp.precision, -10);
    assert_eq!(resp.root_delay, 65536, "1000ms -> 1.0 fixed 16.16");
    assert_eq!(resp.root_dispersion, 32768, "500ms -> 0.5 fixed 16.16");
}

#[test]
fn concurrent_clients_all_get_replies() {
    let srv = TestServer::start(&[]);
    let port = srv.port;
    let mut handles = Vec::new();
    for i in 0..20u8 {
        handles.push(std::thread::spawn(move || {
            let mut req = client_request(4, i as i8);
            // Distinct transmit stamp per client to verify origin echo.
            req[44..48].copy_from_slice(&(i as u32 * 0x0101_0101).to_be_bytes());
            let raw = query(port, &req, Duration::from_secs(5))
                .unwrap_or_else(|| panic!("client {i} got no reply"));
            let resp = NtpPacket::from_bytes(&raw).unwrap();
            assert_eq!(resp.mode, mode::SERVER);
            assert_eq!(&resp.origin_timestamp.to_be_bytes()[4..8], &req[44..48]);
        }));
    }
    for h in handles {
        h.join().unwrap();
    }
}
