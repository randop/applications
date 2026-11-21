use axum::{
    extract::{Path, Query, State},
    http::StatusCode,
    Json,
};
use serde::Deserialize;
use uuid::Uuid;
use validator::Validate;

use crate::auth::AuthUser;
use crate::dto::{CreateInterviewRequest, UpdateInterviewRequest};
use crate::error::{AppError, AppResult};
use crate::models::Interview;
use crate::repo;
use crate::state::AppState;

#[derive(Debug, Deserialize)]
pub struct InterviewQuery {
    pub application_id: Option<Uuid>,
    pub interviewer_id: Option<Uuid>,
    #[serde(default = "default_limit")]
    pub limit: i64,
    #[serde(default)]
    pub offset: i64,
}

fn default_limit() -> i64 {
    50
}

pub async fn list_interviews(
    State(state): State<AppState>,
    _user: AuthUser,
    Query(query): Query<InterviewQuery>,
) -> AppResult<Json<Vec<Interview>>> {
    let interviews = repo::list_interviews(
        &state.db,
        query.application_id.as_ref(),
        query.interviewer_id.as_ref(),
        query.limit,
        query.offset,
    )
    .await?;
    Ok(Json(interviews))
}

pub async fn get_interview(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
) -> AppResult<Json<Interview>> {
    let interview = repo::get_interview(&state.db, &id)
        .await?
        .ok_or_else(|| AppError::NotFound("interview not found".into()))?;
    Ok(Json(interview))
}

pub async fn create_interview(
    State(state): State<AppState>,
    _user: AuthUser,
    Json(payload): Json<CreateInterviewRequest>,
) -> AppResult<(StatusCode, Json<Interview>)> {
    payload.validate()?;

    let interview = repo::create_interview(
        &state.db,
        &payload.application_id,
        payload.interviewer_id.as_ref(),
        payload.scheduled_at,
        payload.duration_minutes,
        payload.location.as_deref(),
        payload.notes.as_deref(),
    )
    .await?;

    Ok((StatusCode::CREATED, Json(interview)))
}

pub async fn update_interview(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
    Json(payload): Json<UpdateInterviewRequest>,
) -> AppResult<Json<Interview>> {
    payload.validate()?;

    let interview = repo::update_interview(
        &state.db,
        &id,
        payload.interviewer_id.as_ref(),
        payload.scheduled_at,
        payload.duration_minutes,
        payload.location.as_deref(),
        payload.status,
        payload.notes.as_deref(),
    )
    .await?;

    Ok(Json(interview))
}

pub async fn delete_interview(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
) -> AppResult<StatusCode> {
    repo::delete_interview(&state.db, &id).await?;
    Ok(StatusCode::NO_CONTENT)
}
