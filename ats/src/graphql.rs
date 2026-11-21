//! GraphQL API for the ATS, built on `async-graphql` + `async-graphql-axum`.
//!
//! The schema mirrors the REST API: the same queries/mutations backed by the same
//! repository functions. Authentication reuses the JWT from the REST API and is
//! read from the `Authorization: Bearer <token>` header on each request.

use async_graphql::http::GraphiQLSource;
use async_graphql::{
    Context, EmptySubscription, InputObject, Object, Result as GqlResult, Schema, SimpleObject, ID,
};
use async_graphql_axum::{GraphQLRequest, GraphQLResponse};
use axum::extract::State;
use axum::http::HeaderMap;
use axum::response::Html;
use chrono::{DateTime, Utc};
use uuid::Uuid;
use validator::Validate;

use crate::auth::{decode_token, hash_password, sign_token, verify_password, AuthUser};
use crate::dto::{StageCount, StatsResponse};
use crate::models::{
    Application, ApplicationStatus, Candidate, EmploymentType, Interview, InterviewStatus, Job,
    JobStatus, Note, Stage, User, UserRole,
};
use crate::repo;
use crate::state::AppState;

pub type AppSchema = Schema<Query, Mutation, EmptySubscription>;

/// Build the GraphQL schema. This is done once at startup and shared via `Arc`
/// in `AppState`; per-request state is injected in the HTTP handler.
pub fn build_schema() -> AppSchema {
    Schema::build(Query, Mutation, EmptySubscription).finish()
}

// ---------------------------------------------------------------------------
// Output types
// ---------------------------------------------------------------------------

#[derive(SimpleObject)]
#[graphql(name = "User")]
pub struct GqlUser {
    pub id: ID,
    pub email: String,
    pub name: String,
    pub role: UserRole,
    pub created_at: DateTime<Utc>,
}

impl From<User> for GqlUser {
    fn from(user: User) -> Self {
        GqlUser {
            id: ID(user.id.to_string()),
            email: user.email,
            name: user.name,
            role: user.role,
            created_at: user.created_at,
        }
    }
}

#[derive(SimpleObject)]
#[graphql(name = "Job")]
pub struct GqlJob {
    pub id: ID,
    pub title: String,
    pub description: String,
    pub department: Option<String>,
    pub location: Option<String>,
    pub employment_type: EmploymentType,
    pub status: JobStatus,
    pub created_by: Option<ID>,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}

impl From<Job> for GqlJob {
    fn from(job: Job) -> Self {
        GqlJob {
            id: ID(job.id.to_string()),
            title: job.title,
            description: job.description,
            department: job.department,
            location: job.location,
            employment_type: job.employment_type,
            status: job.status,
            created_by: job.created_by.map(|id| ID(id.to_string())),
            created_at: job.created_at,
            updated_at: job.updated_at,
        }
    }
}

