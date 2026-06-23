INSERT INTO users VALUES (1, Alice)
INSERT INTO users VALUES (2, Bob)
SELECT * FROM users
SELECT * FROM replica_users
SELECT * FROM replica_users WHERE id = 1
DELETE FROM users WHERE id = 1
SELECT * FROM replica_users WHERE id = 1
exit
