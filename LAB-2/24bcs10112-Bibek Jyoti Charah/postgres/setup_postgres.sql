CREATE TABLE titles (
    id SERIAL PRIMARY KEY,
    title TEXT NOT NULL,
    artists TEXT NOT NULL
);

INSERT INTO titles (title, artists) VALUES ('Bohemian Rhapsody', 'Queen');
INSERT INTO titles (title, artists) VALUES ('Imagine', 'John Lennon');
INSERT INTO titles (title, artists) VALUES ('Blinding Lights', 'The Weeknd');
INSERT INTO titles (title, artists) VALUES ('Shape of You', 'Ed Sheeran');
INSERT INTO titles (title, artists) VALUES ('Hotel California', 'Eagles');
INSERT INTO titles (title, artists) VALUES ('Smells Like Teen Spirit', 'Nirvana');
INSERT INTO titles (title, artists) VALUES ('Stairway to Heaven', 'Led Zeppelin');
INSERT INTO titles (title, artists) VALUES ('Like a Rolling Stone', 'Bob Dylan');
INSERT INTO titles (title, artists) VALUES ('Yesterday', 'The Beatles');
INSERT INTO titles (title, artists) VALUES ('Hallelujah', 'Leonard Cohen');
