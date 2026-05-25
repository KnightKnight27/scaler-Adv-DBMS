-- Create the main table
CREATE TABLE players (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    role TEXT,
    batting_average INTEGER
);

CREATE INDEX idx_players_average ON players(batting_average);

INSERT INTO players (name, role, batting_average) VALUES 
    ('Virat', 'Batsman', 53),
    ('Jasprit', 'Bowler', 11),
    ('Ravindra', 'All-Rounder', 32),
    ('Rohit', 'Batsman', 49),
    ('MS', 'Wicketkeeper', 50);

VACUUM;