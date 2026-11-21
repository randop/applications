//! Data-access layer. Every function maps SQLite rows into domain models.
//!
//! SQLite stores ids as TEXT, timestamps as RFC3339 TEXT and enums as TEXT.

use chrono::{DateTime, Utc};
use sqlx::sqlite::SqliteRow;
use sqlx::{QueryBuilder, Row, SqlitePool};
use uuid::Uuid;

use crate::error::{AppError, AppResult};
use crate::models::{
    datetime_from_db, enum_from_db, uuid_from_db, Application, ApplicationStatus, Candidate,
    EmploymentType, Interview, InterviewStatus, Job, JobStatus, Note, Stage, User, UserRole,
};

const JOB_COLUMNS: &str = "id, title, description, department, location, employment_type, status, created_by, created_at, updated_at";
const CANDIDATE_COLUMNS: &str =
    "id, first_name, last_name, email, phone, resume_url, linkedin_url, source, created_at, updated_at";
const APPLICATION_COLUMNS: &str = "id, job_id, candidate_id, stage, status, applied_at, updated_at";
const INTERVIEW_COLUMNS: &str =
    "id, application_id, interviewer_id, scheduled_at, duration_minutes, location, status, notes, created_at";
const NOTE_COLUMNS: &str = "id, application_id, author_id, content, created_at";
const USER_COLUMNS: &str = "id, email, password_hash, name, role, created_at";

fn db_string(err: String) -> AppError {
    AppError::Internal(format!("failed to decode database value: {err}"))
}

fn opt_uuid(row: &SqliteRow, column: &str) -> AppResult<Option<Uuid>> {
    match row.try_get::<Option<String>, _>(column)? {
        Some(raw) => {
            let parsed = uuid_from_db(&raw).map_err(db_string)?;
            Ok(Some(parsed))
        }
        None => Ok(None),
    }
}

// ----- row mappers ---------------------------------------------------------

fn user_from_row(row: &SqliteRow) -> AppResult<User> {
    Ok(User {
        id: uuid_from_db(&row.try_get::<String, _>("id")?).map_err(db_string)?,
        email: row.try_get("email")?,
        password_hash: row.try_get("password_hash")?,
        name: row.try_get("name")?,
        role: enum_from_db(&row.try_get::<String, _>("role")?).map_err(db_string)?,
        created_at: datetime_from_db(&row.try_get::<String, _>("created_at")?)
            .map_err(db_string)?,
    })
}

fn job_from_row(row: &SqliteRow) -> AppResult<Job> {
    Ok(Job {
        id: uuid_from_db(&row.try_get::<String, _>("id")?).map_err(db_string)?,
        title: row.try_get("title")?,
        description: row.try_get("description")?,
        department: row.try_get("department")?,
        location: row.try_get("location")?,
        employment_type: enum_from_db(&row.try_get::<String, _>("employment_type")?)
            .map_err(db_string)?,
        status: enum_from_db(&row.try_get::<String, _>("status")?).map_err(db_string)?,
        created_by: opt_uuid(row, "created_by")?,
        created_at: datetime_from_db(&row.try_get::<String, _>("created_at")?)
            .map_err(db_string)?,
        updated_at: datetime_from_db(&row.try_get::<String, _>("updated_at")?)
            .map_err(db_string)?,
    })
}

fn candidate_from_row(row: &SqliteRow) -> AppResult<Candidate> {
    Ok(Candidate {
        id: uuid_from_db(&row.try_get::<String, _>("id")?).map_err(db_string)?,
        first_name: row.try_get("first_name")?,
        last_name: row.try_get("last_name")?,
        email: row.try_get("email")?,
        phone: row.try_get("phone")?,
        resume_url: row.try_get("resume_url")?,
        linkedin_url: row.try_get("linkedin_url")?,
        source: row.try_get("source")?,
        created_at: datetime_from_db(&row.try_get::<String, _>("created_at")?)
            .map_err(db_string)?,
        updated_at: datetime_from_db(&row.try_get::<String, _>("updated_at")?)
            .map_err(db_string)?,
    })
}

