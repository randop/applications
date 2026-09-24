use anyhow::{Result, bail};

#[allow(dead_code)]
#[derive(Debug)]
pub enum Command {
    Capability(String),
    Noop(String),
    Logout(String),
    StartTls(String),
    Authenticate {
        tag: String,
        mechanism: String,
        initial: Option<String>,
    },
    Select {
        tag: String,
        mailbox: String,
        readonly: bool,
    },
    Fetch {
        tag: String,
        sequence: String,
        items: String,
        uid: bool,
    },
    Store {
        tag: String,
        sequence: String,
        mode: String,
        flags: String,
        uid: bool,
    },
    Search {
        tag: String,
        criteria: String,
        uid: bool,
    },
    Expunge {
        tag: String,
        uid: bool,
    },
    Append {
        tag: String,
        mailbox: String,
        flags: String,
        literal: Vec<u8>,
    },
}

pub fn parse(line: &str) -> Result<(String, String, Vec<String>)> {
    let mut p = line.split_whitespace();
    let tag = p
        .next()
        .ok_or_else(|| anyhow::anyhow!("missing tag"))?
        .to_string();
    let command = p
        .next()
        .ok_or_else(|| anyhow::anyhow!("missing command"))?
        .to_ascii_uppercase();
    Ok((tag, command, p.map(str::to_string).collect()))
}

pub fn auth_initial(args: &[String]) -> Result<Option<String>> {
    if args.len() > 1 {
        bail!("too many AUTHENTICATE arguments")
    }
    Ok(args.first().cloned())
}
