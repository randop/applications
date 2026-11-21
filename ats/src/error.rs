use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
    Json,
};
use serde::Serialize;

#[derive(Debug, Serialize)]
pub struct ErrorResponse {
    pub error: String,
    pub message: String,
}

#[derive(Debug, thiserror::Error)]
pub enum AppError {
    #[error("{0}")]
    BadRequest(String),

    #[error("{0}")]
    Unauthorized(String),

    #[error("{0}")]
    Forbidden(String),

    #[error("{0}")]
    NotFound(String),

    #[error("{0}")]
    Conflict(String),

    #[error("validation error: {0}")]
    Validation(#[from] validator::ValidationErrors),

    #[error("database error: {0}")]
    Database(#[from] sqlx::Error),

    #[error("migration error: {0}")]
    Migration(#[from] sqlx::migrate::MigrateError),

    #[error("internal error: {0}")]
    Internal(String),
}

impl AppError {
    fn parts(&self) -> (StatusCode, String, String) {
        match self {
            AppError::BadRequest(message) => (
                StatusCode::BAD_REQUEST,
                "bad_request".to_string(),
                message.clone(),
            ),
            AppError::Unauthorized(message) => (
                StatusCode::UNAUTHORIZED,
                "unauthorized".to_string(),
                message.clone(),
            ),
            AppError::Forbidden(message) => (
                StatusCode::FORBIDDEN,
                "forbidden".to_string(),
                message.clone(),
            ),
            AppError::NotFound(message) => (
                StatusCode::NOT_FOUND,
                "not_found".to_string(),
                message.clone(),
            ),
            AppError::Conflict(message) => (
                StatusCode::CONFLICT,
                "conflict".to_string(),
                message.clone(),
            ),
            AppError::Validation(errors) => (
                StatusCode::BAD_REQUEST,
                "validation_error".to_string(),
                errors.to_string(),
            ),
            AppError::Database(err) => {
                tracing::error!("database error: {err}");
                (
                    StatusCode::INTERNAL_SERVER_ERROR,
                    "database_error".to_string(),
                    "a database error occurred".to_string(),
                )
            }
            AppError::Migration(err) => {
                tracing::error!("migration error: {err}");
                (
                    StatusCode::INTERNAL_SERVER_ERROR,
                    "migration_error".to_string(),
                    "a database migration error occurred".to_string(),
                )
            }
            AppError::Internal(message) => {
                tracing::error!("internal error: {message}");
                (
                    StatusCode::INTERNAL_SERVER_ERROR,
                    "internal_error".to_string(),
                    "an internal error occurred".to_string(),
                )
            }
        }
    }
}

impl IntoResponse for AppError {
    fn into_response(self) -> Response {
        let (status, error, message) = self.parts();
        (status, Json(ErrorResponse { error, message })).into_response()
    }
}

pub type AppResult<T> = Result<T, AppError>;
