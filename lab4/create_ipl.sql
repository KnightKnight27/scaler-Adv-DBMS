-- Lab 4: tiny IPL database used to study SQLite's on-disk B-tree pages.
--
-- A single table of franchise stats + one secondary index on net_run_rate.
-- The schema is intentionally small so each record's bytes are easy to find
-- in the hex dump.

DROP TABLE IF EXISTS ipl_clubs;

CREATE TABLE ipl_clubs (
    club_id        INTEGER PRIMARY KEY,
    club_name      TEXT    NOT NULL,
    matches_played INTEGER NOT NULL,
    wins           INTEGER NOT NULL,
    losses         INTEGER NOT NULL,
    net_run_rate   REAL    NOT NULL
);

CREATE INDEX idx_ipl_clubs_nrr ON ipl_clubs(net_run_rate);

INSERT INTO ipl_clubs VALUES (1, 'Mumbai Indians',              14, 10, 4, 0.842);
INSERT INTO ipl_clubs VALUES (2, 'Chennai Super Kings',         14,  9, 5, 0.711);
INSERT INTO ipl_clubs VALUES (3, 'Royal Challengers Bengaluru', 14,  8, 6, 0.355);
INSERT INTO ipl_clubs VALUES (4, 'Kolkata Knight Riders',       14,  8, 6, 0.298);
INSERT INTO ipl_clubs VALUES (5, 'Delhi Capitals',              14,  7, 7, 0.104);
INSERT INTO ipl_clubs VALUES (6, 'Rajasthan Royals',            14,  6, 8,-0.112);
INSERT INTO ipl_clubs VALUES (7, 'Sunrisers Hyderabad',         14,  4,10,-0.583);
