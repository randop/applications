use axum::{
    extract::{Path, Query, State},
    http::StatusCode,
    Json,
};
use serde::Deserialize;
use uuid::Uuid;
use validator::Validate;

use crate::auth::AuthUser;
use crate::dto::{CreateCandidateRequest, UpdateCandidateRequest};
use crate::error::{AppError, AppResult};
use crate::models::Candidate;
use crate::repo;
use crate::state::AppState;

#[derive(Debug, Deserialize)]
pub struct CandidateQuery {
    #[serde(default)]
    pub q: Option<String>,
    #[serde(default = "default_limit")]
    pub limit: i64,
    #[serde(default)]
    pub offset: i64,
}

fn default_limit() -> i64 {
    50
}

pub async fn list_candidates(
    State(state): State<AppState>,
    _user: AuthUser,
    Query(query): Query<CandidateQuery>,
) -> AppResult<Json<Vec<Candidate>>> {
    let candidates =
        repo::list_candidates(&state.db, query.q.as_deref(), query.limit, query.offset).await?;
    Ok(Json(candidates))
}

pub async fn get_candidate(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
) -> AppResult<Json<Candidate>> {
    let candidate = repo::get_candidate(&state.db, &id)
        .await?
        .ok_or_else(|| AppError::NotFound("candidate not found".into()))?;
    Ok(Json(candidate))
}

pub async fn create_candidate(
    State(state): State<AppState>,
    _user: AuthUser,
    Json(payload): Json<CreateCandidateRequest>,
) -> AppResult<(StatusCode, Json<Candidate>)> {
    payload.validate()?;

    let candidate = repo::create_candidate(
        &state.db,
        &payload.first_name,
        &payload.last_name,
        &payload.email,
        payload.phone.as_deref(),
        payload.resume_url.as_deref(),
        payload.linkedin_url.as_deref(),
        payload.source.as_deref(),
    )
    .await?;

    Ok((StatusCode::CREATED, Json(candidate)))
}

pub async fn update_candidate(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
    Json(payload): Json<UpdateCandidateRequest>,
) -> AppResult<Json<Candidate>> {
    payload.validate()?;

    let phone = payload.phone.as_ref().map(|value| value.as_deref());
    let resume_url = payload.resume_url.as_ref().map(|value| value.as_deref());
    let linkedin_url = payload.linkedin_url.as_ref().map(|value| value.as_deref());
    let source = payload.source.as_ref().map(|value| value.as_deref());

    let candidate = repo::update_candidate(
        &state.db,
        &id,
        payload.first_name.as_deref(),
        payload.last_name.as_deref(),
        payload.email.as_deref(),
        phone,
        resume_url,
        linkedin_url,
        source,
    )
    .await?;

    Ok(Json(candidate))
}

pub async fn delete_candidate(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
) -> AppResult<StatusCode> {
    repo::delete_candidate(&state.db, &id).await?;
    Ok(StatusCode::NO_CONTENT)
}
