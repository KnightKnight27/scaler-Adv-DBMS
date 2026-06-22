# Design Decisions

Each decision below records what was chosen, the alternative, and the reasoning.

## Slotted pages with stable RIDs

**Chosen:** a slot directory growing one way and tuple data growing the other.
**Alternative:** fixed-length records packed front to back.
**Why:** the slot indirection lets tuples vary in length (needed for `TEXT`) and
keeps a row's RID (page id + slot id) valid even after neighbouring rows are
deleted. Recovery depends on stable RIDs to replay a change in the same place.

## LRU buffer replacement

**Chosen:** evict the least-recently-used unpinned frame.
**Alternative:** clock / second-chance, or LRU-K.
**Why:** LRU is simple to reason about and to defend, and the pin counter
already prevents evicting a page that is in use. Replacement policy is isolated
in `pick_victim`, so a better policy can be dropped in without touching callers.

## In-memory B+ Tree, rebuilt at startup

**Chosen:** keep the index in memory and rebuild it by scanning the base data
when the database opens.
**Alternative:** persist index pages through the buffer pool like the heap.
**Why:** this keeps the index logic focused on the algorithm (split / borrow /
merge) rather than page layout, while still being correct across restarts. The
cost is a scan at startup. The base data — not the index — is the source of
truth, which also means a corrupt index can never lose committed rows.

## Append-only heap inserts

**Chosen:** insert into the most recent page; allocate a new page when it fills.
**Alternative:** scan all pages for the first one with free space.
**Why:** scanning every page made insert O(n) and the load of 200k rows
O(n²). Appending is O(1). The trade-off is that space freed by deletes in older
pages is not reused; a free-space map would reclaim it and is the natural next
step.

## Strict 2PL with wound-wait

**Chosen:** hold all locks to end-of-transaction (strict 2PL) and prevent
deadlock with wound-wait.
**Alternative:** basic 2PL with a wait-for-graph deadlock *detector*.
**Why:** strict 2PL gives serializability and makes rollback safe (no other
transaction can have read an uncommitted write). Wound-wait prevents deadlock
outright using transaction ids as timestamps, so the engine never has to block a
thread or run cycle detection — a good fit for a single-process teaching engine
where true blocking is hard to demonstrate.

## Logical, physiological logging (redo + undo)

**Chosen:** log the after-image (insert) or before-image (delete) tied to a RID,
and run full redo-then-undo recovery.
**Alternative:** force dirty pages at commit (no redo needed) or never steal
uncommitted pages (no undo needed).
**Why:** the redo+undo design is the interesting one to build and to defend, and
it matches how real engines decouple commit from page flushing (no-force,
steal). Logging at the tuple/RID level keeps redo idempotent — replaying an
insert simply writes the tuple back at its RID.

## Cost-based optimization with fixed selectivities

**Chosen:** estimate equality selectivity as 0.1 and range as 0.33, use those to
order a join's inputs, and prefer an index scan for an indexed equality.
**Alternative:** maintain histograms / distinct-value counts per column.
**Why:** the goal is to show the optimizer *mechanism* — selectivity feeding a
cost comparison that changes the plan — without the bookkeeping of full
statistics. The estimates live in one place (`Optimizer::selectivity`) and could
be replaced with histogram lookups without changing the planner.

## Header-only engine

**Chosen:** implement the engine in headers, with thin `.cpp` entry points.
**Alternative:** split every component into `.h` / `.cpp`.
**Why:** the components are small and heavily interdependent; header-only keeps
the build trivial and the code in one place per concept. The three translation
units (shell, tests, benchmark) each include only what they use.

## Known limitations

- Types are limited to `INT` (64-bit) and `TEXT`.
- The WAL is never truncated, so recovery replays the full history each startup;
  a checkpoint that prunes the log is the obvious extension.
- Joins use a nested loop only; a hash join would help large equi-joins.
- The optimizer considers a single predicate and two-table joins.
- Concurrency is demonstrated through the lock manager's logic rather than with
  OS threads.
