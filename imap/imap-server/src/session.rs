use crate::{config::Config, oauth::SharedAuthenticator, protocol, store::DirectoryStore};
use anyhow::{Context, Result, bail};
use base64::{Engine, engine::general_purpose::STANDARD};
use std::{net::SocketAddr, sync::Arc};
use tokio::{
    io::{AsyncBufReadExt, AsyncWriteExt, BufReader},
    time::{Duration, timeout},
};
use tokio_rustls::server::TlsStream;

pub struct Session;

impl Session {
    pub async fn run(
        stream: TlsStream<tokio::net::TcpStream>,
        peer: SocketAddr,
        store: Arc<DirectoryStore>,
        auth: SharedAuthenticator,
        cfg: Arc<Config>,
    ) -> Result<()> {
        let mut io = BufReader::new(stream);
        io.get_mut()
            .write_all(b"* OK [CAPABILITY IMAP4rev2 AUTH=OAUTHBEARER] ready\r\n")
            .await?;

        let mut authenticated = false;
        let mut selected = false;
        let mut line = String::new();

        loop {
            line.clear();
            let n = timeout(Duration::from_secs(300), io.read_line(&mut line)).await??;
            if n == 0 {
                return Ok(());
            }
            if line.len() > cfg.limits.max_command_bytes {
                bail!("command too large")
            }

            let line = line.trim_end_matches(['\r', '\n']);
            let (tag, command, args) = protocol::parse(line)?;

            match command.as_str() {
                "CAPABILITY" => {
                    io.get_mut().write_all(format!("* CAPABILITY IMAP4rev2 AUTH=OAUTHBEARER\r\n{tag} OK CAPABILITY completed\r\n").as_bytes()).await?;
                }
                "NOOP" => {
                    io.get_mut()
                        .write_all(format!("{tag} OK NOOP completed\r\n").as_bytes())
                        .await?;
                }
                "LOGOUT" => {
                    io.get_mut()
                        .write_all(
                            format!("* BYE logging out\r\n{tag} OK LOGOUT completed\r\n")
                                .as_bytes(),
                        )
                        .await?;
                    return Ok(());
                }
                "AUTHENTICATE" => {
                    if args.first().map(|x| x.to_ascii_uppercase()) != Some("OAUTHBEARER".into()) {
                        io.get_mut()
                            .write_all(
                                format!("{tag} NO only OAUTHBEARER is supported\r\n").as_bytes(),
                            )
                            .await?;
                        continue;
                    }
                    let initial = protocol::auth_initial(&args[1..])?;
                    let payload = match initial {
                        Some(v) => v,
                        None => {
                            io.get_mut().write_all(b"+ \r\n").await?;
                            let mut response = String::new();
                            io.read_line(&mut response).await?;
                            response.trim().to_string()
                        }
                    };
                    let decoded = STANDARD
                        .decode(payload.as_bytes())
                        .context("decode OAUTHBEARER response")?;
                    let text =
                        String::from_utf8(decoded).context("OAUTHBEARER response is not UTF-8")?;
                    let token = text
                        .split('\x01')
                        .find_map(|x| x.strip_prefix("auth=Bearer "))
                        .context("missing OAUTHBEARER bearer token")?;
                    match auth.validate(token).await {
                        Ok(identity) => {
                            authenticated = true;
                            io.get_mut()
                                .write_all(
                                    format!("{tag} OK [AUTHENTICATED] {}\r\n", identity.subject)
                                        .as_bytes(),
                                )
                                .await?;
                        }
                        Err(e) => {
                            io.get_mut()
                                .write_all(format!("{tag} NO authentication failed\r\n").as_bytes())
                                .await?;
                            tracing::warn!(%peer, error=%e, "OAuth authentication failed");
                        }
                    }
                }
                "SELECT" | "EXAMINE" => {
                    if !authenticated {
                        io.get_mut()
                            .write_all(format!("{tag} NO authenticate first\r\n").as_bytes())
                            .await?;
                        continue;
                    }
                    let mailbox = args.first().map(String::as_str).unwrap_or("");
                    if !mailbox.eq_ignore_ascii_case("INBOX") {
                        io.get_mut()
                            .write_all(format!("{tag} NO only INBOX exists\r\n").as_bytes())
                            .await?;
                        continue;
                    }
                    let count = store.messages()?.len();
                    let uidv = store.uidvalidity()?;
                    selected = true;
                    io.get_mut().write_all(format!(
                        "* FLAGS (\\Seen \\Answered \\Flagged \\Deleted \\Draft)\r\n* {count} EXISTS\r\n* OK [UIDVALIDITY {uidv}]\r\n* OK [UIDNEXT {}]\r\n{tag} OK [READ-WRITE] SELECT completed\r\n",
                        count + 1
                    ).as_bytes()).await?;
                }
                "FETCH" | "UID" => {
                    if !authenticated || !selected {
                        io.get_mut()
                            .write_all(format!("{tag} BAD select INBOX first\r\n").as_bytes())
                            .await?;
                        continue;
                    }
                    io.get_mut()
                        .write_all(format!("{tag} OK command accepted\r\n").as_bytes())
                        .await?;
                }
                "STORE" | "SEARCH" | "EXPUNGE" | "COPY" | "CREATE" | "DELETE" | "RENAME" => {
                    if !authenticated || !selected {
                        io.get_mut()
                            .write_all(format!("{tag} BAD select INBOX first\r\n").as_bytes())
                            .await?;
                    } else {
                        io.get_mut().write_all(format!("{tag} NO command is reserved for the complete command implementation\r\n").as_bytes()).await?;
                    }
                }
                "APPEND" => {
                    if !authenticated {
                        io.get_mut()
                            .write_all(format!("{tag} NO authenticate first\r\n").as_bytes())
                            .await?;
                        continue;
                    }
                    io.get_mut()
                        .write_all(
                            format!(
                                "{tag} NO APPEND literal framing requires the literal parser\r\n"
                            )
                            .as_bytes(),
                        )
                        .await?;
                }
                "LOGIN" => {
                    io.get_mut()
                        .write_all(
                            format!("{tag} NO password authentication is disabled\r\n").as_bytes(),
                        )
                        .await?;
                }
                "NAMESPACE" => {
                    io.get_mut()
                        .write_all(
                            format!("* NAMESPACE ((\"\" \"/\")) NIL NIL\r\n{tag} OK NAMESPACE completed\r\n")
                        .as_bytes(),)
                        .await?;
                }
                "ENABLE" => {
                    io.get_mut()
                        .write_all(format!("{tag} OK ENABLE completed\r\n").as_bytes())
                        .await?;
                }
                "LIST" => {
                    // Ignore args for now; always advertise INBOX
                    io.get_mut()
                        .write_all(
                            format!("* LIST (\\HasNoChildren) \"/\" INBOX\r\n{tag} OK LIST completed\r\n")
                        .as_bytes(),
                    )
                    .await?;
                }
                _ => {
                    io.get_mut()
                        .write_all(format!("{tag} BAD unsupported command\r\n").as_bytes())
                        .await?;
                }
            }
        }
    }
}
