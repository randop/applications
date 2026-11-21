use axum::{extract::State, Json};

use crate::auth::AuthUser;
use crate::dto::StatsResponse;
use crate::error::AppResult;
use crate::repo;
use crate::state::AppState;

pub async fn stats_overview(
    State(state): State<AppState>,
    _user: AuthUser,
) -> AppResult<Json<StatsResponse>> {
    let stats = repo::stats(&state.db).await?;
    Ok(Json(stats))
}