#[derive(SimpleObject)]
#[graphql(name = "Candidate")]
pub struct GqlCandidate {
    pub id: ID,
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

impl From<Candidate> for GqlCandidate {
    fn from(candidate: Candidate) -> Self {
        GqlCandidate {
            id: ID(candidate.id.to_string()),
            first_name: candidate.first_name,
            last_name: candidate.last_name,
            email: candidate.email,
            phone: candidate.phone,
            resume_url: candidate.resume_url,
            linkedin_url: candidate.linkedin_url,
            source: candidate.source,
            created_at: candidate.created_at,
            updated_at: candidate.updated_at,
        }
    }
}

#[derive(SimpleObject)]
#[graphql(name = "Application")]
pub struct GqlApplication {
    pub id: ID,
    pub job_id: ID,
    pub candidate_id: ID,
    pub stage: Stage,
    pub status: ApplicationStatus,
    pub applied_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}

impl From<Application> for GqlApplication {
    fn from(application: Application) -> Self {
        GqlApplication {
            id: ID(application.id.to_string()),
            job_id: ID(application.job_id.to_string()),
            candidate_id: ID(application.candidate_id.to_string()),
            stage: application.stage,
            status: application.status,
            applied_at: application.applied_at,
            updated_at: application.updated_at,
        }
    }
}

#[derive(SimpleObject)]
#[graphql(name = "Interview")]
pub struct GqlInterview {
    pub id: ID,
    pub application_id: ID,
    pub interviewer_id: Option<ID>,
    pub scheduled_at: DateTime<Utc>,
    pub duration_minutes: i64,
    pub location: Option<String>,
    pub status: InterviewStatus,
    pub notes: Option<String>,
    pub created_at: DateTime<Utc>,
}

impl From<Interview> for GqlInterview {
    fn from(interview: Interview) -> Self {
        GqlInterview {
            id: ID(interview.id.to_string()),
            application_id: ID(interview.application_id.to_string()),
            interviewer_id: interview.interviewer_id.map(|id| ID(id.to_string())),
            scheduled_at: interview.scheduled_at,
            duration_minutes: interview.duration_minutes,
            location: interview.location,
            status: interview.status,
            notes: interview.notes,
            created_at: interview.created_at,
        }
    }
}

#[derive(SimpleObject)]
#[graphql(name = "Note")]
pub struct GqlNote {
    pub id: ID,
    pub application_id: ID,
    pub author_id: Option<ID>,
    pub content: String,
    pub created_at: DateTime<Utc>,
}

impl From<Note> for GqlNote {
    fn from(note: Note) -> Self {
        GqlNote {
            id: ID(note.id.to_string()),
            application_id: ID(note.application_id.to_string()),
            author_id: note.author_id.map(|id| ID(id.to_string())),
            content: note.content,
            created_at: note.created_at,
        }
    }
}

#[derive(SimpleObject)]
#[graphql(name = "StageCount")]
pub struct GqlStageCount {
    pub stage: Stage,
    pub count: i64,
}

impl From<StageCount> for GqlStageCount {
    fn from(count: StageCount) -> Self {
        GqlStageCount {
            stage: count.stage,
            count: count.count,
        }
    }
}

#[derive(SimpleObject)]
#[graphql(name = "Stats")]
pub struct GqlStats {
    pub jobs_total: i64,
    pub jobs_open: i64,
    pub candidates_total: i64,
    pub applications_total: i64,
    pub applications_active: i64,
    pub interviews_total: i64,
    pub pipeline: Vec<GqlStageCount>,
}

impl From<StatsResponse> for GqlStats {
    fn from(stats: StatsResponse) -> Self {
        GqlStats {
            jobs_total: stats.jobs_total,
            jobs_open: stats.jobs_open,
            candidates_total: stats.candidates_total,
            applications_total: stats.applications_total,
            applications_active: stats.applications_active,
            interviews_total: stats.interviews_total,
            pipeline: stats
                .pipeline
                .into_iter()
                .map(GqlStageCount::from)
                .collect(),
        }
    }
}

#[derive(SimpleObject)]
#[graphql(name = "AuthPayload")]
pub struct GqlAuthPayload {
    pub token: String,
    pub user: GqlUser,
}

// ---------------------------------------------------------------------------
// Input types
// ---------------------------------------------------------------------------

#[derive(InputObject, Validate, Clone)]
pub struct RegisterInput {
    #[validate(length(min = 3, max = 255))]
    pub email: String,
    #[validate(length(min = 8, max = 200))]
    pub password: String,
    #[validate(length(min = 1, max = 120))]
    pub name: String,
    pub role: Option<UserRole>,
}

#[derive(InputObject, Validate, Clone)]
pub struct LoginInput {
    #[validate(length(min = 1))]
    pub email: String,
    #[validate(length(min = 1))]
    pub password: String,
}

#[derive(InputObject, Validate, Clone)]
pub struct CreateUserInput {
    #[validate(length(min = 3, max = 255))]
    pub email: String,
    #[validate(length(min = 8, max = 200))]
    pub password: String,
    #[validate(length(min = 1, max = 120))]
    pub name: String,
    pub role: Option<UserRole>,
}

#[derive(InputObject, Clone)]
pub struct UpdateUserInput {
    pub name: Option<String>,
    pub role: Option<UserRole>,
}

#[derive(InputObject, Validate, Clone)]
pub struct CreateJobInput {
    #[validate(length(min = 1, max = 200))]
    pub title: String,
    #[validate(length(min = 1))]
    pub description: String,
    pub department: Option<String>,
    pub location: Option<String>,
    pub employment_type: Option<EmploymentType>,
    pub status: Option<JobStatus>,
}

#[derive(InputObject, Clone)]
pub struct UpdateJobInput {
    pub title: Option<String>,
    pub description: Option<String>,
    pub department: Option<String>,
    pub location: Option<String>,
    pub employment_type: Option<EmploymentType>,
    pub status: Option<JobStatus>,
}

#[derive(InputObject, Validate, Clone)]
pub struct CreateCandidateInput {
    #[validate(length(min = 1, max = 120))]
    pub first_name: String,
    #[validate(length(min = 1, max = 120))]
    pub last_name: String,
    #[validate(length(min = 3, max = 255))]
    pub email: String,
    pub phone: Option<String>,
    pub resume_url: Option<String>,
    pub linkedin_url: Option<String>,
    pub source: Option<String>,
}

#[derive(InputObject, Clone)]
pub struct UpdateCandidateInput {
    pub first_name: Option<String>,
    pub last_name: Option<String>,
    pub email: Option<String>,
    pub phone: Option<String>,
    pub resume_url: Option<String>,
    pub linkedin_url: Option<String>,
    pub source: Option<String>,
}

#[derive(InputObject, Clone)]
pub struct CreateApplicationInput {
    pub job_id: ID,
    pub candidate_id: ID,
}

#[derive(InputObject, Clone)]
pub struct UpdateApplicationInput {
    pub stage: Option<Stage>,
    pub status: Option<ApplicationStatus>,
}

#[derive(InputObject, Clone)]
pub struct CreateInterviewInput {
    pub application_id: ID,
    pub interviewer_id: Option<ID>,
    pub scheduled_at: DateTime<Utc>,
    pub duration_minutes: Option<i64>,
    pub location: Option<String>,
    pub notes: Option<String>,
}

#[derive(InputObject, Clone)]
pub struct UpdateInterviewInput {
    pub interviewer_id: Option<ID>,
    pub scheduled_at: Option<DateTime<Utc>>,
    pub duration_minutes: Option<i64>,
    pub location: Option<String>,
    pub status: Option<InterviewStatus>,
    pub notes: Option<String>,
}

#[derive(InputObject, Validate, Clone)]
pub struct CreateNoteInput {
    #[validate(length(min = 1, max = 5000))]
    pub content: String,
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn state(ctx: &Context<'_>) -> GqlResult<AppState> {
    Ok(ctx.data::<AppState>()?.clone())
}

fn current_user(ctx: &Context<'_>) -> GqlResult<AuthUser> {
    ctx.data::<Option<AuthUser>>()?
        .clone()
        .ok_or_else(|| async_graphql::Error::new("authentication required"))
}

fn require_admin(ctx: &Context<'_>) -> GqlResult<AuthUser> {
    let user = current_user(ctx)?;
    if user.role == UserRole::Admin {
        Ok(user)
    } else {
        Err(async_graphql::Error::new("admin privileges required"))
    }
}

fn parse_id(raw: &str) -> GqlResult<Uuid> {
    Uuid::parse_str(raw).map_err(|e| async_graphql::Error::new(format!("invalid id '{raw}': {e}")))
}

fn validate_input<T: Validate>(input: &T) -> GqlResult<()> {
    input
        .validate()
        .map_err(|e| async_graphql::Error::new(e.to_string()))
}

fn limit_offset(limit: Option<i64>, offset: Option<i64>) -> (i64, i64) {
    (
        limit.unwrap_or(50).clamp(1, 500),
        offset.unwrap_or(0).max(0),
    )
}

fn decode_auth_header(headers: &HeaderMap, state: &AppState) -> Option<AuthUser> {
    let raw = headers
        .get(axum::http::header::AUTHORIZATION)?
        .to_str()
        .ok()?;
    let token = raw
        .strip_prefix("Bearer ")
        .or_else(|| raw.strip_prefix("bearer "))?;
    decode_token(token, &state.jwt_secret).ok()
}

// ---------------------------------------------------------------------------
// Query root
// ---------------------------------------------------------------------------

pub struct Query;

#[Object]
impl Query {
    async fn version(&self) -> String {
        env!("CARGO_PKG_VERSION").to_string()
    }

