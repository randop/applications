use axum::{
    extract::{Path, Query, State},
    http::StatusCode,
    Json,
};
use serde::Deserialize;
use uuid::Uuid;
use validator::Validate;

use crate::auth::AuthUser;
use crate::dto::{CreateApplicationRequest, UpdateApplicationRequest};
use crate::error::{AppError, AppResult};
use crate::models::{Application, Stage};
use crate::repo;
use crate::state::AppState;

#[derive(Debug, Deserialize)]
pub struct ApplicationQuery {
    pub job_id: Option<Uuid>,
    pub candidate_id: Option<Uuid>,
    pub stage: Option<Stage>,
    #[serde(default = "default_limit")]
    pub limit: i64,
    #[serde(default)]
    pub offset: i64,
}

fn default_limit() -> i64 {
    50
}

pub async fn list_applications(
    State(state): State<AppState>,
    _user: AuthUser,
    Query(query): Query<ApplicationQuery>,
) -> AppResult<Json<Vec<Application>>> {
    let applications = repo::list_applications(
        &state.db,
        query.job_id.as_ref(),
        query.candidate_id.as_ref(),
        query.stage,
        query.limit,
        query.offset,
    )
    .await?;
    Ok(Json(applications))
}

pub async fn get_application(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
) -> AppResult<Json<Application>> {
    let application = repo::get_application(&state.db, &id)
        .await?
        .ok_or_else(|| AppError::NotFound("application not found".into()))?;
    Ok(Json(application))
}

pub async fn create_application(
    State(state): State<AppState>,
    _user: AuthUser,
    Json(payload): Json<CreateApplicationRequest>,
) -> AppResult<(StatusCode, Json<Application>)> {
    payload.validate()?;

    let application =
        repo::create_application(&state.db, &payload.job_id, &payload.candidate_id).await?;

    Ok((StatusCode::CREATED, Json(application)))
}

pub async fn update_application(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
    Json(payload): Json<UpdateApplicationRequest>,
) -> AppResult<Json<Application>> {
    payload.validate()?;

    let application =
        repo::update_application(&state.db, &id, payload.stage, payload.status).await?;

    Ok(Json(application))
}

pub async fn delete_application(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
) -> AppResult<StatusCode> {
    repo::delete_application(&state.db, &id).await?;
    Ok(StatusCode::NO_CONTENT)
}
