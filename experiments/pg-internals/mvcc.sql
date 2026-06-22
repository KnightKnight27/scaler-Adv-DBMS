DROP TABLE IF EXISTS mvcc_demo;
CREATE TABLE mvcc_demo (id int PRIMARY KEY, val text);
INSERT INTO mvcc_demo VALUES (1, 'original');
\echo '--- Before UPDATE ---'
SELECT xmin, xmax, ctid, val FROM mvcc_demo;
UPDATE mvcc_demo SET val = 'updated' WHERE id = 1;
\echo '--- After UPDATE (visible row) ---'
SELECT xmin, xmax, ctid, val FROM mvcc_demo;
\echo '--- Dead tuple stats ---'
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'mvcc_demo';
VACUUM VERBOSE mvcc_demo;
\echo '--- After VACUUM ---'
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'mvcc_demo';
