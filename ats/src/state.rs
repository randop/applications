use std::sync::Arc;

use sqlx::SqlitePool;

use crate::graphql::AppSchema;

/// Shared application state passed to every handler via Axum's `State` extractor.
///
/// The GraphQL schema is built once at startup and shared via `Arc`; cloning this
/// state is cheap (pool + Arc clones), so it can be inserted per request.
#[derive(Clone)]
pub struct AppState {
    pub db: SqlitePool,
    pub jwt_secret: String,
    pub jwt_ttl_hours: i64,
    pub schema: Arc<AppSchema>,
}
