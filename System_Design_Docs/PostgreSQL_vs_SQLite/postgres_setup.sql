DROP TABLE IF EXISTS notes;

CREATE TABLE notes (
    id BIGSERIAL PRIMARY KEY,
    title TEXT NOT NULL,
    owner_name TEXT NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

INSERT INTO notes (title, owner_name) VALUES
    ('server side cache policy', 'api'),
    ('audit event stream', 'db-worker'),
    ('monthly reporting query', 'analytics');

SELECT current_setting('block_size') AS block_size;
SELECT relname, relpages, reltuples
FROM pg_class
WHERE relname = 'notes';
