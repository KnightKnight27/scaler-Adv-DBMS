CREATE TABLE teams (
  id INTEGER PRIMARY KEY,
  team_name TEXT NOT NULL,
  points INTEGER NOT NULL
);

CREATE INDEX idx_teams_points ON teams(points);

INSERT INTO teams (id, team_name, points) VALUES
  (1, 'Chennai Super Kings', 18),
  (2, 'Mumbai Indians', 16),
  (3, 'Royal Challengers Bangalore', 14),
  (4, 'Kolkata Knight Riders', 12),
  (5, 'Sunrisers Hyderabad', 10),
  (6, 'Delhi Capitals', 9),
  (7, 'Rajasthan Royals', 8);
