use axum::{extract::State, http::StatusCode, Json};
use validator::Validate;

use crate::auth::{hash_password, sign_token, verify_password};
use crate::dto::{LoginRequest, LoginResponse, RegisterRequest, UserPublic};
use crate::error::{AppError, AppResult};
use crate::models::UserRole;
use crate::repo;
use crate::state::AppState;

pub async fn register(
    State(state): State<AppState>,
    Json(payload): Json<RegisterRequest>,
) -> AppResult<(StatusCode, Json<LoginResponse>)> {
    payload.validate()?;

    if repo::get_user_by_email(&state.db, &payload.email)
        .await?
        .is_some()
    {
        return Err(AppError::Conflict("email is already registered".into()));
    }

    // Bootstrap rule: the first ever user becomes an admin. Later self-registrations
    // default to recruiter and are not allowed to request the admin role.
    let user_count = repo::count_users(&state.db).await?;
    let role = match payload.role {
        Some(UserRole::Admin) => {
            if user_count == 0 {
                UserRole::Admin
            } else {
                return Err(AppError::Forbidden(
                    "admin role can only be assigned to the first registered user".into(),
                ));
            }
        }
        Some(other) => other,
        None if user_count == 0 => UserRole::Admin,
        None => UserRole::Recruiter,
    };

    let password_hash = hash_password(&payload.password)?;
    let user = repo::create_user(
        &state.db,
        &payload.email,
        &password_hash,
        &payload.name,
        role,
    )
    .await?;

    let token = sign_token(&user, &state)?;
    Ok((
        StatusCode::CREATED,
        Json(LoginResponse {
            token,
            user: UserPublic::from(user),
        }),
    ))
}

pub async fn login(
    State(state): State<AppState>,
    Json(payload): Json<LoginRequest>,
) -> AppResult<Json<LoginResponse>> {
    payload.validate()?;

    let user = repo::get_user_by_email(&state.db, &payload.email)
        .await?
        .ok_or_else(|| AppError::Unauthorized("invalid email or password".into()))?;

    if !verify_password(&payload.password, &user.password_hash)? {
        return Err(AppError::Unauthorized("invalid email or password".into()));
    }

    let token = sign_token(&user, &state)?;
    Ok(Json(LoginResponse {
        token,
        user: UserPublic::from(user),
    }))
}
