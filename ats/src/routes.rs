use axum::routing::{delete, get, post};
use axum::Router;

use crate::graphql;
use crate::handlers;
use crate::state::AppState;

/// Build the complete API router. State is attached by `main`.
pub fn api_routes() -> Router<AppState> {
    Router::new()
        .route("/health", get(handlers::health_check))
        .route(
            "/graphql",
            get(graphql::graphql_get).post(graphql::graphql_post),
        )
        .route("/graphql/sdl", get(graphql::graphql_sdl))
        .route("/api/auth/register", post(handlers::register))
        .route("/api/auth/login", post(handlers::login))
        .route("/api/me", get(handlers::me))
        .route(
            "/api/users",
            get(handlers::list_users).post(handlers::create_user),
        )
        .route(
            "/api/users/{id}",
            get(handlers::get_user)
                .patch(handlers::update_user)
                .delete(handlers::delete_user),
        )
        .route(
            "/api/jobs",
            get(handlers::list_jobs).post(handlers::create_job),
        )
        .route(
            "/api/jobs/{id}",
            get(handlers::get_job)
                .patch(handlers::update_job)
                .delete(handlers::delete_job),
        )
        .route(
            "/api/candidates",
            get(handlers::list_candidates).post(handlers::create_candidate),
        )
        .route(
            "/api/candidates/{id}",
            get(handlers::get_candidate)
                .patch(handlers::update_candidate)
                .delete(handlers::delete_candidate),
        )
        .route(
            "/api/applications",
            get(handlers::list_applications).post(handlers::create_application),
        )
        .route(
            "/api/applications/{id}",
            get(handlers::get_application)
                .patch(handlers::update_application)
                .delete(handlers::delete_application),
        )
        .route(
            "/api/interviews",
            get(handlers::list_interviews).post(handlers::create_interview),
        )
        .route(
            "/api/interviews/{id}",
            get(handlers::get_interview)
                .patch(handlers::update_interview)
                .delete(handlers::delete_interview),
        )
        .route(
            "/api/applications/{id}/notes",
            get(handlers::list_notes).post(handlers::create_note),
        )
        .route("/api/notes/{id}", delete(handlers::delete_note))
        .route("/api/stats", get(handlers::stats_overview))
}
