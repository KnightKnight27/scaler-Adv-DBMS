-- SQLite concurrency observation lab.
--
-- Usage:
--   sqlite3 /tmp/sqlite-concurrency.db < sqlite_concurrency.sql
--   sqlite3 /tmp/sqlite-concurrency.db
--
-- Then open a second sqlite3 shell against the same file.

PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;

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

PRAGMA database_list;
PRAGMA page_size;
PRAGMA page_count;
PRAGMA journal_mode;

-- Session 1:
-- BEGIN IMMEDIATE;
-- UPDATE accounts SET balance = balance - 100 WHERE id = 1;
-- Leave this transaction open before running session 2.

-- Session 2:
-- SELECT balance FROM accounts WHERE id = 1;
-- UPDATE accounts SET balance = balance + 100 WHERE id = 1;
--
-- Expected:
--   The SELECT can read a consistent snapshot in WAL mode.
--   The second UPDATE receives "database is locked" or waits until busy timeout.