fn application_from_row(row: &SqliteRow) -> AppResult<Application> {
    Ok(Application {
        id: uuid_from_db(&row.try_get::<String, _>("id")?).map_err(db_string)?,
        job_id: uuid_from_db(&row.try_get::<String, _>("job_id")?).map_err(db_string)?,
        candidate_id: uuid_from_db(&row.try_get::<String, _>("candidate_id")?)
            .map_err(db_string)?,
        stage: enum_from_db(&row.try_get::<String, _>("stage")?).map_err(db_string)?,
        status: enum_from_db(&row.try_get::<String, _>("status")?).map_err(db_string)?,
        applied_at: datetime_from_db(&row.try_get::<String, _>("applied_at")?)
            .map_err(db_string)?,
        updated_at: datetime_from_db(&row.try_get::<String, _>("updated_at")?)
            .map_err(db_string)?,
    })
}

fn interview_from_row(row: &SqliteRow) -> AppResult<Interview> {
    Ok(Interview {
        id: uuid_from_db(&row.try_get::<String, _>("id")?).map_err(db_string)?,
        application_id: uuid_from_db(&row.try_get::<String, _>("application_id")?)
            .map_err(db_string)?,
        interviewer_id: opt_uuid(row, "interviewer_id")?,
        scheduled_at: datetime_from_db(&row.try_get::<String, _>("scheduled_at")?)
            .map_err(db_string)?,
        duration_minutes: row.try_get("duration_minutes")?,
        location: row.try_get("location")?,
        status: enum_from_db(&row.try_get::<String, _>("status")?).map_err(db_string)?,
        notes: row.try_get("notes")?,
        created_at: datetime_from_db(&row.try_get::<String, _>("created_at")?)
            .map_err(db_string)?,
    })
}

fn note_from_row(row: &SqliteRow) -> AppResult<Note> {
    Ok(Note {
        id: uuid_from_db(&row.try_get::<String, _>("id")?).map_err(db_string)?,
        application_id: uuid_from_db(&row.try_get::<String, _>("application_id")?)
            .map_err(db_string)?,
        author_id: opt_uuid(row, "author_id")?,
        content: row.try_get("content")?,
        created_at: datetime_from_db(&row.try_get::<String, _>("created_at")?)
            .map_err(db_string)?,
    })
}

// ----- users ---------------------------------------------------------------

pub async fn count_users(db: &SqlitePool) -> AppResult<i64> {
    let count: i64 = sqlx::query_scalar("SELECT COUNT(*) FROM users")
        .fetch_one(db)
        .await?;
    Ok(count)
}

pub async fn get_user_by_id(db: &SqlitePool, id: &Uuid) -> AppResult<Option<User>> {
    let row = sqlx::query(&format!("SELECT {USER_COLUMNS} FROM users WHERE id = ?"))
        .bind(id.to_string())
        .fetch_optional(db)
        .await?;
    row.map(|r| user_from_row(&r)).transpose()
}

pub async fn get_user_by_email(db: &SqlitePool, email: &str) -> AppResult<Option<User>> {
    let row = sqlx::query(&format!("SELECT {USER_COLUMNS} FROM users WHERE email = ?"))
        .bind(email)
        .fetch_optional(db)
        .await?;
    row.map(|r| user_from_row(&r)).transpose()
}

pub async fn create_user(
    db: &SqlitePool,
    email: &str,
    password_hash: &str,
    name: &str,
    role: UserRole,
) -> AppResult<User> {
    let id = Uuid::new_v4();
    let now = Utc::now();
    sqlx::query(
        "INSERT INTO users (id, email, password_hash, name, role, created_at) VALUES (?, ?, ?, ?, ?, ?)",
    )
    .bind(id.to_string())
    .bind(email)
    .bind(password_hash)
    .bind(name)
    .bind(role.as_str())
    .bind(now.to_rfc3339())
    .execute(db)
    .await?;

    get_user_by_id(db, &id)
        .await?
        .ok_or_else(|| AppError::Internal("created user could not be loaded".into()))
}

pub async fn list_users(db: &SqlitePool) -> AppResult<Vec<User>> {
    let rows = sqlx::query(&format!(
        "SELECT {USER_COLUMNS} FROM users ORDER BY created_at ASC"
    ))
    .fetch_all(db)
    .await?;
    rows.iter().map(user_from_row).collect()
}

