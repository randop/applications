CREATE TABLE IF NOT EXISTS modes (
    id serial PRIMARY KEY,
    created_at timestamp DEFAULT current_timestamp,
    updated_at timestamp DEFAULT current_timestamp,
    mode varchar(25) NOT NULL
);

INSERT INTO modes (id, mode) VALUES (1, 'markdown'), (2, 'html');

GRANT ALL PRIVILEGES ON TABLE modes TO myuser;

CREATE TABLE IF NOT EXISTS posts (
    id SERIAL PRIMARY KEY,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    title VARCHAR(255) NOT NULL,
    content TEXT NOT NULL,
    mode_id INTEGER NOT NULL DEFAULT 1,
    CONSTRAINT fk_mode_id FOREIGN KEY (mode_id) REFERENCES modes(id)
);

GRANT ALL PRIVILEGES ON TABLE posts TO myuser;

INSERT INTO posts (id, title, content) VALUES (1, 'My first post', '# Hello, World!\nThis is a **Markdown** example.');