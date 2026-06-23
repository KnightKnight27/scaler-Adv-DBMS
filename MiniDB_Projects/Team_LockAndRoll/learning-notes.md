# Our Learning Notes

This is the stuff we actually learned while building MiniDB — the design calls we
debated, the bugs that taught us something, and the bits that surprised us. The README is
the proper write-up; this is more like our lab notebook. Writing it down also doubles as
our viva prep.

## The one abstraction that paid off

Early on we put everything behind a small per-transaction row API on `Database`:

```cpp
bool   read_key (Transaction*, TableInfo*, int64_t key, Tuple* out);
void   scan_table(Transaction*, TableInfo*, fn(RID, const Tuple&));
void   insert_row(Transaction*, TableInfo*, const Tuple&);
size_t delete_row(Transaction*, TableInfo*, int64_t key);
```

The executors are written against this *once*. Whether a read grabs a shared lock (2PL) or
reads a snapshot (MVCC) is hidden behind it. The payoff: the exact same query plan runs
under either concurrency scheme, and the benchmark compares them on identical code. Lesson
we'll keep: spend time getting the seam right and everything above it gets simpler.

## Slotted pages, and the `free_ptr` that bit us

Pages are 4 KB with a tiny header (`page_lsn`, `num_slots`, `free_ptr`), a slot array
growing down from the top of the header, and tuple data growing up from the bottom. A slot
with length 0 is a tombstone we reuse later. We chose not to compact freed bytes inside a
page — simpler, and fine at our scale — at the cost of some wasted space until a rebuild.

The sneaky one: a brand-new page is just zeros on disk until we initialize it in memory. If
a crash happens before that initialized page is flushed, recovery sees a zero page where
`free_ptr == 0`, and our insert code reads that as "page is full." Took us a bit to spot.
Fix was small once we understood it: an initialized page's `free_ptr` is never 0, so during
recovery we treat 0 as "fresh" and reset it to `PAGE_SIZE`.

## The WAL rule is sacred (and easy to break quietly)

The rule: never write a dirty page to disk before the log records describing it are durable.
We enforce it in exactly two spots — buffer-pool eviction and the explicit flushes — by
calling `flush_to(page_lsn)` before any `write_page`. The `page_lsn` (LSN of the last record
that touched the page) is what ties it together. It's the kind of invariant that's easy to
"mostly" follow and silently get wrong, so we kept the enforcement in one place.

## Recovery: repeat history, then undo the losers

We went with textbook ARIES, three passes: figure out who committed, **redo** every logged
change whose effect isn't on its page yet (`page_lsn < record.lsn`), then **undo** anything
belonging to a transaction that never committed. Two things made this much easier than we
feared:

- **Physiological logging.** Each record carries the before/after image of one slot, so redo
  and undo are just idempotent slot overwrites — no need to replay in a precise allocator
  state. Idempotence is what lets recovery run twice safely if it gets interrupted.
- **Where writes land.** MVCC only writes the heap at commit, so a crash mid-commit leaves
  heap records with no COMMIT and undo cleans them up. 2PL writes eagerly but logs inverse
  ops on abort, which converges to the same place. Either way: committed ⇒ present,
  uncommitted ⇒ gone.

The lesson that actually cost us time: **volatile state has to be wiped on a crash too.** Our
first crash-simulation kept the in-memory lock table around, so after "recovering" a later
read hung forever waiting on a lock held by a transaction that no longer existed. A real
crash would have wiped that table; our simulation had to as well.

## B+ tree: splits are easy, deletes are where the bugs live

Order-64 tree, keys are the integer primary key, values are RIDs, leaves chained for range
scans. Inserts and splits were straightforward. Deletes are the part with all the corner
cases — borrow from a sibling, or merge and pull the separator down, and collapse the root
when it empties. We hammered it with 5,000 keys plus a half-delete pass that forces merges
before trusting it. We also kept the index in memory and rebuild it from the heap on open;
the heap + WAL are the source of truth, so we dodged writing a second recovery protocol.

## 2PL: deadlocks are real, and "read then lock" is a trap

The lock manager keeps shared/exclusive holders per row and a waits-for graph; on every
block it does a quick cycle check and aborts the requester if it finds one. Worked, deadlocks
and all.

The subtle bug a review pass caught: our table scan was reading each row *and then* taking
its lock. That's not strict 2PL — a writer could change the row in the gap and we'd hand back
a stale value. We fixed it to lock the row first, then re-read it under the latch, so the
value we return is the one the lock actually protects. (We also learned where row-level 2PL
stops: it doesn't stop phantom inserts — that'd need predicate/next-key locking. We wrote
that down honestly rather than over-claim "serializable.")

## MVCC was the fun part — and the trickiest bug

Each transaction takes a snapshot timestamp at BEGIN and reads the newest committed version
at or before it (plus its own uncommitted writes). Readers take no locks at all. Writes stage
an uncommitted version; if someone else already has an uncommitted or newer-committed version
of that row, we abort — first-committer-wins.

The best bug of the whole project showed up here. We were bumping the global commit clock
*before* publishing the new versions, and `begin()` read that clock under a different lock. So
a transaction could start, see commit timestamp `N` in its snapshot, but the versions stamped
`N` weren't marked committed yet — it'd miss rows it should have seen. Classic torn snapshot.
The fix was to move the commit clock *into* the version store and assign the timestamp under
the same lock that publishes the versions and that snapshots read. After that a snapshot
either fully includes a commit or fully excludes it, never half. ThreadSanitizer agreed.

## The benchmark humbled us

Our first benchmark showed 2PL and MVCC at basically the same throughput, which made no sense
for the workload we expected MVCC to win. Turns out every commit was doing an `fsync`, both
modes paid it equally, and the disk was all we were measuring — not the concurrency control
at all.

Two fixes: a durability knob to turn off fsync-per-commit for the benchmark (so we measure CC
cost, not disk latency — the recovery tests still exercise durable commits), and a workload
that actually exposes the difference: long read-only scans running next to point writers.
Under 2PL the scans hold shared locks on every row until they commit, so writers stall and
vice-versa; under MVCC the scans read a snapshot and nobody blocks. That got us a clean
~13–16× total-throughput gap, which is the whole point of the track. Lesson: make sure you're
measuring the thing you think you're measuring.

## Optimizer: small, but it makes the real calls

It's a modest cost model — table size from the index, textbook selectivities (`1/N` for a PK
equality, `0.1` for other equalities, `1/3` for ranges) — but it makes the two decisions the
project asks for: index scan vs sequential scan, and join order (greedy by estimated
cardinality, left-deep). `EXPLAIN` prints the plan with the estimates so you can see why it
chose what it chose.

## Treating our own files as untrusted

A late hardening pass changed how we think about the recovery path: the log and pages on disk
might be torn or corrupt, so the code that reads them has to assume the worst. We added bounds
checks to tuple decoding, to the recovery slot-apply, and to the WAL reader (a corrupt record
is treated as a truncated tail, not a buffer overrun). On the SQL side, bad input now produces
a clean error instead of an `std::bad_variant_access` — arithmetic checks its operands, a NULL
primary key is rejected, oversized integer literals are caught, and so on. And the catalog is
written with temp-file + fsync + rename so a crash mid-write can't leave it unreadable.

## What we'd do next

If we kept going: SSI to make MVCC fully serializable (kill write-skew), garbage-collect old
versions, make the B+ tree a real paged + WAL-logged structure instead of rebuilding on open,
add a hash join, swap the coarse global latches for per-page latching, and batch commits
(group commit) so durability stops being the bottleneck.