pub async fn update_user(
    db: &SqlitePool,
    id: &Uuid,
    name: Option<&str>,
    role: Option<UserRole>,
) -> AppResult<User> {
    let existing = get_user_by_id(db, id)
        .await?
        .ok_or_else(|| AppError::NotFound("user not found".into()))?;

    let name = name.unwrap_or(&existing.name);
    let role = role.unwrap_or(existing.role);

    sqlx::query("UPDATE users SET name = ?, role = ? WHERE id = ?")
        .bind(name)
        .bind(role.as_str())
        .bind(id.to_string())
        .execute(db)
        .await?;

    get_user_by_id(db, id)
        .await?
        .ok_or_else(|| AppError::Internal("updated user could not be loaded".into()))
}

pub async fn delete_user(db: &SqlitePool, id: &Uuid) -> AppResult<()> {
    let result = sqlx::query("DELETE FROM users WHERE id = ?")
        .bind(id.to_string())
        .execute(db)
        .await?;
    if result.rows_affected() == 0 {
        return Err(AppError::NotFound("user not found".into()));
    }
    Ok(())
}

// ----- jobs ----------------------------------------------------------------

pub async fn create_job(
    db: &SqlitePool,
    title: &str,
    description: &str,
    department: Option<&str>,
    location: Option<&str>,
    employment_type: EmploymentType,
    status: JobStatus,
    created_by: &Uuid,
) -> AppResult<Job> {
    let id = Uuid::new_v4();
    let now = Utc::now();
    sqlx::query(&format!(
        "INSERT INTO jobs ({JOB_COLUMNS}) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    ))
    .bind(id.to_string())
    .bind(title)
    .bind(description)
    .bind(department)
    .bind(location)
    .bind(employment_type.as_str())
    .bind(status.as_str())
    .bind(created_by.to_string())
    .bind(now.to_rfc3339())
    .bind(now.to_rfc3339())
    .execute(db)
    .await?;

    get_job(db, &id)
        .await?
        .ok_or_else(|| AppError::Internal("created job could not be loaded".into()))
}

pub async fn get_job(db: &SqlitePool, id: &Uuid) -> AppResult<Option<Job>> {
    let row = sqlx::query(&format!("SELECT {JOB_COLUMNS} FROM jobs WHERE id = ?"))
        .bind(id.to_string())
        .fetch_optional(db)
        .await?;
    row.map(|r| job_from_row(&r)).transpose()
}

pub async fn list_jobs(
    db: &SqlitePool,
    status: Option<JobStatus>,
    department: Option<&str>,
    limit: i64,
    offset: i64,
) -> AppResult<Vec<Job>> {
    let limit = limit.clamp(1, 500);
    let offset = offset.max(0);

    // Build the WHERE clause dynamically so each predicate is a plain indexed
    // comparison (SQLite can use idx_jobs_status / idx_jobs_department) instead
    // of the non-sargable `(? IS NULL OR col = ?)` form.
    let mut builder: QueryBuilder<'_, sqlx::Sqlite> =
        QueryBuilder::new(format!("SELECT {JOB_COLUMNS} FROM jobs WHERE 1 = 1"));
    if let Some(status) = status {
        builder
            .push(" AND status = ")
            .push_bind(status.as_str().to_string());
    }
    if let Some(department) = department {
        builder
            .push(" AND department = ")
            .push_bind(department.to_string());
    }
    builder
        .push(" ORDER BY created_at DESC LIMIT ")
        .push_bind(limit)
        .push(" OFFSET ")
        .push_bind(offset);

    let rows = builder.build().fetch_all(db).await?;
    rows.iter().map(job_from_row).collect()
}

pub async fn update_job(
    db: &SqlitePool,
    id: &Uuid,
    title: Option<&str>,
    description: Option<&str>,
    department: Option<Option<&str>>,
    location: Option<Option<&str>>,
    employment_type: Option<EmploymentType>,
    status: Option<JobStatus>,
) -> AppResult<Job> {
    let existing = get_job(db, id)
        .await?
        .ok_or_else(|| AppError::NotFound("job not found".into()))?;

    let title = title.unwrap_or(&existing.title);
    let description = description.unwrap_or(&existing.description);

    let department: Option<String> = match department {
        Some(value) => value.map(|s| s.to_string()),
        None => existing.department.clone(),
    };
    let location: Option<String> = match location {
        Some(value) => value.map(|s| s.to_string()),
        None => existing.location.clone(),
    };
    let employment_type = employment_type.unwrap_or(existing.employment_type);
    let status = status.unwrap_or(existing.status);
    let now = Utc::now();

    sqlx::query(
        "UPDATE jobs SET title = ?, description = ?, department = ?, location = ?, \
         employment_type = ?, status = ?, updated_at = ? WHERE id = ?",
    )
    .bind(title)
    .bind(description)
    .bind(department)
    .bind(location)
    .bind(employment_type.as_str())
    .bind(status.as_str())
    .bind(now.to_rfc3339())
    .bind(id.to_string())
    .execute(db)
    .await?;

    get_job(db, id)
        .await?
        .ok_or_else(|| AppError::Internal("updated job could not be loaded".into()))
}

pub async fn delete_job(db: &SqlitePool, id: &Uuid) -> AppResult<()> {
    let result = sqlx::query("DELETE FROM jobs WHERE id = ?")
        .bind(id.to_string())
        .execute(db)
        .await?;
    if result.rows_affected() == 0 {
        return Err(AppError::NotFound("job not found".into()));
    }
    Ok(())
}

// ----- candidates ----------------------------------------------------------

pub async fn create_candidate(
    db: &SqlitePool,
    first_name: &str,
    last_name: &str,
    email: &str,
    phone: Option<&str>,
    resume_url: Option<&str>,
    linkedin_url: Option<&str>,
    source: Option<&str>,
) -> AppResult<Candidate> {
    let id = Uuid::new_v4();
    let now = Utc::now();
    sqlx::query(&format!(
        "INSERT INTO candidates ({CANDIDATE_COLUMNS}) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    ))
    .bind(id.to_string())
    .bind(first_name)
    .bind(last_name)
    .bind(email)
    .bind(phone)
    .bind(resume_url)
    .bind(linkedin_url)
    .bind(source)
    .bind(now.to_rfc3339())
    .bind(now.to_rfc3339())
    .execute(db)
    .await?;

    get_candidate(db, &id)
        .await?
        .ok_or_else(|| AppError::Internal("created candidate could not be loaded".into()))
}

pub async fn get_candidate(db: &SqlitePool, id: &Uuid) -> AppResult<Option<Candidate>> {
    let row = sqlx::query(&format!(
        "SELECT {CANDIDATE_COLUMNS} FROM candidates WHERE id = ?"
    ))
    .bind(id.to_string())
    .fetch_optional(db)
    .await?;
    row.map(|r| candidate_from_row(&r)).transpose()
}

pub async fn list_candidates(
    db: &SqlitePool,
    query: Option<&str>,
    limit: i64,
    offset: i64,
) -> AppResult<Vec<Candidate>> {
    let limit = limit.clamp(1, 500);
    let offset = offset.max(0);

    let pattern: Option<String> = query
        .map(|q| q.trim())
        .filter(|q| !q.is_empty())
        .map(|q| format!("%{q}%"));

    let mut builder: QueryBuilder<'_, sqlx::Sqlite> = QueryBuilder::new(format!(
        "SELECT {CANDIDATE_COLUMNS} FROM candidates WHERE 1 = 1"
    ));
    if let Some(pattern) = pattern {
        builder
            .push(" AND (first_name LIKE ")
            .push_bind(pattern.clone())
            .push(" OR last_name LIKE ")
            .push_bind(pattern.clone())
            .push(" OR email LIKE ")
            .push_bind(pattern)
            .push(")");
    }
    builder
        .push(" ORDER BY created_at DESC LIMIT ")
        .push_bind(limit)
        .push(" OFFSET ")
        .push_bind(offset);

    let rows = builder.build().fetch_all(db).await?;
    rows.iter().map(candidate_from_row).collect()
}

pub async fn update_candidate(
    db: &SqlitePool,
    id: &Uuid,
    first_name: Option<&str>,
    last_name: Option<&str>,
    email: Option<&str>,
    phone: Option<Option<&str>>,
    resume_url: Option<Option<&str>>,
    linkedin_url: Option<Option<&str>>,
    source: Option<Option<&str>>,
) -> AppResult<Candidate> {
    let existing = get_candidate(db, id)
        .await?
        .ok_or_else(|| AppError::NotFound("candidate not found".into()))?;

    let first_name = first_name.unwrap_or(&existing.first_name);
    let last_name = last_name.unwrap_or(&existing.last_name);
    let email = email.unwrap_or(&existing.email);
    let phone: Option<String> = match phone {
        Some(v) => v.map(|s| s.to_string()),
        None => existing.phone.clone(),
    };
    let resume_url: Option<String> = match resume_url {
        Some(v) => v.map(|s| s.to_string()),
        None => existing.resume_url.clone(),
    };
    let linkedin_url: Option<String> = match linkedin_url {
        Some(v) => v.map(|s| s.to_string()),
        None => existing.linkedin_url.clone(),
    };
    let source: Option<String> = match source {
        Some(v) => v.map(|s| s.to_string()),
        None => existing.source.clone(),
    };
    let now = Utc::now();

    sqlx::query(
        "UPDATE candidates SET first_name = ?, last_name = ?, email = ?, phone = ?, \
         resume_url = ?, linkedin_url = ?, source = ?, updated_at = ? WHERE id = ?",
    )
    .bind(first_name)
    .bind(last_name)
    .bind(email)
    .bind(phone)
    .bind(resume_url)
    .bind(linkedin_url)
    .bind(source)
    .bind(now.to_rfc3339())
    .bind(id.to_string())
    .execute(db)
    .await?;

    get_candidate(db, id)
        .await?
        .ok_or_else(|| AppError::Internal("updated candidate could not be loaded".into()))
}

pub async fn delete_candidate(db: &SqlitePool, id: &Uuid) -> AppResult<()> {
    let result = sqlx::query("DELETE FROM candidates WHERE id = ?")
        .bind(id.to_string())
        .execute(db)
        .await?;
    if result.rows_affected() == 0 {
        return Err(AppError::NotFound("candidate not found".into()));
    }
    Ok(())
}

// ----- applications --------------------------------------------------------

pub async fn create_application(
    db: &SqlitePool,
    job_id: &Uuid,
    candidate_id: &Uuid,
) -> AppResult<Application> {
    let mut tx = db.begin().await?;

    let job = sqlx::query(&format!("SELECT {JOB_COLUMNS} FROM jobs WHERE id = ?"))
        .bind(job_id.to_string())
        .fetch_optional(&mut *tx)
        .await?
        .map(|r| job_from_row(&r))
        .transpose()?
        .ok_or_else(|| AppError::NotFound("job not found".into()))?;

    if job.status != JobStatus::Open {
        return Err(AppError::BadRequest(
            "job is not open for new applications".into(),
        ));
    }

    let candidate_exists: i64 = sqlx::query_scalar("SELECT COUNT(*) FROM candidates WHERE id = ?")
        .bind(candidate_id.to_string())
        .fetch_one(&mut *tx)
        .await?;
    if candidate_exists == 0 {
        return Err(AppError::NotFound("candidate not found".into()));
    }

    let existing: i64 = sqlx::query_scalar(
        "SELECT COUNT(*) FROM applications WHERE job_id = ? AND candidate_id = ?",
    )
    .bind(job_id.to_string())
    .bind(candidate_id.to_string())
    .fetch_one(&mut *tx)
    .await?;
    if existing > 0 {
        return Err(AppError::Conflict(
            "this candidate has already applied to this job".into(),
        ));
    }

    let id = Uuid::new_v4();
    let now = Utc::now();
    sqlx::query(&format!(
        "INSERT INTO applications ({APPLICATION_COLUMNS}) VALUES (?, ?, ?, ?, ?, ?, ?)"
    ))
    .bind(id.to_string())
    .bind(job_id.to_string())
    .bind(candidate_id.to_string())
    .bind(Stage::Applied.as_str())
    .bind(ApplicationStatus::Active.as_str())
    .bind(now.to_rfc3339())
    .bind(now.to_rfc3339())
    .execute(&mut *tx)
    .await?;

    tx.commit().await?;

    get_application(db, &id)
        .await?
        .ok_or_else(|| AppError::Internal("created application could not be loaded".into()))
}

pub async fn get_application(db: &SqlitePool, id: &Uuid) -> AppResult<Option<Application>> {
    let row = sqlx::query(&format!(
        "SELECT {APPLICATION_COLUMNS} FROM applications WHERE id = ?"
    ))
    .bind(id.to_string())
    .fetch_optional(db)
    .await?;
    row.map(|r| application_from_row(&r)).transpose()
}

pub async fn list_applications(
    db: &SqlitePool,
    job_id: Option<&Uuid>,
    candidate_id: Option<&Uuid>,
    stage: Option<Stage>,
    limit: i64,
    offset: i64,
) -> AppResult<Vec<Application>> {
    let limit = limit.clamp(1, 500);
    let offset = offset.max(0);

    let mut builder: QueryBuilder<'_, sqlx::Sqlite> = QueryBuilder::new(format!(
        "SELECT {APPLICATION_COLUMNS} FROM applications WHERE 1 = 1"
    ));
    if let Some(job_id) = job_id {
        builder.push(" AND job_id = ").push_bind(job_id.to_string());
    }
    if let Some(candidate_id) = candidate_id {
        builder
            .push(" AND candidate_id = ")
            .push_bind(candidate_id.to_string());
    }
    if let Some(stage) = stage {
        builder.push(" AND stage = ").push_bind(stage.as_str());
    }
    builder
        .push(" ORDER BY applied_at DESC LIMIT ")
        .push_bind(limit)
        .push(" OFFSET ")
        .push_bind(offset);

    let rows = builder.build().fetch_all(db).await?;
    rows.iter().map(application_from_row).collect()
}

