//! NTP packet codec per RFC 5905.
//!
//! Wire format (48 bytes minimum):
//! ```text
//!  0: LI(2) | VN(3) | Mode(3)
//!  1: Stratum
//!  2: Poll (signed, log2 seconds)
//!  3: Precision (signed, log2 seconds)
//!  4-7:   Root Delay (unsigned 16.16 fixed point, seconds)
//!  8-11:  Root Dispersion (unsigned 16.16 fixed point, seconds)
//!  12-15: Reference ID
//!  16-23: Reference Timestamp (NTP 64-bit)
//!  24-31: Origin Timestamp
//!  32-39: Receive Timestamp
//!  40-47: Transmit Timestamp
//! ```

use std::time::{SystemTime, UNIX_EPOCH};

pub const NTP_PACKET_SIZE: usize = 48;
/// Seconds between NTP epoch (1900-01-01) and Unix epoch (1970-01-01).
pub const NTP_EPOCH_OFFSET: u64 = 2_208_988_800;
/// Same offset including leap seconds hard to track; kept as u64 const above.

/// NTP modes (RFC 5905, Figure 4).
pub mod mode {
    pub const RESERVED: u8 = 0;
    pub const SYMMETRIC_ACTIVE: u8 = 1;
    pub const SYMMETRIC_PASSIVE: u8 = 2;
    pub const CLIENT: u8 = 3;
    pub const SERVER: u8 = 4;
    pub const BROADCAST: u8 = 5;
}

/// Leap indicator values.
pub mod leap {
    pub const NO_WARNING: u8 = 0;
    pub const ADD_SECOND: u8 = 1;
    pub const DEL_SECOND: u8 = 2;
    pub const ALARM: u8 = 3;
}

/// 64-bit NTP timestamp: 32-bit seconds since 1900 + 32-bit fraction.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct NtpTimestamp {
    pub seconds: u32,
    pub fraction: u32,
}

impl NtpTimestamp {
    pub const ZERO: Self = Self {
        seconds: 0,
        fraction: 0,
    };

    /// Capture the current system time as an NTP timestamp.
    pub fn now() -> Self {
        Self::from_system_time(SystemTime::now())
    }

    pub fn from_system_time(t: SystemTime) -> Self {
        match t.duration_since(UNIX_EPOCH) {
            Ok(d) => {
                let secs = d.as_secs().wrapping_add(NTP_EPOCH_OFFSET);
                // fraction = nanos * 2^32 / 1e9
                let frac = (((d.subsec_nanos() as u64) << 32) / 1_000_000_000u64) as u32;
                Self {
                    seconds: secs as u32,
                    fraction: frac,
                }
            }
            // Pre-1970 times should not happen for a server; saturate to zero.
            Err(_) => Self::ZERO,
        }
    }

    /// Convert back to [`SystemTime`]. Returns `None` on underflow
    /// (dates before 1970, i.e. NTP seconds < offset) or out-of-range.
    pub fn to_system_time(self) -> Option<SystemTime> {
        let secs = (self.seconds as u64).checked_sub(NTP_EPOCH_OFFSET)?;
        let nanos = ((self.fraction as u64) * 1_000_000_000u64) >> 32;
        UNIX_EPOCH.checked_add(std::time::Duration::new(secs, nanos as u32))
    }

    /// NTP timestamp as f64 seconds since 1900 (useful for tests/debug).
    pub fn as_f64(self) -> f64 {
        self.seconds as f64 + self.fraction as f64 / u32::MAX as f64
    }

    pub fn from_be_bytes(b: [u8; 8]) -> Self {
        Self {
            seconds: u32::from_be_bytes([b[0], b[1], b[2], b[3]]),
            fraction: u32::from_be_bytes([b[4], b[5], b[6], b[7]]),
        }
    }

    pub fn to_be_bytes(self) -> [u8; 8] {
        let mut out = [0u8; 8];
        out[0..4].copy_from_slice(&self.seconds.to_be_bytes());
        out[4..8].copy_from_slice(&self.fraction.to_be_bytes());
        out
    }
}

