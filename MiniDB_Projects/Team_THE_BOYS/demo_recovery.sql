-- Core feature demo: crash recovery
CREATE TABLE accounts (id INT PRIMARY KEY, balance INT);
BEGIN;
INSERT INTO accounts (id, balance) VALUES (1, 1000);
INSERT INTO accounts (id, balance) VALUES (2, 500);
COMMIT;
SELECT * FROM accounts;
-- After running this script, delete minidb.dat and run: .recover
