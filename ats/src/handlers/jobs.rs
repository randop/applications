use axum::{
    extract::{Path, Query, State},
    http::StatusCode,
    Json,
};
use serde::Deserialize;
use uuid::Uuid;
use validator::Validate;

use crate::auth::AuthUser;
use crate::dto::{CreateJobRequest, UpdateJobRequest};
use crate::error::AppResult;
use crate::models::{EmploymentType, Job, JobStatus};
use crate::repo;
use crate::state::AppState;

#[derive(Debug, Deserialize)]
pub struct JobQuery {
    pub status: Option<JobStatus>,
    pub department: Option<String>,
    #[serde(default = "default_limit")]
    pub limit: i64,
    #[serde(default)]
    pub offset: i64,
}

fn default_limit() -> i64 {
    50
}

pub async fn list_jobs(
    State(state): State<AppState>,
    _user: AuthUser,
    Query(query): Query<JobQuery>,
) -> AppResult<Json<Vec<Job>>> {
    let jobs = repo::list_jobs(
        &state.db,
        query.status,
        query.department.as_deref(),
        query.limit,
        query.offset,
    )
    .await?;
    Ok(Json(jobs))
}

pub async fn get_job(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
) -> AppResult<Json<Job>> {
    let job = repo::get_job(&state.db, &id)
        .await?
        .ok_or_else(|| crate::error::AppError::NotFound("job not found".into()))?;
    Ok(Json(job))
}

pub async fn create_job(
    State(state): State<AppState>,
    user: AuthUser,
    Json(payload): Json<CreateJobRequest>,
) -> AppResult<(StatusCode, Json<Job>)> {
    payload.validate()?;

    let job = repo::create_job(
        &state.db,
        &payload.title,
        &payload.description,
        payload.department.as_deref(),
        payload.location.as_deref(),
        payload.employment_type.unwrap_or(EmploymentType::FullTime),
        payload.status.unwrap_or(JobStatus::Open),
        &user.id,
    )
    .await?;

    Ok((StatusCode::CREATED, Json(job)))
}

pub async fn update_job(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
    Json(payload): Json<UpdateJobRequest>,
) -> AppResult<Json<Job>> {
    payload.validate()?;

    let department = payload.department.as_ref().map(|value| value.as_deref());
    let location = payload.location.as_ref().map(|value| value.as_deref());

    let job = repo::update_job(
        &state.db,
        &id,
        payload.title.as_deref(),
        payload.description.as_deref(),
        department,
        location,
        payload.employment_type,
        payload.status,
    )
    .await?;

    Ok(Json(job))
}

pub async fn delete_job(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
) -> AppResult<StatusCode> {
    repo::delete_job(&state.db, &id).await?;
    Ok(StatusCode::NO_CONTENT)
}