pub async fn update_application(
    db: &SqlitePool,
    id: &Uuid,
    stage: Option<Stage>,
    status: Option<ApplicationStatus>,
) -> AppResult<Application> {
    let existing = get_application(db, id)
        .await?
        .ok_or_else(|| AppError::NotFound("application not found".into()))?;

    let stage = stage.unwrap_or(existing.stage);
    let status = status.unwrap_or(existing.status);
    let now = Utc::now();

    sqlx::query("UPDATE applications SET stage = ?, status = ?, updated_at = ? WHERE id = ?")
        .bind(stage.as_str())
        .bind(status.as_str())
        .bind(now.to_rfc3339())
        .bind(id.to_string())
        .execute(db)
        .await?;

    get_application(db, id)
        .await?
        .ok_or_else(|| AppError::Internal("updated application could not be loaded".into()))
}

pub async fn delete_application(db: &SqlitePool, id: &Uuid) -> AppResult<()> {
    let result = sqlx::query("DELETE FROM applications WHERE id = ?")
        .bind(id.to_string())
        .execute(db)
        .await?;
    if result.rows_affected() == 0 {
        return Err(AppError::NotFound("application not found".into()));
    }
    Ok(())
}

// ----- interviews ----------------------------------------------------------

pub async fn create_interview(
    db: &SqlitePool,
    application_id: &Uuid,
    interviewer_id: Option<&Uuid>,
    scheduled_at: DateTime<Utc>,
    duration_minutes: i64,
    location: Option<&str>,
    notes: Option<&str>,
) -> AppResult<Interview> {
    let application_exists: i64 =
        sqlx::query_scalar("SELECT COUNT(*) FROM applications WHERE id = ?")
            .bind(application_id.to_string())
            .fetch_one(db)
            .await?;
    if application_exists == 0 {
        return Err(AppError::NotFound("application not found".into()));
    }

    if let Some(interviewer) = interviewer_id {
        let interviewer_exists: i64 = sqlx::query_scalar("SELECT COUNT(*) FROM users WHERE id = ?")
            .bind(interviewer.to_string())
            .fetch_one(db)
            .await?;
        if interviewer_exists == 0 {
            return Err(AppError::NotFound("interviewer not found".into()));
        }
    }

    let id = Uuid::new_v4();
    sqlx::query(&format!(
        "INSERT INTO interviews ({INTERVIEW_COLUMNS}) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
    ))
    .bind(id.to_string())
    .bind(application_id.to_string())
    .bind(interviewer_id.map(|i| i.to_string()))
    .bind(scheduled_at.to_rfc3339())
    .bind(duration_minutes)
    .bind(location)
    .bind(InterviewStatus::Scheduled.as_str())
    .bind(notes)
    .bind(Utc::now().to_rfc3339())
    .execute(db)
    .await?;

    get_interview(db, &id)
        .await?
        .ok_or_else(|| AppError::Internal("created interview could not be loaded".into()))
}

