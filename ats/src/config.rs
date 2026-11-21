use crate::error::{AppError, AppResult};
use std::time::Duration;

/// Connection pool configuration is derived from `DATABASE_URL`.
#[derive(Debug, Clone)]
pub struct Config {
    pub port: u16,
    pub database_url: String,
    pub jwt_secret: String,
    pub jwt_ttl_hours: i64,
}

impl Config {
    pub fn from_env() -> AppResult<Self> {
        let port = std::env::var("PORT")
            .ok()
            .and_then(|p| p.parse::<u16>().ok())
            .unwrap_or(8080);

        let database_url = std::env::var("DATABASE_URL")
            .unwrap_or_else(|_| "sqlite://ats.db?mode=rwc".to_string());

        let jwt_secret = std::env::var("JWT_SECRET").unwrap_or_else(|_| {
            tracing::warn!("JWT_SECRET is not set; falling back to an insecure default");
            "change-me-to-a-long-random-string".to_string()
        });

        let jwt_ttl_hours = std::env::var("JWT_TTL_HOURS")
            .ok()
            .and_then(|v| v.parse::<i64>().ok())
            .filter(|v| *v > 0)
            .unwrap_or(24);

        Ok(Self {
            port,
            database_url,
            jwt_secret,
            jwt_ttl_hours,
        })
    }
}

/// Build the SQLite pool and run embedded migrations.
pub async fn init_db(database_url: &str) -> AppResult<sqlx::SqlitePool> {
    use sqlx::sqlite::{
        SqliteConnectOptions, SqliteJournalMode, SqlitePoolOptions, SqliteSynchronous,
    };
    use std::str::FromStr;

    // SQLite-specific performance pragmas:
    //  - WAL + NORMAL synchronous: durable across app crashes, not power loss, much
    //    faster writes than the default FULL.
    //  - 16 MiB page cache and 64 MiB mmap: fewer disk reads for hot pages.
    //  - MEMORY temp store: avoids temp file I/O for sorts/group-by.
    //  - Larger statement cache: reuse prepared statements across requests.
    let options = SqliteConnectOptions::from_str(database_url)
        .map_err(|e| AppError::Internal(format!("invalid DATABASE_URL '{database_url}': {e}")))?
        .create_if_missing(true)
        .foreign_keys(true)
        .journal_mode(SqliteJournalMode::Wal)
        .synchronous(SqliteSynchronous::Normal)
        .busy_timeout(Duration::from_secs(5))
        .statement_cache_capacity(256)
        .pragma("cache_size", "-16384")
        .pragma("mmap_size", "67108864")
        .pragma("temp_store", "memory");

    let pool = SqlitePoolOptions::new()
        .min_connections(1)
        .max_connections(8)
        .idle_timeout(Some(Duration::from_secs(60)))
        .connect_with(options)
        .await?;

    sqlx::migrate!("./migrations").run(&pool).await?;

    Ok(pool)
}