/// Parsed NTP packet (first 48 bytes; extensions after byte 48 are ignored
/// but preserved by the caller if needed).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NtpPacket {
    pub leap: u8,    // 2 bits
    pub version: u8, // 3 bits
    pub mode: u8,    // 3 bits
    pub stratum: u8,
    pub poll: i8,
    pub precision: i8,
    pub root_delay: u32,      // 16.16 fixed point
    pub root_dispersion: u32, // 16.16 fixed point
    pub ref_id: [u8; 4],
    pub ref_timestamp: NtpTimestamp,
    pub origin_timestamp: NtpTimestamp,
    pub receive_timestamp: NtpTimestamp,
    pub transmit_timestamp: NtpTimestamp,
}

#[derive(Debug)]
pub enum ParseError {
    TooShort(usize),
}

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::TooShort(n) => write!(f, "packet too short: {n} bytes (need 48)"),
        }
    }
}

impl std::error::Error for ParseError {}

impl NtpPacket {
    /// Typical client request: `0x1B` = LI 0, VN 3, Mode 3, rest zeroed.
    pub fn client_request(version: u8) -> Self {
        Self {
            leap: leap::NO_WARNING,
            version: version.clamp(1, 4),
            mode: mode::CLIENT,
            stratum: 0,
            poll: 0,
            precision: 0,
            root_delay: 0,
            root_dispersion: 0,
            ref_id: [0; 4],
            ref_timestamp: NtpTimestamp::ZERO,
            origin_timestamp: NtpTimestamp::ZERO,
            receive_timestamp: NtpTimestamp::ZERO,
            transmit_timestamp: NtpTimestamp::now(),
            // NOTE: a well-behaved client stamps transmit time here.
            // For server-side handling we overwrite this from the request bytes.
        }
    }

    pub fn from_bytes(buf: &[u8]) -> Result<Self, ParseError> {
        if buf.len() < NTP_PACKET_SIZE {
            return Err(ParseError::TooShort(buf.len()));
        }
        let b0 = buf[0];
        let get = |o: usize| -> [u8; 8] {
            [
                buf[o],
                buf[o + 1],
                buf[o + 2],
                buf[o + 3],
                buf[o + 4],
                buf[o + 5],
                buf[o + 6],
                buf[o + 7],
            ]
        };
        Ok(Self {
            leap: (b0 >> 6) & 0x03,
            version: (b0 >> 3) & 0x07,
            mode: b0 & 0x07,
            stratum: buf[1],
            poll: buf[2] as i8,
            precision: buf[3] as i8,
            root_delay: u32::from_be_bytes([buf[4], buf[5], buf[6], buf[7]]),
            root_dispersion: u32::from_be_bytes([buf[8], buf[9], buf[10], buf[11]]),
            ref_id: [buf[12], buf[13], buf[14], buf[15]],
            ref_timestamp: NtpTimestamp::from_be_bytes(get(16)),
            origin_timestamp: NtpTimestamp::from_be_bytes(get(24)),
            receive_timestamp: NtpTimestamp::from_be_bytes(get(32)),
            transmit_timestamp: NtpTimestamp::from_be_bytes(get(40)),
        })
    }

    pub fn to_bytes(self) -> [u8; NTP_PACKET_SIZE] {
        let mut out = [0u8; NTP_PACKET_SIZE];
        out[0] = (self.leap << 6) | (self.version << 3) | self.mode;
        out[1] = self.stratum;
        out[2] = self.poll as u8;
        out[3] = self.precision as u8;
        out[4..8].copy_from_slice(&self.root_delay.to_be_bytes());
        out[8..12].copy_from_slice(&self.root_dispersion.to_be_bytes());
        out[12..16].copy_from_slice(&self.ref_id);
        out[16..24].copy_from_slice(&self.ref_timestamp.to_be_bytes());
        out[24..32].copy_from_slice(&self.origin_timestamp.to_be_bytes());
        out[32..40].copy_from_slice(&self.receive_timestamp.to_be_bytes());
        out[40..48].copy_from_slice(&self.transmit_timestamp.to_be_bytes());
        out
    }

