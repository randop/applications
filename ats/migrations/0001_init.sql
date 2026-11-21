-- ATS initial schema

CREATE TABLE IF NOT EXISTS users (
    id            TEXT PRIMARY KEY,
    email         TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    name          TEXT NOT NULL,
    role          TEXT NOT NULL DEFAULT 'recruiter',
    created_at    TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS jobs (
    id              TEXT PRIMARY KEY,
    title           TEXT NOT NULL,
    description     TEXT NOT NULL,
    department      TEXT,
    location        TEXT,
    employment_type TEXT NOT NULL DEFAULT 'full_time',
    status          TEXT NOT NULL DEFAULT 'draft',
    created_by      TEXT REFERENCES users (id) ON DELETE SET NULL,
    created_at      TEXT NOT NULL,
    updated_at      TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS candidates (
    id           TEXT PRIMARY KEY,
    first_name   TEXT NOT NULL,
    last_name    TEXT NOT NULL,
    email        TEXT NOT NULL,
    phone        TEXT,
    resume_url   TEXT,
    linkedin_url TEXT,
    source       TEXT,
    created_at   TEXT NOT NULL,
    updated_at   TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS applications (
    id           TEXT PRIMARY KEY,
    job_id       TEXT NOT NULL REFERENCES jobs (id) ON DELETE CASCADE,
    candidate_id TEXT NOT NULL REFERENCES candidates (id) ON DELETE CASCADE,
    stage        TEXT NOT NULL DEFAULT 'applied',
    status       TEXT NOT NULL DEFAULT 'active',
    applied_at   TEXT NOT NULL,
    updated_at   TEXT NOT NULL,
    UNIQUE (job_id, candidate_id)
);

CREATE TABLE IF NOT EXISTS interviews (
    id                TEXT PRIMARY KEY,
    application_id    TEXT NOT NULL REFERENCES applications (id) ON DELETE CASCADE,
    interviewer_id    TEXT REFERENCES users (id) ON DELETE SET NULL,
    scheduled_at      TEXT NOT NULL,
    duration_minutes  INTEGER NOT NULL DEFAULT 45,
    location          TEXT,
    status            TEXT NOT NULL DEFAULT 'scheduled',
    notes             TEXT,
    created_at        TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS notes (
    id             TEXT PRIMARY KEY,
    application_id TEXT NOT NULL REFERENCES applications (id) ON DELETE CASCADE,
    author_id      TEXT REFERENCES users (id) ON DELETE SET NULL,
    content        TEXT NOT NULL,
    created_at     TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_jobs_status ON jobs (status);
CREATE INDEX IF NOT EXISTS idx_jobs_department ON jobs (department);
CREATE INDEX IF NOT EXISTS idx_candidates_email ON candidates (email);
CREATE INDEX IF NOT EXISTS idx_applications_job ON applications (job_id);
CREATE INDEX IF NOT EXISTS idx_applications_candidate ON applications (candidate_id);
CREATE INDEX IF NOT EXISTS idx_applications_stage ON applications (stage);
CREATE INDEX IF NOT EXISTS idx_interviews_application ON interviews (application_id);
CREATE INDEX IF NOT EXISTS idx_notes_application ON notes (application_id);