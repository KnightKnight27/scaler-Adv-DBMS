-- Transactions: COMMIT persists, ROLLBACK reverts.
CREATE TABLE account (id INT, balance INT);
INSERT INTO account VALUES (1, 100);

-- Committed change is durable.
BEGIN;
INSERT INTO account VALUES (2, 200);
COMMIT;
SELECT id, balance FROM account ORDER BY id;

-- Rolled-back change disappears (and the deleted row is restored).
BEGIN;
DELETE FROM account WHERE id = 1;
SELECT id, balance FROM account ORDER BY id;
ROLLBACK;
SELECT id, balance FROM account ORDER BY id;
