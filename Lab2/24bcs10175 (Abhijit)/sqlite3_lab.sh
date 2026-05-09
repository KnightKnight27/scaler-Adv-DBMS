#!/bin/bash

echo "Creating SQLite database..."

sqlite3 football_clubs <<EOF

CREATE TABLE IF NOT EXISTS football_clubs (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    location TEXT NOT NULL,
    ucl INTEGER NOT NULL
);

INSERT INTO football_clubs (id, name, location, ucl) VALUES
(1, 'Barcelona FC', 'Spain', 5),
(2, 'Real Madrid FC', 'Spain', 15),
(3, 'FC Bayern Munich', 'Germany', 6),
(4, 'Liverpool FC', 'England', 6),
(5, 'Manchester United FC', 'England', 3),
(6, 'Manchester City FC', 'England', 1),
(7, 'Arsenal FC', 'England', 0);

CREATE TABLE IF NOT EXISTS movies (
    id INTEGER PRIMARY KEY,
    title TEXT NOT NULL,
    imdb_rating REAL
);

INSERT INTO movies (title, imdb_rating) VALUES
('Oppenheimer', 8.3),
('Dune: Part Two', 8.5),
('Spider-Man: Across the Spider-Verse', 8.6),
('Interstellar', 8.7);

SELECT * FROM football_clubs;

PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;

EOF

echo ""
echo "Database file size:"
ls -lh football_clubs

echo ""
echo "Running query without mmap..."
time sqlite3 football_clubs "SELECT * FROM football_clubs;"

echo ""
echo "Enabling mmap..."
sqlite3 football_clubs "PRAGMA mmap_size=268435456;"

echo ""
echo "Running query with mmap..."
time sqlite3 football_clubs "SELECT * FROM football_clubs;"
