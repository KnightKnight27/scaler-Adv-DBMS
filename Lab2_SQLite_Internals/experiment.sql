-- SQLite Performance Tuning and Benchmarking Script
-- Optimal PRAGMA Configurations for Database Architectures

PRAGMA page_size = 4096;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA mmap_size = 268435456; -- 256MB memory mapping
PRAGMA cache_size = -65536;    -- 64MB cache

-- Create high-load test schema
CREATE TABLE IF NOT EXISTS users (
    user_id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    email TEXT NOT NULL,
    age INTEGER,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS transactions (
    txn_id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER,
    amount REAL,
    status TEXT,
    FOREIGN KEY(user_id) REFERENCES users(user_id)
);

-- Indexing for Query Optimization
CREATE INDEX IF NOT EXISTS idx_users_age ON users(age);
CREATE INDEX IF NOT EXISTS idx_txn_user_id ON transactions(user_id);

-- Insert transactional data to verify write-path concurrency
BEGIN TRANSACTION;
INSERT INTO users (username, email, age) VALUES ('alice', 'alice@db.org', 25);
INSERT INTO users (username, email, age) VALUES ('bob', 'bob@db.org', 30);
INSERT INTO users (username, email, age) VALUES ('charlie', 'charlie@db.org', 22);

INSERT INTO transactions (user_id, amount, status) VALUES (1, 150.00, 'COMPLETED');
INSERT INTO transactions (user_id, amount, status) VALUES (2, 299.99, 'PENDING');
INSERT INTO transactions (user_id, amount, status) VALUES (3, 45.50, 'COMPLETED');
COMMIT;

-- Benchmark selection performance
SELECT u.username, COUNT(t.txn_id) AS total_txns, SUM(t.amount) AS total_amount
FROM users u
LEFT JOIN transactions t ON u.user_id = t.user_id
WHERE u.age >= 21
GROUP BY u.user_id;