    async fn me(&self, ctx: &Context<'_>) -> GqlResult<GqlUser> {
        let auth = current_user(ctx)?;
        let state = state(ctx)?;
        let user = repo::get_user_by_id(&state.db, &auth.id)
            .await?
            .ok_or_else(|| async_graphql::Error::new("user not found"))?;
        Ok(GqlUser::from(user))
    }

    async fn users(&self, ctx: &Context<'_>) -> GqlResult<Vec<GqlUser>> {
        require_admin(ctx)?;
        let state = state(ctx)?;
        let users = repo::list_users(&state.db).await?;
        Ok(users.into_iter().map(GqlUser::from).collect())
    }

    async fn jobs(
        &self,
        ctx: &Context<'_>,
        status: Option<JobStatus>,
        department: Option<String>,
        limit: Option<i64>,
        offset: Option<i64>,
    ) -> GqlResult<Vec<GqlJob>> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let (limit, offset) = limit_offset(limit, offset);
        let jobs = repo::list_jobs(&state.db, status, department.as_deref(), limit, offset).await?;
        Ok(jobs.into_iter().map(GqlJob::from).collect())
    }

    async fn job(&self, ctx: &Context<'_>, id: ID) -> GqlResult<GqlJob> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        let job = repo::get_job(&state.db, &id)
            .await?
            .ok_or_else(|| async_graphql::Error::new("job not found"))?;
        Ok(GqlJob::from(job))
    }

