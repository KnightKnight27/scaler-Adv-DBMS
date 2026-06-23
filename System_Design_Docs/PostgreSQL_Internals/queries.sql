DROP TABLE IF EXISTS accounts;

CREATE TABLE accounts (
    account_id BIGSERIAL PRIMARY KEY,
    holder TEXT NOT NULL,
    balance INTEGER NOT NULL CHECK (balance >= 0)
);

INSERT INTO accounts (holder, balance) VALUES
    ('Aarav', 5000),
    ('Meera', 7500),
    ('Kabir', 3000);

BEGIN;
UPDATE accounts SET balance = balance - 500 WHERE holder = 'Aarav';
UPDATE accounts SET balance = balance + 500 WHERE holder = 'Meera';
COMMIT;

EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM accounts WHERE balance >= 4000;
