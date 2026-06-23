PRAGMA journal_mode = WAL;
PRAGMA page_size = 4096;

DROP TABLE IF EXISTS notes;

CREATE TABLE notes (
    id INTEGER PRIMARY KEY,
    title TEXT NOT NULL,
    owner TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

INSERT INTO notes (title, owner, updated_at) VALUES
    ('local cache design', 'app', '2026-06-23'),
    ('sync queue', 'worker', '2026-06-23'),
    ('offline draft', 'mobile', '2026-06-23');

SELECT page_size, page_count FROM pragma_page_size(), pragma_page_count();
