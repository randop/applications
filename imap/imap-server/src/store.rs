use anyhow::{Context, Result};
use chrono::{DateTime, Utc};
use rusqlite::{Connection, OptionalExtension, params};
use std::{
    fs,
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
};
use uuid::Uuid;

#[derive(Debug, Clone)]
pub struct Message {
    #[allow(dead_code)]
    pub uuid: Uuid,
    #[allow(dead_code)]
    pub uid: u32,
    #[allow(dead_code)]
    pub flags: String,
    #[allow(dead_code)]
    pub internal_date: DateTime<Utc>,
    #[allow(dead_code)]
    pub size: u64,
    #[allow(dead_code)]
    pub path: PathBuf,
}

pub struct DirectoryStore {
    root: PathBuf,
    db: Mutex<Connection>,
}

/// Derive an IMAP-legal UIDVALIDITY (nz-number, 1..=u32::MAX) from a UUIDv7.
/// UUIDv7 stores a Unix-ms timestamp in the high bits; we use the first 4 bytes.
fn uidvalidity_from_uuid_v7(id: Uuid) -> u32 {
    let b = id.as_bytes();
    let v = u32::from_be_bytes([b[0], b[1], b[2], b[3]]);
    if v == 0 { 1 } else { v }
}

impl DirectoryStore {
    pub fn open(root: &Path) -> Result<Self> {
        fs::create_dir_all(root)?;
        let meta = root.join(".imap");
        fs::create_dir_all(&meta)?;
        let db = Connection::open(meta.join("metadata.sqlite"))?;
        db.execute_batch(
            "PRAGMA journal_mode=WAL;
             CREATE TABLE IF NOT EXISTS mailbox(
               id INTEGER PRIMARY KEY CHECK(id=1),
               uidvalidity INTEGER NOT NULL,
               uidnext INTEGER NOT NULL
             );
             CREATE TABLE IF NOT EXISTS messages(
               uuid TEXT PRIMARY KEY,
               uid INTEGER NOT NULL UNIQUE,
               flags TEXT NOT NULL DEFAULT '',
               internal_date TEXT NOT NULL
             );",
        )?;

        // Seed mailbox row once with a UUIDv7-derived UIDVALIDITY (fits in u32).
        let has_mailbox: bool =
            db.query_row("SELECT EXISTS(SELECT 1 FROM mailbox WHERE id=1)", [], |r| {
                r.get(0)
            })?;
        if !has_mailbox {
            let uidvalidity = uidvalidity_from_uuid_v7(Uuid::now_v7());
            db.execute(
                "INSERT INTO mailbox(id, uidvalidity, uidnext) VALUES(1, ?1, 1)",
                params![uidvalidity],
            )?;
        } else {
            // Repair legacy rows that used abs(random()) and may exceed u32.
            let raw: i64 = db.query_row("SELECT uidvalidity FROM mailbox WHERE id=1", [], |r| {
                r.get(0)
            })?;
            if raw <= 0 || raw > u32::MAX as i64 {
                let uidvalidity = uidvalidity_from_uuid_v7(Uuid::now_v7());
                db.execute(
                    "UPDATE mailbox SET uidvalidity=?1 WHERE id=1",
                    params![uidvalidity],
                )?;
            }
        }

        let s = Self {
            root: root.to_path_buf(),
            db: Mutex::new(db),
        };
        s.reconcile()?;
        Ok(s)
    }

    pub(crate) fn reconcile(&self) -> Result<()> {
        let db = self.db.lock().unwrap();
        for entry in fs::read_dir(&self.root)? {
            let entry = entry?;
            let path = entry.path();
            if path.extension().and_then(|x| x.to_str()) != Some("eml") {
                continue;
            }
            let Some(stem) = path.file_stem().and_then(|x| x.to_str()) else {
                continue;
            };
            let Ok(uuid) = Uuid::parse_str(stem) else {
                continue;
            };

            let exists: bool = db.query_row(
                "SELECT EXISTS(SELECT 1 FROM messages WHERE uuid=?1)",
                params![uuid.to_string()],
                |r| r.get(0),
            )?;
            if !exists {
                let uid: u32 =
                    db.query_row("SELECT uidnext FROM mailbox WHERE id=1", [], |r| r.get(0))?;
                db.execute(
                    "INSERT INTO messages(uuid,uid,internal_date) VALUES(?1,?2,?3)",
                    params![uuid.to_string(), uid, Utc::now().to_rfc3339()],
                )?;
                db.execute("UPDATE mailbox SET uidnext=uidnext+1 WHERE id=1", [])?;
            }
        }
        Ok(())
    }

    #[allow(dead_code)]
    pub fn start_watcher(self: &Arc<Self>) -> Result<crate::watcher::DirectoryWatcher> {
        crate::watcher::DirectoryWatcher::start(self.clone(), self.root.clone())
    }

