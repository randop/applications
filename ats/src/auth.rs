use argon2::password_hash::{
    rand_core::OsRng, PasswordHash, PasswordHasher, PasswordVerifier, SaltString,
};
use argon2::Argon2;
use axum::{extract::FromRequestParts, http::request::Parts};
use jsonwebtoken::{decode, encode, Algorithm, DecodingKey, EncodingKey, Header, Validation};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::error::AppError;
use crate::models::{User, UserRole};
use crate::state::AppState;

/// Authenticated principal extracted from a validated `Authorization: Bearer <jwt>` header.
#[derive(Debug, Clone)]
pub struct AuthUser {
    pub id: Uuid,
    pub email: String,
    pub name: String,
    pub role: UserRole,
}

impl AuthUser {
    pub fn require_admin(&self) -> Result<(), AppError> {
        if self.role == UserRole::Admin {
            Ok(())
        } else {
            Err(AppError::Forbidden("admin privileges required".into()))
        }
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub struct JwtClaims {
    /// subject: user id
    pub sub: String,
    pub email: String,
    pub name: String,
    pub role: String,
    /// issued at (unix seconds)
    pub iat: i64,
    /// expiry (unix seconds)
    pub exp: i64,
}

pub fn hash_password(plain: &str) -> Result<String, AppError> {
    let salt = SaltString::generate(&mut OsRng);
    Argon2::default()
        .hash_password(plain.as_bytes(), &salt)
        .map(|hash| hash.to_string())
        .map_err(|e| AppError::Internal(format!("failed to hash password: {e}")))
}

pub fn verify_password(plain: &str, hashed: &str) -> Result<bool, AppError> {
    let parsed = PasswordHash::new(hashed)
        .map_err(|e| AppError::Internal(format!("stored password hash is invalid: {e}")))?;
    Ok(Argon2::default()
        .verify_password(plain.as_bytes(), &parsed)
        .is_ok())
}

pub fn sign_token(user: &User, state: &AppState) -> Result<String, AppError> {
    let now = chrono::Utc::now().timestamp();
    let exp = now + state.jwt_ttl_hours * 3600;
    let claims = JwtClaims {
        sub: user.id.to_string(),
        email: user.email.clone(),
        name: user.name.clone(),
        role: user.role.as_str().to_string(),
        iat: now,
        exp,
    };
    encode(
        &Header::new(Algorithm::HS256),
        &claims,
        &EncodingKey::from_secret(state.jwt_secret.as_bytes()),
    )
    .map_err(|e| AppError::Internal(format!("failed to sign token: {e}")))
}

/// Decode and validate a JWT, returning the authenticated principal.
pub fn decode_token(token: &str, secret: &str) -> Result<AuthUser, AppError> {
    let mut validation = Validation::new(Algorithm::HS256);
    validation.validate_exp = true;

    let data = decode::<JwtClaims>(
        token,
        &DecodingKey::from_secret(secret.as_bytes()),
        &validation,
    )
    .map_err(|_| AppError::Unauthorized("invalid or expired token".into()))?;

    let claims = data.claims;
    let id = Uuid::parse_str(&claims.sub)
        .map_err(|_| AppError::Unauthorized("invalid subject in token".into()))?;
    let role = claims
        .role
        .parse::<UserRole>()
        .map_err(|_| AppError::Unauthorized("invalid role in token".into()))?;

    Ok(AuthUser {
        id,
        email: claims.email,
        name: claims.name,
        role,
    })
}

impl FromRequestParts<AppState> for AuthUser {
    type Rejection = AppError;

    async fn from_request_parts(
        parts: &mut Parts,
        state: &AppState,
    ) -> Result<Self, Self::Rejection> {
        let header = parts
            .headers
            .get(axum::http::header::AUTHORIZATION)
            .and_then(|value| value.to_str().ok())
            .ok_or_else(|| AppError::Unauthorized("missing Authorization header".into()))?;

        let token = header
            .strip_prefix("Bearer ")
            .or_else(|| header.strip_prefix("bearer "))
            .ok_or_else(|| {
                AppError::Unauthorized("Authorization header must be 'Bearer <token>'".into())
            })?;

        decode_token(token, &state.jwt_secret)
    }
}