pub async fn get_interview(db: &SqlitePool, id: &Uuid) -> AppResult<Option<Interview>> {
    let row = sqlx::query(&format!(
        "SELECT {INTERVIEW_COLUMNS} FROM interviews WHERE id = ?"
    ))
    .bind(id.to_string())
    .fetch_optional(db)
    .await?;
    row.map(|r| interview_from_row(&r)).transpose()
}

pub async fn list_interviews(
    db: &SqlitePool,
    application_id: Option<&Uuid>,
    interviewer_id: Option<&Uuid>,
    limit: i64,
    offset: i64,
) -> AppResult<Vec<Interview>> {
    let limit = limit.clamp(1, 500);
    let offset = offset.max(0);

    let mut builder: QueryBuilder<'_, sqlx::Sqlite> = QueryBuilder::new(format!(
        "SELECT {INTERVIEW_COLUMNS} FROM interviews WHERE 1 = 1"
    ));
    if let Some(application_id) = application_id {
        builder
            .push(" AND application_id = ")
            .push_bind(application_id.to_string());
    }
    if let Some(interviewer_id) = interviewer_id {
        builder
            .push(" AND interviewer_id = ")
            .push_bind(interviewer_id.to_string());
    }
    builder
        .push(" ORDER BY scheduled_at ASC LIMIT ")
        .push_bind(limit)
        .push(" OFFSET ")
        .push_bind(offset);

    let rows = builder.build().fetch_all(db).await?;
    rows.iter().map(interview_from_row).collect()
}

