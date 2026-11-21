use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;
use validator::Validate;

use crate::models::{
    ApplicationStatus, EmploymentType, InterviewStatus, JobStatus, Stage, User, UserRole,
};

/// Public user representation returned by the API (never exposes the password hash).
#[derive(Debug, Clone, Serialize)]
pub struct UserPublic {
    pub id: Uuid,
    pub email: String,
    pub name: String,
    pub role: UserRole,
    pub created_at: DateTime<Utc>,
}

impl From<User> for UserPublic {
    fn from(user: User) -> Self {
        UserPublic {
            id: user.id,
            email: user.email,
            name: user.name,
            role: user.role,
            created_at: user.created_at,
        }
    }
}

// ----- Auth ----------------------------------------------------------------

#[derive(Debug, Deserialize, Validate)]
pub struct RegisterRequest {
    #[validate(length(min = 3, max = 255, message = "email must be 3-255 chars"))]
    #[serde(default)]
    pub email: String,

    #[validate(length(min = 8, max = 200, message = "password must be 8-200 chars"))]
    #[serde(default)]
    pub password: String,

    #[validate(length(min = 1, max = 120, message = "name must be 1-120 chars"))]
    #[serde(default)]
    pub name: String,

    /// Optional; the first user is always an admin, later self-registrations are recruiters.
    #[serde(default)]
    pub role: Option<UserRole>,
}

#[derive(Debug, Deserialize, Validate)]
pub struct LoginRequest {
    #[validate(length(min = 1, message = "email is required"))]
    #[serde(default)]
    pub email: String,

    #[validate(length(min = 1, message = "password is required"))]
    #[serde(default)]
    pub password: String,
}

#[derive(Debug, Serialize)]
pub struct LoginResponse {
    pub token: String,
    pub user: UserPublic,
}

// ----- Users ---------------------------------------------------------------

#[derive(Debug, Deserialize, Validate)]
pub struct CreateUserRequest {
    #[validate(length(min = 3, max = 255))]
    #[serde(default)]
    pub email: String,

    #[validate(length(min = 8, max = 200))]
    #[serde(default)]
    pub password: String,

    #[validate(length(min = 1, max = 120))]
    #[serde(default)]
    pub name: String,

    #[serde(default)]
    pub role: Option<UserRole>,
}

#[derive(Debug, Deserialize, Validate)]
pub struct UpdateUserRequest {
    #[validate(length(min = 1, max = 120))]
    #[serde(default)]
    pub name: Option<String>,

    #[serde(default)]
    pub role: Option<UserRole>,
}

// ----- Jobs ----------------------------------------------------------------

#[derive(Debug, Deserialize, Validate)]
pub struct CreateJobRequest {
    #[validate(length(min = 1, max = 200))]
    #[serde(default)]
    pub title: String,

    #[validate(length(min = 1, message = "description is required"))]
    #[serde(default)]
    pub description: String,

    #[serde(default)]
    pub department: Option<String>,

    #[serde(default)]
    pub location: Option<String>,

    #[serde(default)]
    pub employment_type: Option<EmploymentType>,

    #[serde(default)]
    pub status: Option<JobStatus>,
}

#[derive(Debug, Deserialize, Validate)]
pub struct UpdateJobRequest {
    #[validate(length(min = 1, max = 200))]
    #[serde(default)]
    pub title: Option<String>,

    #[validate(length(min = 1))]
    #[serde(default)]
    pub description: Option<String>,

    /// When present, sets the department. Send `null` to clear it.
    #[serde(default, deserialize_with = "double_option::deserialize_some")]
    pub department: Option<Option<String>>,

    #[serde(default, deserialize_with = "double_option::deserialize_some")]
    pub location: Option<Option<String>>,

    #[serde(default)]
    pub employment_type: Option<EmploymentType>,

    #[serde(default)]
    pub status: Option<JobStatus>,
}

// ----- Candidates ----------------------------------------------------------

#[derive(Debug, Deserialize, Validate)]
pub struct CreateCandidateRequest {
    #[validate(length(min = 1, max = 120))]
    #[serde(default)]
    pub first_name: String,

    #[validate(length(min = 1, max = 120))]
    #[serde(default)]
    pub last_name: String,

