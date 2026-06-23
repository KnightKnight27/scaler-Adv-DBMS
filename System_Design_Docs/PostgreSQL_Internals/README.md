# PostgreSQL Internal Architecture

## 1. Problem Background
Postgres has to serve many users, keep data safe through crashes, and still run complex queries fast. To do that it can't just read and write files for every request. It needs a cache for pages (buffer manager), a way to let readers and writers run together (MVCC), a log so nothing is lost on a power cut (WAL), and a planner smart enough to pick a good join. This writeup looks at each.

## 2. Architecture Overview
```
   SQL query
      |
  Parser -> Planner (uses pg_statistic)
      |
  Executor
      |
 Buffer Manager <--> Shared Buffers (RAM)
      |                    | dirty pages
 Heap / nbtree pages       v
 Data files  <--------  WAL log
```
A query is parsed and planned, the executor asks the buffer manager for pages, changes go to WAL first, and data files get flushed later at a checkpoint.

## 3. Internal Design
Tables are 8KB heap pages, indexes use the `nbtree` B-tree. Every row carries `xmin` and `xmax`, which is how MVCC keeps multiple versions alive. The buffer manager holds hot pages in shared buffers to avoid disk reads. Durability comes from WAL: log the change before writing the data page. Crash recovery just replays WAL from the last checkpoint. The planner uses stats from `pg_statistic` (filled by `ANALYZE`) to estimate rows and pick a plan.

## 4. Design Trade-Offs
MVCC updates don't overwrite rows, they make new versions, which keeps reads lock-free but leaves dead rows, so `VACUUM` is needed to clean them. WAL gives strong durability but adds a write before every change. Shared buffers speed reads but fight the OS cache for RAM. The planner is powerful but only as good as its stats; stale stats give bad plans. Every one trades some overhead for safety or concurrency.

## 5. Experiments / Observations
`EXPLAIN ANALYZE` on a join shows it best: it prints the chosen plan, the planner's estimated rows, and the actual rows and time. When estimates and actuals are close, the stats were good. After running `ANALYZE` on fresh tables the estimates line up better and the plan can even flip (nested loop -> hash join) because the row guesses changed. Watching estimate vs actual is the most useful habit I picked up.
```sql
EXPLAIN ANALYZE
SELECT s.name, e.grade FROM students s
JOIN enrollments e ON s.id = e.student_id
WHERE e.grade = 'A';
```

## Points to Explain

**How pages move through the buffer manager.**
The executor asks the buffer manager for a page. If it's already in shared buffers (hit) it comes from RAM. If not (miss), the manager picks a victim slot using clock-sweep, writes it back if dirty, reads the wanted page into that slot, and returns it. Changed pages are marked dirty and flushed later by the background writer or at a checkpoint, not immediately. That batching avoids a disk write on every change.

**How PostgreSQL implements MVCC.**
Instead of locking rows, Postgres keeps versions. Each row has `xmin` (creator txn) and `xmax` (deleter txn). A transaction takes a snapshot at start, and a row is visible only if its `xmin` is committed and `xmax` isn't visible to it. An update doesn't overwrite, it sets `xmax` on the old row and inserts a new version. So readers never block writers and writers never block readers.

**Why VACUUM is necessary.**
Updates and deletes leave dead row versions that no transaction can see, but they still take space and slow scans. `VACUUM` reclaims that space, updates the visibility map, and freezes old transaction IDs to avoid XID wraparound (which would be catastrophic). Without it a busy table bloats and queries slow down. So VACUUM is the cleanup cost Postgres pays for its lock-free, multi-version design.

**How WAL guarantees durability.**
Before any change hits the real data file, it's written to the WAL and flushed to disk first. That's the "write ahead" rule: log first, data later. So even if the server crashes right after commit, the change is safe in the WAL. On restart, recovery replays WAL from the last checkpoint. Checkpoints flush dirty pages so the WAL to replay stays bounded. That ordering is the whole durability guarantee.

**How planning relies on statistics.**
The planner estimates result sizes instead of running the query. Those estimates come from `pg_statistic`, filled by `ANALYZE`, which stores row counts, common values, and histograms per column. From these it guesses selectivity (how many rows match `grade = 'A'`) and uses costs to pick scans and join methods. Good stats give good plans, stale stats give bad ones. That's why running `ANALYZE` after big changes matters.

## 6. Key Learnings
What clicked is that these parts support each other. MVCC needs VACUUM to stay healthy, WAL makes the buffer manager's lazy "flush later" safe, and the planner is only as smart as its stats. The theme is trading a bit of background work or extra I/O for big wins in concurrency and safety. Seeing estimated vs actual rows also made the planner feel less like magic and more like informed guessing you can help by keeping stats fresh.