    async fn candidates(
        &self,
        ctx: &Context<'_>,
        q: Option<String>,
        limit: Option<i64>,
        offset: Option<i64>,
    ) -> GqlResult<Vec<GqlCandidate>> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let (limit, offset) = limit_offset(limit, offset);
        let candidates = repo::list_candidates(&state.db, q.as_deref(), limit, offset).await?;
        Ok(candidates.into_iter().map(GqlCandidate::from).collect())
    }

    async fn candidate(&self, ctx: &Context<'_>, id: ID) -> GqlResult<GqlCandidate> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        let candidate = repo::get_candidate(&state.db, &id)
            .await?
            .ok_or_else(|| async_graphql::Error::new("candidate not found"))?;
        Ok(GqlCandidate::from(candidate))
    }

    async fn applications(
        &self,
        ctx: &Context<'_>,
        job_id: Option<ID>,
        candidate_id: Option<ID>,
        stage: Option<Stage>,
        limit: Option<i64>,
        offset: Option<i64>,
    ) -> GqlResult<Vec<GqlApplication>> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let (limit, offset) = limit_offset(limit, offset);
        let job_id = job_id.map(|v| parse_id(v.as_str())).transpose()?;
        let candidate_id = candidate_id.map(|v| parse_id(v.as_str())).transpose()?;
        let applications = repo::list_applications(
            &state.db,
            job_id.as_ref(),
            candidate_id.as_ref(),
            stage,
            limit,
            offset,
        )
        .await?;
        Ok(applications.into_iter().map(GqlApplication::from).collect())
    }

    async fn application(&self, ctx: &Context<'_>, id: ID) -> GqlResult<GqlApplication> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        let application = repo::get_application(&state.db, &id)
            .await?
            .ok_or_else(|| async_graphql::Error::new("application not found"))?;
        Ok(GqlApplication::from(application))
    }

    async fn interviews(
        &self,
        ctx: &Context<'_>,
        application_id: Option<ID>,
        interviewer_id: Option<ID>,
        limit: Option<i64>,
        offset: Option<i64>,
    ) -> GqlResult<Vec<GqlInterview>> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let (limit, offset) = limit_offset(limit, offset);
        let application_id = application_id.map(|v| parse_id(v.as_str())).transpose()?;
        let interviewer_id = interviewer_id.map(|v| parse_id(v.as_str())).transpose()?;
        let interviews = repo::list_interviews(
            &state.db,
            application_id.as_ref(),
            interviewer_id.as_ref(),
            limit,
            offset,
        )
        .await?;
        Ok(interviews.into_iter().map(GqlInterview::from).collect())
    }

    async fn interview(&self, ctx: &Context<'_>, id: ID) -> GqlResult<GqlInterview> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        let interview = repo::get_interview(&state.db, &id)
            .await?
            .ok_or_else(|| async_graphql::Error::new("interview not found"))?;
        Ok(GqlInterview::from(interview))
    }

    async fn notes(&self, ctx: &Context<'_>, application_id: ID) -> GqlResult<Vec<GqlNote>> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let application_id = parse_id(application_id.as_str())?;
        let notes = repo::list_notes(&state.db, &application_id).await?;
        Ok(notes.into_iter().map(GqlNote::from).collect())
    }

    async fn stats(&self, ctx: &Context<'_>) -> GqlResult<GqlStats> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let stats = repo::stats(&state.db).await?;
        Ok(GqlStats::from(stats))
    }
}

