#!/bin/bash

echo "Creating PostgreSQL tables..."

sudo -u postgres psql <<EOF

CREATE TABLE IF NOT EXISTS football_clubs (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    location TEXT NOT NULL,
    ucl INTEGER NOT NULL
);

INSERT INTO football_clubs (name, location, ucl) VALUES
('Barcelona FC', 'Spain', 5),
('Real Madrid FC', 'Spain', 15),
('FC Bayern Munich', 'Germany', 6),
('Liverpool FC', 'England', 6),
('Manchester United FC', 'England', 3),
('Manchester City FC', 'England', 1),
('Arsenal FC', 'England', 0);

SELECT * FROM football_clubs;

EOF

echo ""
echo "Running PostgreSQL query timing..."

time sudo -u postgres psql -c "SELECT * FROM football_clubs;"