pub async fn update_interview(
    db: &SqlitePool,
    id: &Uuid,
    interviewer_id: Option<&Uuid>,
    scheduled_at: Option<DateTime<Utc>>,
    duration_minutes: Option<i64>,
    location: Option<&str>,
    status: Option<InterviewStatus>,
    notes: Option<&str>,
) -> AppResult<Interview> {
    let existing = get_interview(db, id)
        .await?
        .ok_or_else(|| AppError::NotFound("interview not found".into()))?;

    if let Some(interviewer) = interviewer_id {
        let exists: i64 = sqlx::query_scalar("SELECT COUNT(*) FROM users WHERE id = ?")
            .bind(interviewer.to_string())
            .fetch_one(db)
            .await?;
        if exists == 0 {
            return Err(AppError::NotFound("interviewer not found".into()));
        }
    }

    let resolved_interviewer: Option<String> = match interviewer_id {
        Some(value) => Some(value.to_string()),
        None => existing.interviewer_id.map(|value| value.to_string()),
    };

    let scheduled_at = scheduled_at.unwrap_or(existing.scheduled_at);
    let duration_minutes = duration_minutes.unwrap_or(existing.duration_minutes);

    let resolved_location: Option<String> = match location {
        Some(value) => Some(value.to_string()),
        None => existing.location.clone(),
    };

    let status = status.unwrap_or(existing.status);

    let resolved_notes: Option<String> = match notes {
        Some(value) => Some(value.to_string()),
        None => existing.notes.clone(),
    };

    sqlx::query(
        "UPDATE interviews SET interviewer_id = ?, scheduled_at = ?, duration_minutes = ?, \
         location = ?, status = ?, notes = ? WHERE id = ?",
    )
    .bind(resolved_interviewer)
    .bind(scheduled_at.to_rfc3339())
    .bind(duration_minutes)
    .bind(resolved_location)
    .bind(status.as_str())
    .bind(resolved_notes)
    .bind(id.to_string())
    .execute(db)
    .await?;

    get_interview(db, id)
        .await?
        .ok_or_else(|| AppError::Internal("updated interview could not be loaded".into()))
}