// ---------------------------------------------------------------------------
// Mutation root
// ---------------------------------------------------------------------------

pub struct Mutation;

#[Object]
impl Mutation {
    async fn register(&self, ctx: &Context<'_>, input: RegisterInput) -> GqlResult<GqlAuthPayload> {
        validate_input(&input)?;
        let state = state(ctx)?;

        if repo::get_user_by_email(&state.db, &input.email)
            .await?
            .is_some()
        {
            return Err(async_graphql::Error::new("email is already registered"));
        }

        let user_count = repo::count_users(&state.db).await?;
        let role = match input.role {
            Some(UserRole::Admin) => {
                if user_count == 0 {
                    UserRole::Admin
                } else {
                    return Err(async_graphql::Error::new(
                        "admin role can only be assigned to the first registered user",
                    ));
                }
            }
            Some(other) => other,
            None if user_count == 0 => UserRole::Admin,
            None => UserRole::Recruiter,
        };

        let password_hash = hash_password(&input.password)?;
        let user =
            repo::create_user(&state.db, &input.email, &password_hash, &input.name, role).await?;
        let token = sign_token(&user, &state)?;

        Ok(GqlAuthPayload {
            token,
            user: GqlUser::from(user),
        })
    }

    async fn login(&self, ctx: &Context<'_>, input: LoginInput) -> GqlResult<GqlAuthPayload> {
        validate_input(&input)?;
        let state = state(ctx)?;

        let user = repo::get_user_by_email(&state.db, &input.email)
            .await?
            .ok_or_else(|| async_graphql::Error::new("invalid email or password"))?;

        if !verify_password(&input.password, &user.password_hash)? {
            return Err(async_graphql::Error::new("invalid email or password"));
        }

        let token = sign_token(&user, &state)?;
        Ok(GqlAuthPayload {
            token,
            user: GqlUser::from(user),
        })
    }

    async fn create_user(&self, ctx: &Context<'_>, input: CreateUserInput) -> GqlResult<GqlUser> {
        require_admin(ctx)?;
        validate_input(&input)?;
        let state = state(ctx)?;

        if repo::get_user_by_email(&state.db, &input.email)
            .await?
            .is_some()
        {
            return Err(async_graphql::Error::new("email is already registered"));
        }

        let role = input.role.unwrap_or(UserRole::Recruiter);
        let password_hash = hash_password(&input.password)?;
        let user =
            repo::create_user(&state.db, &input.email, &password_hash, &input.name, role).await?;
        Ok(GqlUser::from(user))
    }