    pub fn uidvalidity(&self) -> Result<u32> {
        let raw: i64 = self.db.lock().unwrap().query_row(
            "SELECT uidvalidity FROM mailbox WHERE id=1",
            [],
            |r| r.get(0),
        )?;
        u32::try_from(raw).context("uidvalidity out of u32 range")
    }

    pub fn messages(&self) -> Result<Vec<Message>> {
        let db = self.db.lock().unwrap();
        let mut st =
            db.prepare("SELECT uuid,uid,flags,internal_date FROM messages ORDER BY uid")?;
        let mut rows = st.query([])?;
        let mut out = Vec::new();
        while let Some(r) = rows.next()? {
            let uuid: String = r.get(0)?;
            let uuid = Uuid::parse_str(&uuid)?;
            let path = self.root.join(format!("{uuid}.eml"));
            if !path.exists() {
                continue;
            }
            let size = fs::metadata(&path)?.len();
            let date = DateTime::parse_from_rfc3339(&r.get::<_, String>(3)?)?.with_timezone(&Utc);
            out.push(Message {
                uuid,
                uid: r.get(1)?,
                flags: r.get(2)?,
                internal_date: date,
                size,
                path,
            });
        }
        Ok(out)
    }

    #[allow(dead_code)]
    pub fn append(&self, bytes: &[u8], flags: &str) -> Result<Message> {
        let uuid = Uuid::now_v7();
        let tmp = self.root.join(format!(".{uuid}.tmp"));
        let final_path = self.root.join(format!("{uuid}.eml"));
        fs::write(&tmp, bytes)?;
        fs::rename(&tmp, &final_path)?;

        let db = self.db.lock().unwrap();
        let uid: u32 = db.query_row("SELECT uidnext FROM mailbox WHERE id=1", [], |r| r.get(0))?;
        let date = Utc::now();
        db.execute(
            "INSERT INTO messages(uuid,uid,flags,internal_date) VALUES(?1,?2,?3,?4)",
            params![uuid.to_string(), uid, flags, date.to_rfc3339()],
        )?;
        db.execute("UPDATE mailbox SET uidnext=uidnext+1 WHERE id=1", [])?;
        Ok(Message {
            uuid,
            uid,
            flags: flags.into(),
            internal_date: date,
            size: bytes.len() as u64,
            path: final_path,
        })
    }

    #[allow(dead_code)]
    pub fn set_flags(&self, uid: u32, flags: &str) -> Result<bool> {
        Ok(self.db.lock().unwrap().execute(
            "UPDATE messages SET flags=?1 WHERE uid=?2",
            params![flags, uid],
        )? == 1)
    }

    #[allow(dead_code)]
    pub fn mark_deleted(&self, uid: u32) -> Result<bool> {
        let db = self.db.lock().unwrap();
        let current: Option<String> = db
            .query_row(
                "SELECT flags FROM messages WHERE uid=?1",
                params![uid],
                |r| r.get(0),
            )
            .optional()?;
        let Some(mut flags) = current else {
            return Ok(false);
        };
        if !flags.split_whitespace().any(|x| x == "\\Deleted") {
            if !flags.is_empty() {
                flags.push(' ');
            }
            flags.push_str("\\Deleted");
        }
        db.execute(
            "UPDATE messages SET flags=?1 WHERE uid=?2",
            params![flags, uid],
        )?;
        Ok(true)
    }

    #[allow(dead_code)]
    pub fn expunge(&self) -> Result<Vec<u32>> {
        let doomed = {
            let db = self.db.lock().unwrap();
            let mut st = db.prepare(
                "SELECT uuid,uid FROM messages WHERE instr(' '||flags||' ',' \\Deleted ')>0",
            )?;
            let mut rows = st.query([])?;
            let mut v = Vec::new();
            while let Some(r) = rows.next()? {
                v.push((r.get::<_, String>(0)?, r.get::<_, u32>(1)?));
            }
            v
        };
        let db = self.db.lock().unwrap();
        let mut removed = Vec::new();
        for (uuid, uid) in doomed {
            let path = self.root.join(format!("{uuid}.eml"));
            let _ = fs::remove_file(path);
            db.execute("DELETE FROM messages WHERE uid=?1", params![uid])?;
            removed.push(uid);
        }
        Ok(removed)
    }

    #[allow(dead_code)]
    pub fn read(&self, uid: u32) -> Result<Option<Vec<u8>>> {
        let uuid: Option<String> = self
            .db
            .lock()
            .unwrap()
            .query_row(
                "SELECT uuid FROM messages WHERE uid=?1",
                params![uid],
                |r| r.get(0),
            )
            .optional()?;
        match uuid {
            Some(uuid) => Ok(Some(fs::read(self.root.join(format!("{uuid}.eml")))?)),
            None => Ok(None),
        }
    }
}
