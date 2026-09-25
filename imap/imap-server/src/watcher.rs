use crate::store::DirectoryStore;
use anyhow::{Context, Result};
use notify::{Event, EventKind, RecommendedWatcher, RecursiveMode, Watcher};
use std::{
    path::PathBuf,
    sync::{Arc, mpsc},
    thread,
    time::Duration,
};

pub struct DirectoryWatcher {
    _watcher: RecommendedWatcher,
    _worker: thread::JoinHandle<()>,
}

impl DirectoryWatcher {
    pub fn start(store: Arc<DirectoryStore>, spool: PathBuf) -> Result<Self> {
        let (tx, rx) = mpsc::sync_channel::<()>(1);
        let mut watcher =
            notify::recommended_watcher(move |result: notify::Result<Event>| match result {
                Ok(event) if Self::relevant(&event) => {
                    let _ = tx.try_send(());
                }
                Ok(_) => {}
                Err(error) => {
                    tracing::warn!(%error, "filesystem watcher error");
                    let _ = tx.try_send(());
                }
            })
            .context("create filesystem watcher")?;
        watcher
            .watch(&spool, RecursiveMode::NonRecursive)
            .with_context(|| format!("watch {}", spool.display()))?;
        let worker = thread::Builder::new()
            .name("imap-fs-reconcile".into())
            .spawn(move || {
                while rx.recv().is_ok() {
                    thread::sleep(Duration::from_millis(100));
                    while rx.try_recv().is_ok() {}
                    if let Err(error) = store.reconcile() {
                        tracing::warn!(%error, "mailbox reconciliation failed");
                    }
                }
            })
            .context("spawn filesystem watcher worker")?;
        Ok(Self {
            _watcher: watcher,
            _worker: worker,
        })
    }

    fn relevant(event: &Event) -> bool {
        if !matches!(
            event.kind,
            EventKind::Create(_) | EventKind::Modify(_) | EventKind::Remove(_)
        ) {
            return false;
        }
        event.paths.iter().any(|path| {
            path.extension().and_then(|x| x.to_str()) == Some("eml")
                || path
                    .file_name()
                    .and_then(|x| x.to_str())
                    .is_some_and(|name| name.ends_with(".tmp"))
        })
    }
}
