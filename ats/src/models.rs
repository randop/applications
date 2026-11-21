use std::fmt;
use std::str::FromStr;

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

fn parse_enum<T: FromStr>(raw: &str) -> Result<T, String>
where
    T::Err: fmt::Display,
{
    T::from_str(raw).map_err(|e| format!("invalid value '{raw}': {e}"))
}

/// Generate `as_str`, `Display` and `FromStr` for a string-backed enum.
macro_rules! impl_string_enum {
    ($name:ident { $($variant:ident => $value:literal),+ $(,)? }) => {
        impl $name {
            pub fn as_str(&self) -> &'static str {
                match self {
                    $(Self::$variant => $value,)+
                }
            }
        }

        impl fmt::Display for $name {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(self.as_str())
            }
        }

        impl FromStr for $name {
            type Err = String;

            fn from_str(s: &str) -> Result<Self, Self::Err> {
                match s {
                    $($value => Ok(Self::$variant),)+
                    other => Err(format!("unknown {} '{other}'", stringify!($name))),
                }
            }
        }
    };
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, async_graphql::Enum)]
#[serde(rename_all = "snake_case")]
#[graphql(rename_items = "SCREAMING_SNAKE_CASE")]
pub enum UserRole {
    Admin,
    Recruiter,
    HiringManager,
}
impl_string_enum!(UserRole {
    Admin => "admin",
    Recruiter => "recruiter",
    HiringManager => "hiring_manager",
});

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, async_graphql::Enum)]
#[serde(rename_all = "snake_case")]
#[graphql(rename_items = "SCREAMING_SNAKE_CASE")]
pub enum JobStatus {
    Draft,
    Open,
    Closed,
    Archived,
}
impl_string_enum!(JobStatus {
    Draft => "draft",
    Open => "open",
    Closed => "closed",
    Archived => "archived",
});

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, async_graphql::Enum)]
#[serde(rename_all = "snake_case")]
#[graphql(rename_items = "SCREAMING_SNAKE_CASE")]
pub enum EmploymentType {
    FullTime,
    PartTime,
    Contract,
    Internship,
    Temporary,
}
impl_string_enum!(EmploymentType {
    FullTime => "full_time",
    PartTime => "part_time",
    Contract => "contract",
    Internship => "internship",
    Temporary => "temporary",
});

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, async_graphql::Enum)]
#[serde(rename_all = "snake_case")]
#[graphql(rename_items = "SCREAMING_SNAKE_CASE")]
pub enum Stage {
    Applied,
    Screening,
    Interview,
    Offer,
    Hired,
    Rejected,
}
impl_string_enum!(Stage {
    Applied => "applied",
    Screening => "screening",
    Interview => "interview",
    Offer => "offer",
    Hired => "hired",
    Rejected => "rejected",
});

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, async_graphql::Enum)]
#[serde(rename_all = "snake_case")]
#[graphql(rename_items = "SCREAMING_SNAKE_CASE")]
pub enum ApplicationStatus {
    Active,
    Withdrawn,
}
impl_string_enum!(ApplicationStatus {
    Active => "active",
    Withdrawn => "withdrawn",
});

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, async_graphql::Enum)]
#[serde(rename_all = "snake_case")]
#[graphql(rename_items = "SCREAMING_SNAKE_CASE")]
pub enum InterviewStatus {
    Scheduled,
    Completed,
    Canceled,
    NoShow,
}
impl_string_enum!(InterviewStatus {
    Scheduled => "scheduled",
    Completed => "completed",
    Canceled => "canceled",
    NoShow => "no_show",
});

#[derive(Debug, Clone, Serialize)]
pub struct User {
    pub id: Uuid,
    pub email: String,
    #[serde(skip_serializing)]
    pub password_hash: String,
    pub name: String,
    pub role: UserRole,
    pub created_at: DateTime<Utc>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Job {
    pub id: Uuid,
    pub title: String,
    pub description: String,
    pub department: Option<String>,
    pub location: Option<String>,
    pub employment_type: EmploymentType,
    pub status: JobStatus,
    pub created_by: Option<Uuid>,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Candidate {
    pub id: Uuid,
    pub first_name: String,
    pub last_name: String,
    pub email: String,
    pub phone: Option<String>,
    pub resume_url: Option<String>,
    pub linkedin_url: Option<String>,
    pub source: Option<String>,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Application {
    pub id: Uuid,
    pub job_id: Uuid,
    pub candidate_id: Uuid,
    pub stage: Stage,
    pub status: ApplicationStatus,
    pub applied_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Interview {
    pub id: Uuid,
    pub application_id: Uuid,
    pub interviewer_id: Option<Uuid>,
    pub scheduled_at: DateTime<Utc>,
    pub duration_minutes: i64,
    pub location: Option<String>,
    pub status: InterviewStatus,
    pub notes: Option<String>,
    pub created_at: DateTime<Utc>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Note {
    pub id: Uuid,
    pub application_id: Uuid,
    pub author_id: Option<Uuid>,
    pub content: String,
    pub created_at: DateTime<Utc>,
}

pub(crate) fn uuid_from_db(raw: &str) -> Result<Uuid, String> {
    Uuid::parse_str(raw).map_err(|e| format!("invalid uuid in database '{raw}': {e}"))
}

pub(crate) fn datetime_from_db(raw: &str) -> Result<DateTime<Utc>, String> {
    DateTime::parse_from_rfc3339(raw)
        .map(|dt| dt.with_timezone(&Utc))
        .map_err(|e| format!("invalid datetime in database '{raw}': {e}"))
}

pub(crate) fn enum_from_db<T: FromStr>(raw: &str) -> Result<T, String>
where
    T::Err: fmt::Display,
{
    parse_enum(raw)
}
