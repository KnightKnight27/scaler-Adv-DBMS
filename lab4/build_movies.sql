-- Drop existing table and index to ensure a clean build
DROP TABLE IF EXISTS movies;
DROP INDEX IF EXISTS idx_movies_year;

-- Initialize the primary movies table
CREATE TABLE movies (
    movie_id INTEGER PRIMARY KEY AUTOINCREMENT,
    movie_title TEXT NOT NULL,
    director_name TEXT NOT NULL,
    release_year INTEGER
);

-- Create a secondary index to optimize year-based lookups
CREATE INDEX idx_movies_year ON movies(release_year);

-- Populate the database with mock test data
INSERT INTO movies (movie_title, director_name, release_year) VALUES ('The Shawshank Redemption', 'Frank Darabont', 1994);
INSERT INTO movies (movie_title, director_name, release_year) VALUES ('The Godfather', 'Francis Ford Coppola', 1972);
INSERT INTO movies (movie_title, director_name, release_year) VALUES ('The Dark Knight', 'Christopher Nolan', 2008);
INSERT INTO movies (movie_title, director_name, release_year) VALUES ('Pulp Fiction', 'Quentin Tarantino', 1994);
INSERT INTO movies (movie_title, director_name, release_year) VALUES ('Schindler''s List', 'Steven Spielberg', 1993);