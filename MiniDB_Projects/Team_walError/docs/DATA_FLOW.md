# MiniDB Data Flows

Four representative operations traced end to end and mapped to the code. Function
names are `module.func`; see [`ARCHITECTURE.md`](ARCHITECTURE.md) for the layer
map and [`MODULES.md`](MODULES.md) for interfaces.

---

## 1. INSERT — `INSERT INTO users VALUES (1, 'ada', 36)`

1. **Parse.** `engine.Database.execute` → `engine._run` → `sql.parse` →
   `Insert(table='users', columns=None, rows=[[1,'ada',36]])`.
2. **Dispatch as a write.** `_run` routes to `engine._do_write`. Not inside an
   explicit `BEGIN`, so it opens an implicit transaction
   (`TransactionManager.begin`) and appends a `begin` WAL record.
3. **Apply** (`engine._apply_write`):
   a. `txn.lock_exclusive('users')` → `lock_manager.acquire(txn, 'users', X)`.
   b. `executor._row_in_schema_order` builds the full tuple in column order.
   c. `catalog.Table.insert_row(values)`:
      - PK uniqueness check via the `id` index (`_Index.contains`).
      - `schema.encode(values)` → bytes (`types.Schema.encode`).
      - `heap.HeapFile.insert(record)` → pins the tail page via
        `buffer_pool.fetch_page`, `page.Page.insert`, unpins dirty → returns `RID`.
      - every index updated (`_Index.insert(value, rid)` → `btree.BPlusTree.insert`).
      - `stats.row_count += 1`.
   d. `wal.WriteAheadLog.append({"op":"insert", ...})` (logged *after* a successful apply).
4. **Commit** (autocommit): append `commit`, `wal.flush()` (**fsync — durability
   point**), `TransactionManager.commit` releases the exclusive lock.
5. **Result.** `Result(rowcount=1, message="1 row(s) inserted")`.

*Atomicity:* if any row in a multi-row INSERT fails, `_do_write` aborts the
implicit txn and calls `engine._rebuild_state()` to discard partial in-memory
effects (the uncommitted WAL records are never replayed).

---

## 2. SELECT with JOIN via index — `SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.uid WHERE o.total > 100`

1. **Parse.** `sql.parse` → `Select(items=[u.name, o.total], from='users' u,
   joins=[orders o ON u.id=o.uid], where = o.total > 100)`.
2. **Read dispatch.** `engine._do_read` opens a transient transaction and takes
   **shared** locks on `users` and `orders` (`txn.lock_shared`).
3. **Optimize** (`plan.optimize`):
   - Collect relations `{u: users, o: orders}`; `conjuncts(where)` → `[o.total>100]`.
   - Classify: `o.total>100` is local to `o`; the join predicate `u.id=o.uid` is a
     join conjunct.
   - Estimate cardinalities (`local_selectivity`, `eq_selectivity`) and order
     joins greedily smallest-first (`optimize`'s loop).
   - Access paths via `choose_access_path`: an equality/range on an **indexed**
     column becomes an `executor.IndexScan` (e.g. PK lookups), otherwise a
     `executor.SeqScan`; local predicates layer a `Filter` on top.
   - Build `Project(NestedLoopJoin(outer, inner, on=u.id=o.uid), items)`.
4. **Execute** (`executor.materialize` iterates the tree):
   - `NestedLoopJoin._rows`: for each outer `Row`, re-`open()` the inner operator,
     `merge` rows, and keep those satisfying the `ON` predicate (`evaluate`).
   - Each scan pulls tuples from `catalog.Table.scan` / `index_lookup` →
     `heap.HeapFile` → `buffer_pool` → `disk_manager`.
   - `Project._rows` evaluates the select list against the joined row.
5. **Release + result.** The transient txn commits (releases shared locks);
   `materialize` returns a `Result(columns=['name','total'], rows=[...])`.

Run `EXPLAIN <query>` to see the chosen operators with `est_rows`/`est_cost`
(`plan.format_plan`).

---

## 3. Deadlock between two transactions

Setup (`lock_manager` / `transaction`, exercised by `demos/demo_transactions.py`):

1. T1: `lock_exclusive('A')` → granted (`LockManager._can_grant` true; recorded in
   `_granted['A']`).
2. T2: `lock_exclusive('B')` → granted.
3. T1: `lock_exclusive('B')` → conflicts (T2 holds X). `acquire` records T1 in
   `_waiting['B']`, builds the wait-for graph (`_build_wait_for`): `T1 → T2`. No
   cycle yet → `Condition.wait()` (T1 blocks).
4. T2: `lock_exclusive('A')` → conflicts (T1 holds X). Records T2 in `_waiting['A']`;
   `_build_wait_for` now has `T1→T2` and `T2→T1`. `_creates_cycle(T2)` finds the
   cycle → T2 is the **victim**: removed from waiting, `DeadlockError` raised.
5. T2's caller catches `DeadlockError` and `abort`s (`release_all` frees `B`),
   which `notify_all`s the condition; T1 wakes, re-checks, and now acquires `B`.
6. Net: exactly one victim aborts, the other makes progress — no thread hangs.

In the engine, a `DeadlockError` during a statement triggers
`engine._handle_deadlock`: abort the victim txn and `_rebuild_state()` to discard
its uncommitted effects, then surface a `MiniDBError`.

---

## 4. Crash + recovery

Scenario (from `tests/test_recovery.py` / `demos/demo_recovery.py`):

1. **Session 1.** `Database(path)` opens; `_rebuild_state` truncates the heap file
   and replays an (empty) WAL.
2. Autocommit `CREATE TABLE` + a committed `BEGIN/INSERT×2/COMMIT`: each write is
   applied to the catalog and logged; `COMMIT` is **fsynced** (`wal.flush`).
3. A second `BEGIN` + `INSERT` is **not** committed — its records may be in the WAL
   buffer but no `commit` record is fsynced for that txn.
4. **Crash.** `Database.crash()` drops the process state *without* flushing the
   buffer pool or committing — simulating power loss.
5. **Session 2.** `Database(path)` reopens:
   - `_rebuild_state` truncates the heap file (it is a rebuildable materialization).
   - `wal.WriteAheadLog.__init__` reads all records from `path.wal`.
   - `wal.replay(records, catalog)` computes the committed txn set (those with a
     `commit` record) and re-applies **only** their `create_table`/`insert`/
     `delete` operations via the normal `catalog.Table` paths (`_apply`).
6. **Guarantee.** The committed rows (1, 2) are present; the uncommitted row (3) is
   absent because its transaction was never in the committed set — no UNDO needed.
   `Database.recovery_stats` reports `{committed_txns, applied, skipped}`.

This is the WAL-as-source-of-truth model: durability comes entirely from the
fsynced redo log, and recovery is a deterministic replay of committed work.
