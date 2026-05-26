CREATE TABLE books (
    id     INTEGER PRIMARY KEY,
    title  TEXT NOT NULL,
    pages  INTEGER,
    genre  TEXT
);

CREATE INDEX idx_books_pages
ON books(pages);

INSERT INTO books (title, pages, genre) VALUES
('Refactoring',           448, 'Tech'),
('Clean Code',            464, 'Tech'),
('Sapiens',               498, 'History'),
('Dune',                  688, 'SciFi'),
('Hobbit',                310, 'Fantasy'),
('1984',                  328, 'Fiction'),
('Atomic Habits',         320, 'SelfHelp'),
('Deep Work',             304, 'SelfHelp'),
('CLRS',                 1312, 'Tech'),
('SICP',                  657, 'Tech'),
('Foundation',            255, 'SciFi'),
('Brave New World',       311, 'Fiction');

VACUUM;