pub async fn delete_interview(db: &SqlitePool, id: &Uuid) -> AppResult<()> {
    let result = sqlx::query("DELETE FROM interviews WHERE id = ?")
        .bind(id.to_string())
        .execute(db)
        .await?;
    if result.rows_affected() == 0 {
        return Err(AppError::NotFound("interview not found".into()));
    }
    Ok(())
}

// ----- notes ---------------------------------------------------------------

pub async fn create_note(
    db: &SqlitePool,
    application_id: &Uuid,
    author_id: &Uuid,
    content: &str,
) -> AppResult<Note> {
    let application_exists: i64 =
        sqlx::query_scalar("SELECT COUNT(*) FROM applications WHERE id = ?")
            .bind(application_id.to_string())
            .fetch_one(db)
            .await?;
    if application_exists == 0 {
        return Err(AppError::NotFound("application not found".into()));
    }

    let id = Uuid::new_v4();
    sqlx::query(&format!(
        "INSERT INTO notes ({NOTE_COLUMNS}) VALUES (?, ?, ?, ?, ?)"
    ))
    .bind(id.to_string())
    .bind(application_id.to_string())
    .bind(author_id.to_string())
    .bind(content)
    .bind(Utc::now().to_rfc3339())
    .execute(db)
    .await?;

    get_note(db, &id)
        .await?
        .ok_or_else(|| AppError::Internal("created note could not be loaded".into()))
}