    async fn update_user(
        &self,
        ctx: &Context<'_>,
        id: ID,
        input: UpdateUserInput,
    ) -> GqlResult<GqlUser> {
        require_admin(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        let user = repo::update_user(&state.db, &id, input.name.as_deref(), input.role).await?;
        Ok(GqlUser::from(user))
    }

    async fn delete_user(&self, ctx: &Context<'_>, id: ID) -> GqlResult<bool> {
        let auth = require_admin(ctx)?;
        let id = parse_id(id.as_str())?;
        if auth.id == id {
            return Err(async_graphql::Error::new(
                "you cannot delete your own account",
            ));
        }
        let state = state(ctx)?;
        repo::delete_user(&state.db, &id).await?;
        Ok(true)
    }

    async fn create_job(&self, ctx: &Context<'_>, input: CreateJobInput) -> GqlResult<GqlJob> {
        let auth = current_user(ctx)?;
        validate_input(&input)?;
        let state = state(ctx)?;
        let job = repo::create_job(
            &state.db,
            &input.title,
            &input.description,
            input.department.as_deref(),
            input.location.as_deref(),
            input.employment_type.unwrap_or(EmploymentType::FullTime),
            input.status.unwrap_or(JobStatus::Open),
            &auth.id,
        )
        .await?;
        Ok(GqlJob::from(job))
    }

    async fn update_job(
        &self,
        ctx: &Context<'_>,
        id: ID,
        input: UpdateJobInput,
    ) -> GqlResult<GqlJob> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        let department = input.department.as_deref().map(|v| Some(v));
        let location = input.location.as_deref().map(|v| Some(v));
        let job = repo::update_job(
            &state.db,
            &id,
            input.title.as_deref(),
            input.description.as_deref(),
            department,
            location,
            input.employment_type,
            input.status,
        )
        .await?;
        Ok(GqlJob::from(job))
    }

    async fn delete_job(&self, ctx: &Context<'_>, id: ID) -> GqlResult<bool> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        repo::delete_job(&state.db, &id).await?;
        Ok(true)
    }

    async fn create_candidate(
        &self,
        ctx: &Context<'_>,
        input: CreateCandidateInput,
    ) -> GqlResult<GqlCandidate> {
        current_user(ctx)?;
        validate_input(&input)?;
        let state = state(ctx)?;
        let candidate = repo::create_candidate(
            &state.db,
            &input.first_name,
            &input.last_name,
            &input.email,
            input.phone.as_deref(),
            input.resume_url.as_deref(),
            input.linkedin_url.as_deref(),
            input.source.as_deref(),
        )
        .await?;
        Ok(GqlCandidate::from(candidate))
    }

