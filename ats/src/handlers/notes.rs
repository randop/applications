use axum::{
    extract::{Path, State},
    http::StatusCode,
    Json,
};
use uuid::Uuid;
use validator::Validate;

use crate::auth::AuthUser;
use crate::dto::CreateNoteRequest;
use crate::error::AppResult;
use crate::models::Note;
use crate::repo;
use crate::state::AppState;

pub async fn list_notes(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(application_id): Path<Uuid>,
) -> AppResult<Json<Vec<Note>>> {
    let notes = repo::list_notes(&state.db, &application_id).await?;
    Ok(Json(notes))
}

pub async fn create_note(
    State(state): State<AppState>,
    user: AuthUser,
    Path(application_id): Path<Uuid>,
    Json(payload): Json<CreateNoteRequest>,
) -> AppResult<(StatusCode, Json<Note>)> {
    payload.validate()?;

    let note = repo::create_note(&state.db, &application_id, &user.id, &payload.content).await?;

    Ok((StatusCode::CREATED, Json(note)))
}

pub async fn delete_note(
    State(state): State<AppState>,
    _user: AuthUser,
    Path(id): Path<Uuid>,
) -> AppResult<StatusCode> {
    repo::delete_note(&state.db, &id).await?;
    Ok(StatusCode::NO_CONTENT)
}