    /// Build a server response for `request`.
    ///
    /// Follows RFC 5905 server rules in the common case:
    /// - `origin` = request's `transmit`
    /// - `receive` = time the request arrived
    /// - `transmit` = time the response leaves
    /// - mode = SERVER, version echoed back (capped at 4)
    pub fn build_response(
        request: &Self,
        recv_time: NtpTimestamp,
        tx_time: NtpTimestamp,
        stratum: u8,
        ref_id: [u8; 4],
        ref_time: NtpTimestamp,
        precision: i8,
        root_delay: u32,
        root_dispersion: u32,
    ) -> Self {
        Self {
            leap: leap::NO_WARNING,
            version: request.version.clamp(1, 4).min(4),
            mode: mode::SERVER,
            stratum,
            poll: request.poll,
            precision,
            root_delay,
            root_dispersion,
            ref_id,
            ref_timestamp: ref_time,
            origin_timestamp: request.transmit_timestamp,
            receive_timestamp: recv_time,
            transmit_timestamp: tx_time,
        }
    }

    /// Whether this packet is a client request we should answer.
    pub fn is_client_request(&self) -> bool {
        self.mode == mode::CLIENT && (2..=4).contains(&self.version)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip_client_request_byte0() {
        // Matches ntp-client/client.ts: first byte 0x1B, rest zero... except
        // our constructor stamps transmit time like a real client.
        let mut pkt = NtpPacket::client_request(3);
        pkt.transmit_timestamp = NtpTimestamp::ZERO;
        let bytes = pkt.to_bytes();
        assert_eq!(bytes[0], 0x1B);
        assert_eq!(&bytes[1..48], &[0u8; 47]);
    }

    #[test]
    fn parse_known_client_request() {
        let mut buf = [0u8; 48];
        buf[0] = 0x1B;
        let p = NtpPacket::from_bytes(&buf).unwrap();
        assert_eq!(p.leap, 0);
        assert_eq!(p.version, 3);
        assert_eq!(p.mode, mode::CLIENT);
        assert!(p.is_client_request());
    }

    #[test]
    fn reject_short_packet() {
        assert!(NtpPacket::from_bytes(&[0u8; 47]).is_err());
        assert!(NtpPacket::from_bytes(&[]).is_err());
        let err = NtpPacket::from_bytes(&[0u8; 10]).unwrap_err();
        assert!(err.to_string().contains("48"));
    }

    #[test]
    fn extension_bytes_are_ignored() {
        // Packets longer than 48 bytes (RFC 5905 extension fields) parse
        // using only the first 48 bytes.
        let mut buf = [0xAAu8; 68];
        buf[0] = 0x1B;
        let p = NtpPacket::from_bytes(&buf).unwrap();
        assert_eq!((p.leap, p.version, p.mode), (0, 3, mode::CLIENT));
        assert_eq!(p.transmit_timestamp.seconds, 0xAAAA_AAAA);
    }

    #[test]
    fn client_request_version_is_clamped() {
        assert_eq!(NtpPacket::client_request(0).version, 1);
        assert_eq!(NtpPacket::client_request(1).version, 1);
        assert_eq!(NtpPacket::client_request(4).version, 4);
        assert_eq!(NtpPacket::client_request(9).version, 4);
    }

    #[test]
    fn is_client_request_matrix() {
        // Only mode CLIENT (3) with version 2..=4 is answered.
        for version in 0..8u8 {
            for m in [
                mode::RESERVED,
                mode::SYMMETRIC_ACTIVE,
                mode::SYMMETRIC_PASSIVE,
            ] {
                let p = NtpPacket {
                    version,
                    mode: m,
                    ..NtpPacket::client_request(3)
                };
                assert!(!p.is_client_request(), "vn={version} mode={m}");
            }
            let server = NtpPacket {
                version,
                mode: mode::SERVER,
                ..NtpPacket::client_request(3)
            };
            assert!(!server.is_client_request(), "vn={version} mode=server");
            let bcast = NtpPacket {
                version,
                mode: mode::BROADCAST,
                ..NtpPacket::client_request(3)
            };
            assert!(!bcast.is_client_request(), "vn={version} mode=broadcast");

            let client = NtpPacket {
                version,
                mode: mode::CLIENT,
                ..NtpPacket::client_request(3)
            };
            assert_eq!(
                client.is_client_request(),
                (2..=4).contains(&version),
                "vn={version} mode=client"
            );
        }
    }

    #[test]
    fn first_byte_bit_packing_roundtrip() {
        for leap in 0..4u8 {
            for version in 0..8u8 {
                for m in 0..8u8 {
                    let p = NtpPacket {
                        leap,
                        version,
                        mode: m,
                        ..NtpPacket::client_request(3)
                    };
                    let rt = NtpPacket::from_bytes(&p.to_bytes()).unwrap();
                    assert_eq!((rt.leap, rt.version, rt.mode), (leap, version, m));
                }
            }
        }
    }

    #[test]
    fn signed_fields_roundtrip() {
        // poll/precision are signed; check negative extremes survive.
        let p = NtpPacket {
            poll: -6,
            precision: -127,
            ..NtpPacket::client_request(4)
        };
        let rt = NtpPacket::from_bytes(&p.to_bytes()).unwrap();
        assert_eq!(rt.poll, -6);
        assert_eq!(rt.precision, -127);
    }

    #[test]
    fn response_echoes_version_capped_at_4() {
        for (req_vn, resp_vn) in [(1, 1), (2, 2), (3, 3), (4, 4)] {
            let req = NtpPacket::client_request(req_vn);
            let resp = NtpPacket::build_response(
                &req,
                NtpTimestamp::ZERO,
                NtpTimestamp::ZERO,
                1,
                *b"GPS.",
                NtpTimestamp::ZERO,
                -20,
                0,
                0,
            );
            assert_eq!(resp.version, resp_vn);
        }
        // Out-of-range versions parsed from the wire are clamped, never echoed raw.
        let mut odd = NtpPacket::client_request(3);
        odd.version = 7;
        let resp = NtpPacket::build_response(
            &odd,
            NtpTimestamp::ZERO,
            NtpTimestamp::ZERO,
            1,
            *b"GPS.",
            NtpTimestamp::ZERO,
            -20,
            0,
            0,
        );
        assert_eq!(resp.version, 4);
    }

    #[test]
    fn response_passes_through_poll_and_config() {
        let mut req = NtpPacket::client_request(3);
        req.poll = 9;
        let ref_time = NtpTimestamp {
            seconds: 42,
            fraction: 7,
        };
        let resp = NtpPacket::build_response(
            &req,
            NtpTimestamp::ZERO,
            NtpTimestamp::ZERO,
            2,
            *b"GOES",
            ref_time,
            -12,
            111,
            222,
        );
        assert_eq!(resp.poll, 9);
        assert_eq!(resp.leap, leap::NO_WARNING);
        assert_eq!(resp.ref_id, *b"GOES");
        assert_eq!(resp.ref_timestamp, ref_time);
        assert_eq!(resp.root_delay, 111);
        assert_eq!(resp.root_dispersion, 222);
        assert_eq!(resp.precision, -12);
    }

    #[test]
    fn response_copies_origin_and_stamps_times() {
        let mut req = NtpPacket::client_request(4);
        req.transmit_timestamp = NtpTimestamp {
            seconds: 0xDEAD_BEEF,
            fraction: 123,
        };
        let recv = NtpTimestamp {
            seconds: 1,
            fraction: 2,
        };
        let tx = NtpTimestamp {
            seconds: 3,
            fraction: 4,
        };
        let resp =
            NtpPacket::build_response(&req, recv, tx, 1, *b"GPS.", NtpTimestamp::ZERO, -20, 0, 0);
        assert_eq!(resp.mode, mode::SERVER);
        assert_eq!(resp.version, 4);
        assert_eq!(resp.origin_timestamp, req.transmit_timestamp);
        assert_eq!(resp.receive_timestamp, recv);
        assert_eq!(resp.transmit_timestamp, tx);
        assert_eq!(resp.stratum, 1);
    }

    #[test]
    fn timestamp_unix_epoch_is_ntp_offset() {
        // 1970-01-01T00:00:00Z == 2_208_988_800 NTP seconds, zero fraction.
        let ts = NtpTimestamp::from_system_time(UNIX_EPOCH);
        assert_eq!(
            ts,
            NtpTimestamp {
                seconds: NTP_EPOCH_OFFSET as u32,
                fraction: 0
            }
        );
        assert_eq!(ts.to_system_time().unwrap(), UNIX_EPOCH);
    }

    #[test]
    fn timestamp_pre_epoch_saturates_to_zero() {
        let before = UNIX_EPOCH - std::time::Duration::from_secs(1);
        assert_eq!(NtpTimestamp::from_system_time(before), NtpTimestamp::ZERO);
    }

    #[test]
    fn timestamp_to_system_time_underflow_is_none() {
        // NTP seconds below the epoch offset predate 1970: unrepresentable.
        let ts = NtpTimestamp {
            seconds: (NTP_EPOCH_OFFSET - 1) as u32,
            fraction: 0,
        };
        assert_eq!(ts.to_system_time(), None);
    }

    #[test]
    fn timestamp_known_fraction() {
        // 0.5s -> fraction 0x8000_0000.
        let t = UNIX_EPOCH + std::time::Duration::new(0, 500_000_000);
        let ts = NtpTimestamp::from_system_time(t);
        assert_eq!(ts.seconds, NTP_EPOCH_OFFSET as u32);
        assert_eq!(ts.fraction, 0x8000_0000);
    }

    #[test]
    fn timestamp_be_bytes_roundtrip() {
        let ts = NtpTimestamp {
            seconds: 0xDEAD_BEEF,
            fraction: 0x0102_0304,
        };
        assert_eq!(NtpTimestamp::from_be_bytes(ts.to_be_bytes()), ts);
    }

    #[test]
    fn timestamp_as_f64_sanity() {
        let ts = NtpTimestamp {
            seconds: 10,
            fraction: 0,
        };
        assert!((ts.as_f64() - 10.0).abs() < f64::EPSILON);
        assert!(NtpTimestamp::ZERO.as_f64() == 0.0);
    }

    #[test]
    fn timestamp_systemtime_roundtrip() {
        let now = SystemTime::now();
        let ts = NtpTimestamp::from_system_time(now);
        let back = ts.to_system_time().expect("must round-trip");
        let delta = back
            .duration_since(now)
            .unwrap_or_else(|e| e.duration())
            .as_millis();
        // 32-bit fraction resolution is ~0.23ns, but nanos->fraction
        // truncation costs < 1ms.
        assert!(delta <= 1, "delta {delta}ms too large");
    }

    #[test]
    fn bytes_roundtrip() {
        let p = NtpPacket {
            leap: 0,
            version: 4,
            mode: mode::SERVER,
            stratum: 1,
            poll: 6,
            precision: -20,
            root_delay: 0,
            root_dispersion: 0x0001_0000 >> 4,
            ref_id: *b"GPS.",
            ref_timestamp: NtpTimestamp {
                seconds: 100,
                fraction: 200,
            },
            origin_timestamp: NtpTimestamp {
                seconds: 1,
                fraction: 1,
            },
            receive_timestamp: NtpTimestamp {
                seconds: 2,
                fraction: 2,
            },
            transmit_timestamp: NtpTimestamp {
                seconds: 3,
                fraction: 3,
            },
        };
        let rt = NtpPacket::from_bytes(&p.to_bytes()).unwrap();
        assert_eq!(p, rt);
    }
}