    async fn update_candidate(
        &self,
        ctx: &Context<'_>,
        id: ID,
        input: UpdateCandidateInput,
    ) -> GqlResult<GqlCandidate> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        let candidate = repo::update_candidate(
            &state.db,
            &id,
            input.first_name.as_deref(),
            input.last_name.as_deref(),
            input.email.as_deref(),
            input.phone.as_deref().map(|v| Some(v)),
            input.resume_url.as_deref().map(|v| Some(v)),
            input.linkedin_url.as_deref().map(|v| Some(v)),
            input.source.as_deref().map(|v| Some(v)),
        )
        .await?;
        Ok(GqlCandidate::from(candidate))
    }

    async fn delete_candidate(&self, ctx: &Context<'_>, id: ID) -> GqlResult<bool> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        repo::delete_candidate(&state.db, &id).await?;
        Ok(true)
    }

    async fn create_application(
        &self,
        ctx: &Context<'_>,
        input: CreateApplicationInput,
    ) -> GqlResult<GqlApplication> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let job_id = parse_id(input.job_id.as_str())?;
        let candidate_id = parse_id(input.candidate_id.as_str())?;
        let application = repo::create_application(&state.db, &job_id, &candidate_id).await?;
        Ok(GqlApplication::from(application))
    }

    async fn update_application(
        &self,
        ctx: &Context<'_>,
        id: ID,
        input: UpdateApplicationInput,
    ) -> GqlResult<GqlApplication> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        let application =
            repo::update_application(&state.db, &id, input.stage, input.status).await?;
        Ok(GqlApplication::from(application))
    }

    async fn delete_application(&self, ctx: &Context<'_>, id: ID) -> GqlResult<bool> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        repo::delete_application(&state.db, &id).await?;
        Ok(true)
    }

    async fn create_interview(
        &self,
        ctx: &Context<'_>,
        input: CreateInterviewInput,
    ) -> GqlResult<GqlInterview> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let application_id = parse_id(input.application_id.as_str())?;
        let interviewer_id = match input.interviewer_id {
            Some(value) => Some(parse_id(value.as_str())?),
            None => None,
        };
        let duration_minutes = input.duration_minutes.unwrap_or(45);
        if !(1..=1440).contains(&duration_minutes) {
            return Err(async_graphql::Error::new(
                "duration_minutes must be between 1 and 1440",
            ));
        }
        let interview = repo::create_interview(
            &state.db,
            &application_id,
            interviewer_id.as_ref(),
            input.scheduled_at,
            duration_minutes,
            input.location.as_deref(),
            input.notes.as_deref(),
        )
        .await?;
        Ok(GqlInterview::from(interview))
    }

    async fn update_interview(
        &self,
        ctx: &Context<'_>,
        id: ID,
        input: UpdateInterviewInput,
    ) -> GqlResult<GqlInterview> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        let interviewer_id = match input.interviewer_id {
            Some(value) => Some(parse_id(value.as_str())?),
            None => None,
        };
        if let Some(duration) = input.duration_minutes {
            if !(1..=1440).contains(&duration) {
                return Err(async_graphql::Error::new(
                    "duration_minutes must be between 1 and 1440",
                ));
            }
        }
        let interview = repo::update_interview(
            &state.db,
            &id,
            interviewer_id.as_ref(),
            input.scheduled_at,
            input.duration_minutes,
            input.location.as_deref(),
            input.status,
            input.notes.as_deref(),
        )
        .await?;
        Ok(GqlInterview::from(interview))
    }

    async fn delete_interview(&self, ctx: &Context<'_>, id: ID) -> GqlResult<bool> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        repo::delete_interview(&state.db, &id).await?;
        Ok(true)
    }

    async fn create_note(
        &self,
        ctx: &Context<'_>,
        application_id: ID,
        input: CreateNoteInput,
    ) -> GqlResult<GqlNote> {
        let auth = current_user(ctx)?;
        validate_input(&input)?;
        let state = state(ctx)?;
        let application_id = parse_id(application_id.as_str())?;
        let note = repo::create_note(&state.db, &application_id, &auth.id, &input.content).await?;
        Ok(GqlNote::from(note))
    }

    async fn delete_note(&self, ctx: &Context<'_>, id: ID) -> GqlResult<bool> {
        current_user(ctx)?;
        let state = state(ctx)?;
        let id = parse_id(id.as_str())?;
        repo::delete_note(&state.db, &id).await?;
        Ok(true)
    }
}

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------

pub async fn graphql_post(
    State(state): State<AppState>,
    headers: HeaderMap,
    request: GraphQLRequest,
) -> GraphQLResponse {
    let auth = decode_auth_header(&headers, &state);
    // Inject per-request state (pool/JWT config) so resolvers can reach it; the
    // schema itself is built once at startup and stored on `AppState`.
    let request = request.into_inner().data(state.clone()).data(auth);
    state.schema.execute(request).await.into()
}

pub async fn graphql_get() -> Html<String> {
    Html(GraphiQLSource::build().endpoint("/graphql").finish())
}

pub async fn graphql_sdl(State(state): State<AppState>) -> String {
    state.schema.sdl()
}
