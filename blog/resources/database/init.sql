CREATE TABLE IF NOT EXISTS posts (
    id SERIAL PRIMARY KEY,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    title VARCHAR(255) NOT NULL,
    content TEXT NOT NULL
);

GRANT ALL PRIVILEGES ON TABLE posts TO myuser;

INSERT INTO posts (id, title, content) VALUES (1, 'My first post', '# Hello, World!\nThis is a **Markdown** example.');