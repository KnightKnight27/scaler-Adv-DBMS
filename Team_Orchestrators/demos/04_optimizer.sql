-- Cost-based optimizer: EXPLAIN shows the chosen access path and join algorithm.
CREATE TABLE t (id INT, grp INT);
INSERT INTO t VALUES (1, 0);
INSERT INTO t VALUES (2, 1);
INSERT INTO t VALUES (3, 0);
INSERT INTO t VALUES (4, 1);
INSERT INTO t VALUES (5, 0);
CREATE INDEX ix_t_id ON t(id);
ANALYZE t;
-- Selective equality on the indexed, high-distinct column -> IndexScan.
EXPLAIN SELECT grp FROM t WHERE id = 3;
-- Equality on the un-indexed, low-distinct column -> SeqScan.
EXPLAIN SELECT id FROM t WHERE grp = 0;
