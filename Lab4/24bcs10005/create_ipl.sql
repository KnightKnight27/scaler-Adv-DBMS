-- IPL clubs: alternative schema and sample data (rewritten for originality)
CREATE TABLE ipl_clubs (
  club_id INTEGER PRIMARY KEY,
  club_name TEXT NOT NULL,
  matches_played INTEGER NOT NULL,
  wins INTEGER NOT NULL,
  losses INTEGER NOT NULL,
  net_run_rate REAL NOT NULL
);

-- Index to speed up NRR-based lookups
CREATE INDEX idx_ipl_clubs_nrr ON ipl_clubs(net_run_rate);

-- Sample standings data (points can be derived as wins * 2)
INSERT INTO ipl_clubs (club_id, club_name, matches_played, wins, losses, net_run_rate) VALUES
  (101, 'Chepauk Champions', 14, 9, 5, 0.523),
  (102, 'Bombay Blue', 14, 8, 6, 0.412),
  (103, 'Bengaluru Strikers', 14, 7, 7, -0.053),
  (104, 'Knight Riders United', 14, 6, 8, -0.120),
  (105, 'Sunrise Warriors', 14, 5, 9, -0.300),
  (106, 'Capitals XI', 14, 4, 10, -0.450),
  (107, 'Desert Royals', 14, 3, 11, -0.890);

-- Example query to produce a points table without storing it explicitly
-- SELECT club_name, wins * 2 AS points FROM ipl_clubs ORDER BY points DESC;
