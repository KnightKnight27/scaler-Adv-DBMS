-- Lab 4 - SQLite on-disk format walkthrough
-- Akshat Kushwaha | 24BCS10060
--
-- A tiny library table. The UNIQUE isbn column makes SQLite build an
-- automatic index, so the file ends up with a schema page, a data page,
-- and an index page. VACUUM packs it tight so the layout is easy to read.

CREATE TABLE books (
    id     INTEGER PRIMARY KEY,
    title  TEXT NOT NULL,
    author TEXT NOT NULL,
    year   INTEGER,
    isbn   TEXT UNIQUE
);

INSERT INTO books (title, author, year, isbn) VALUES
    ('Database Internals', 'Alex Petrov',      2019, 'ISBN-1001'),
    ('Designing Data Apps', 'Martin Kleppmann', 2017, 'ISBN-1002'),
    ('The C++ Language',    'Bjarne S',         2013, 'ISBN-1003'),
    ('SQL Performance',     'Markus Winand',    2012, 'ISBN-1004'),
    ('Operating Systems',   'Remzi A',          2018, 'ISBN-1005');

VACUUM;