pub async fn get_note(db: &SqlitePool, id: &Uuid) -> AppResult<Option<Note>> {
    let row = sqlx::query(&format!("SELECT {NOTE_COLUMNS} FROM notes WHERE id = ?"))
        .bind(id.to_string())
        .fetch_optional(db)
        .await?;
    row.map(|r| note_from_row(&r)).transpose()
}

pub async fn list_notes(db: &SqlitePool, application_id: &Uuid) -> AppResult<Vec<Note>> {
    let rows = sqlx::query(&format!(
        "SELECT {NOTE_COLUMNS} FROM notes WHERE application_id = ? ORDER BY created_at ASC"
    ))
    .bind(application_id.to_string())
    .fetch_all(db)
    .await?;
    rows.iter().map(note_from_row).collect()
}

pub async fn delete_note(db: &SqlitePool, id: &Uuid) -> AppResult<()> {
    let result = sqlx::query("DELETE FROM notes WHERE id = ?")
        .bind(id.to_string())
        .execute(db)
        .await?;
    if result.rows_affected() == 0 {
        return Err(AppError::NotFound("note not found".into()));
    }
    Ok(())
}

// ----- stats ---------------------------------------------------------------

pub async fn stats(db: &SqlitePool) -> AppResult<crate::dto::StatsResponse> {
    use crate::dto::{StageCount, StatsResponse};

    // Fetch all scalar counts in a single round-trip instead of six queries.
    let counts = sqlx::query(
        "SELECT \
            (SELECT COUNT(*) FROM jobs) AS jobs_total, \
            (SELECT COUNT(*) FROM jobs WHERE status = 'open') AS jobs_open, \
            (SELECT COUNT(*) FROM candidates) AS candidates_total, \
            (SELECT COUNT(*) FROM applications) AS applications_total, \
            (SELECT COUNT(*) FROM applications WHERE status = 'active') AS applications_active, \
            (SELECT COUNT(*) FROM interviews) AS interviews_total",
    )
    .fetch_one(db)
    .await?;

    let jobs_total: i64 = counts.try_get("jobs_total")?;
    let jobs_open: i64 = counts.try_get("jobs_open")?;
    let candidates_total: i64 = counts.try_get("candidates_total")?;
    let applications_total: i64 = counts.try_get("applications_total")?;
    let applications_active: i64 = counts.try_get("applications_active")?;
    let interviews_total: i64 = counts.try_get("interviews_total")?;

    let rows = sqlx::query(
        "SELECT stage, COUNT(*) AS count FROM applications WHERE status = ? GROUP BY stage",
    )
    .bind(ApplicationStatus::Active.as_str())
    .fetch_all(db)
    .await?;

    let mut pipeline = Vec::new();
    for row in &rows {
        let stage_raw: String = row.try_get("stage")?;
        let count: i64 = row.try_get("count")?;
        if let Ok(stage) = enum_from_db::<Stage>(&stage_raw) {
            pipeline.push(StageCount { stage, count });
        }
    }

    Ok(StatsResponse {
        jobs_total,
        jobs_open,
        candidates_total,
        applications_total,
        applications_active,
        interviews_total,
        pipeline,
    })
}
