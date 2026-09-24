use std::sync::Arc;

use clap::Parser;
use tokio::net::UdpSocket;

use ntp_server::ntp::NtpTimestamp;
use ntp_server::server::{self, ServerConfig};

/// Minimal async NTP (RFC 5905) UDP server.
///
/// Listens for client-mode (mode 3) requests and replies in server mode
/// (mode 4), stamping receive/transmit times from the local system clock.
/// Stratum-1 by default; NOT disciplined to an upstream source — the local
/// clock is the reference. For lab/testing use, or put a real refclock
/// behind `--ref-id` semantics yourself.
#[derive(Parser, Debug, Clone)]
#[command(name = "ntp-server", version, about)]
struct Args {
    /// Address to bind (v4 or v6). Use 0.0.0.0 for all IPv4 interfaces.
    #[arg(long, default_value = "0.0.0.0")]
    bind: String,

    /// UDP port to listen on. Standard NTP port is 123 (needs root/cap).
    #[arg(long, default_value_t = 123)]
    port: u16,

    /// Stratum to advertise (1 = primary reference, e.g. GPS).
    #[arg(long, default_value_t = 1, value_parser = clap::value_parser!(u8).range(1..=15))]
    stratum: u8,

    /// Reference identifier, exactly 4 ASCII chars (e.g. "GPS.", "ATOM", "GOES").
    #[arg(long, default_value = "GPS.")]
    ref_id: String,

    /// Precision (log2 seconds, e.g. -20 ≈ 1µs) to advertise.
    /// Space-separated negatives need explicit opt-in in clap.
    #[arg(long, default_value_t = -20, allow_hyphen_values = true)]
    precision: i8,

    /// Root delay to advertise, in milliseconds.
    #[arg(long, default_value_t = 0.0)]
    root_delay_ms: f64,

    /// Root dispersion to advertise, in milliseconds.
    #[arg(long, default_value_t = 1.0)]
    root_dispersion_ms: f64,
}

#[tokio::main]
async fn main() -> Result<(), server::AnyError> {
    let args = Args::parse();

    let cfg = ServerConfig {
        stratum: args.stratum,
        ref_id: server::parse_ref_id(&args.ref_id)?,
        precision: args.precision,
        root_delay: server::secs_to_fixed_16_16(args.root_delay_ms / 1000.0),
        root_dispersion: server::secs_to_fixed_16_16(args.root_dispersion_ms / 1000.0),
        boot_ref_time: NtpTimestamp::now(),
    };

    let bind_addr = format!("{}:{}", args.bind, args.port);
    let socket = match UdpSocket::bind(&bind_addr).await {
        Ok(s) => s,
        Err(e) => {
            server::log(
                "{bind}",
                &format!("failed to bind {bind_addr}: {e} (hint: ports <1024 need root; try --port 1123)"),
            );
            std::process::exit(1);
        }
    };
    let socket = Arc::new(socket);
    server::log(
        "{bind}",
        &format!(
            "ntp-server listening on {bind_addr} stratum={} ref_id={:?} precision={}",
            cfg.stratum,
            String::from_utf8_lossy(&cfg.ref_id),
            cfg.precision
        ),
    );

    let mut buf = vec![0u8; 512]; // allow extension fields; we use first 48
    loop {
        tokio::select! {
            recvd = socket.recv_from(&mut buf) => {
                let (len, peer) = match recvd {
                    Ok(v) => v,
                    Err(e) => {
                        server::log("{recv}", &format!("[ERROR] recv: {e}"));
                        continue;
                    }
                };
                let sock = Arc::clone(&socket);
                // Copy out the datagram; handle concurrently so one slow
                // peer cannot stall the server.
                let datagram = buf[..len].to_vec();
                tokio::spawn(async move {
                    if let Err(e) = server::handle_request(&sock, &datagram, peer, &cfg).await {
                        server::log("{req}", &format!("{peer} [ERROR] {e}"));
                    }
                });
            }
            _ = tokio::signal::ctrl_c() => {
                server::log("{shutdown}", "received Ctrl-C, exiting");
                break;
            }
        }
    }
    Ok(())
}
