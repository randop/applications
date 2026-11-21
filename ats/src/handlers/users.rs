use axum::{
    extract::{Path, State},
    http::StatusCode,
    Json,
};
use uuid::Uuid;
use validator::Validate;

use crate::auth::AuthUser;
use crate::dto::{CreateUserRequest, UpdateUserRequest, UserPublic};
use crate::error::{AppError, AppResult};
use crate::models::UserRole;
use crate::repo;
use crate::state::AppState;

/// Return the currently authenticated user.
pub async fn me(user: AuthUser) -> Json<UserPublic> {
    Json(UserPublic {
        id: user.id,
        email: user.email,
        name: user.name,
        role: user.role,
        created_at: chrono::Utc::now(),
    })
}

pub async fn list_users(
    State(state): State<AppState>,
    user: AuthUser,
) -> AppResult<Json<Vec<UserPublic>>> {
    user.require_admin()?;
    let users = repo::list_users(&state.db).await?;
    Ok(Json(users.into_iter().map(UserPublic::from).collect()))
}

pub async fn get_user(
    State(state): State<AppState>,
    user: AuthUser,
    Path(id): Path<Uuid>,
) -> AppResult<Json<UserPublic>> {
    user.require_admin()?;
    let found = repo::get_user_by_id(&state.db, &id)
        .await?
        .ok_or_else(|| AppError::NotFound("user not found".into()))?;
    Ok(Json(UserPublic::from(found)))
}

pub async fn create_user(
    State(state): State<AppState>,
    user: AuthUser,
    Json(payload): Json<CreateUserRequest>,
) -> AppResult<(StatusCode, Json<UserPublic>)> {
    user.require_admin()?;
    payload.validate()?;

    if repo::get_user_by_email(&state.db, &payload.email)
        .await?
        .is_some()
    {
        return Err(AppError::Conflict("email is already registered".into()));
    }

    let role = payload.role.unwrap_or(UserRole::Recruiter);
    let password_hash = crate::auth::hash_password(&payload.password)?;
    let created = repo::create_user(
        &state.db,
        &payload.email,
        &password_hash,
        &payload.name,
        role,
    )
    .await?;

    Ok((StatusCode::CREATED, Json(UserPublic::from(created))))
}

pub async fn update_user(
    State(state): State<AppState>,
    user: AuthUser,
    Path(id): Path<Uuid>,
    Json(payload): Json<UpdateUserRequest>,
) -> AppResult<Json<UserPublic>> {
    user.require_admin()?;
    payload.validate()?;

    let updated = repo::update_user(&state.db, &id, payload.name.as_deref(), payload.role).await?;
    Ok(Json(UserPublic::from(updated)))
}

pub async fn delete_user(
    State(state): State<AppState>,
    user: AuthUser,
    Path(id): Path<Uuid>,
) -> AppResult<StatusCode> {
    user.require_admin()?;
    if user.id == id {
        return Err(AppError::BadRequest(
            "you cannot delete your own account".into(),
        ));
    }
    repo::delete_user(&state.db, &id).await?;
    Ok(StatusCode::NO_CONTENT)
}