    #[validate(length(min = 3, max = 255))]
    #[serde(default)]
    pub email: String,

    #[serde(default)]
    pub phone: Option<String>,

    #[serde(default)]
    pub resume_url: Option<String>,

    #[serde(default)]
    pub linkedin_url: Option<String>,

    #[serde(default)]
    pub source: Option<String>,
}

#[derive(Debug, Deserialize, Validate)]
pub struct UpdateCandidateRequest {
    #[validate(length(min = 1, max = 120))]
    #[serde(default)]
    pub first_name: Option<String>,

    #[validate(length(min = 1, max = 120))]
    #[serde(default)]
    pub last_name: Option<String>,

    #[validate(length(min = 3, max = 255))]
    #[serde(default)]
    pub email: Option<String>,

    #[serde(default, deserialize_with = "double_option::deserialize_some")]
    pub phone: Option<Option<String>>,

    #[serde(default, deserialize_with = "double_option::deserialize_some")]
    pub resume_url: Option<Option<String>>,

    #[serde(default, deserialize_with = "double_option::deserialize_some")]
    pub linkedin_url: Option<Option<String>>,

    #[serde(default, deserialize_with = "double_option::deserialize_some")]
    pub source: Option<Option<String>>,
}

// ----- Applications --------------------------------------------------------

#[derive(Debug, Deserialize, Validate)]
pub struct CreateApplicationRequest {
    pub job_id: Uuid,

    pub candidate_id: Uuid,
}

#[derive(Debug, Deserialize, Validate)]
pub struct UpdateApplicationRequest {
    #[serde(default)]
    pub stage: Option<Stage>,

    #[serde(default)]
    pub status: Option<ApplicationStatus>,
}

// ----- Interviews ----------------------------------------------------------

#[derive(Debug, Deserialize, Validate)]
pub struct CreateInterviewRequest {
    pub application_id: Uuid,

    #[serde(default)]
    pub interviewer_id: Option<Uuid>,

    pub scheduled_at: DateTime<Utc>,

    #[validate(range(min = 1, max = 1440, message = "duration_minutes must be 1-1440"))]
    #[serde(default = "default_duration")]
    pub duration_minutes: i64,

    #[serde(default)]
    pub location: Option<String>,

    #[serde(default)]
    pub notes: Option<String>,
}

#[derive(Debug, Deserialize, Validate)]
pub struct UpdateInterviewRequest {
    #[serde(default)]
    pub interviewer_id: Option<Uuid>,

    #[serde(default)]
    pub scheduled_at: Option<DateTime<Utc>>,

    #[validate(range(min = 1, max = 1440))]
    #[serde(default)]
    pub duration_minutes: Option<i64>,

    #[serde(default)]
    pub location: Option<String>,

    #[serde(default)]
    pub status: Option<InterviewStatus>,

    #[serde(default)]
    pub notes: Option<String>,
}

// ----- Notes ---------------------------------------------------------------

#[derive(Debug, Deserialize, Validate)]
pub struct CreateNoteRequest {
    #[validate(length(min = 1, max = 5000, message = "content must be 1-5000 chars"))]
    #[serde(default)]
    pub content: String,
}

// ----- Stats ---------------------------------------------------------------

#[derive(Debug, Serialize)]
pub struct StageCount {
    pub stage: Stage,
    pub count: i64,
}

#[derive(Debug, Serialize)]
pub struct StatsResponse {
    pub jobs_total: i64,
    pub jobs_open: i64,
    pub candidates_total: i64,
    pub applications_total: i64,
    pub applications_active: i64,
    pub interviews_total: i64,
    pub pipeline: Vec<StageCount>,
}

fn default_duration() -> i64 {
    45
}

/// Helpers so `Option<T>` fields distinguish "omitted" from "explicit null".
mod double_option {
    use serde::{Deserialize, Deserializer};

    pub fn deserialize_some<'de, T, D>(deserializer: D) -> Result<Option<Option<T>>, D::Error>
    where
        T: Deserialize<'de>,
        D: Deserializer<'de>,
    {
        Deserialize::deserialize(deserializer).map(Some)
    }
}
