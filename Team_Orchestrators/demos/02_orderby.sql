-- ORDER BY: rows returned in ascending key order.
CREATE TABLE scores (player VARCHAR(16), points INT);
INSERT INTO scores VALUES ('zoe', 30);
INSERT INTO scores VALUES ('amy', 50);
INSERT INTO scores VALUES ('max', 10);
SELECT player, points FROM scores ORDER BY points;
SELECT player, points FROM scores ORDER BY player;
