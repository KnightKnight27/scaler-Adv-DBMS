-- PostgreSQL concurrency observation lab.
--
-- Usage:
--   1. Run the setup block once in psql.
--   2. Open two psql sessions connected to the same database.
--   3. Run the "session 1" and "session 2" blocks as instructed below.

DROP TABLE IF EXISTS accounts;

CREATE TABLE accounts (
  id integer PRIMARY KEY,
  owner_name text NOT NULL,
  balance integer NOT NULL CHECK (balance >= 0)
);

INSERT INTO accounts (id, owner_name, balance)
VALUES
  (1, 'alice', 1000),
  (2, 'bob', 1000);

ANALYZE accounts;

-- Session 1:
-- BEGIN;
-- UPDATE accounts SET balance = balance - 100 WHERE id = 1;
-- SELECT pg_backend_pid() AS session_1_pid;
-- Leave this transaction open before running session 2.

-- Session 2:
-- SELECT balance FROM accounts WHERE id = 1;
-- UPDATE accounts SET balance = balance + 100 WHERE id = 1;
--
-- Expected:
--   The SELECT can read the last committed version.
--   The UPDATE waits because session 1 holds the row-level write lock.

-- Lock inspection query, run from a third psql session if available:
-- SELECT pid, locktype, relation::regclass, mode, granted
-- FROM pg_locks
-- WHERE relation = 'accounts'::regclass
-- ORDER BY pid, granted DESC;

