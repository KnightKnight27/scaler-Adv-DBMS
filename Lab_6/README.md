# Lab 6 — In-Memory Transaction Manager

This is my Lab 6 submission for Advanced DBMS: a concurrent, in-memory transaction manager written in C++17. I built it to bring together the three classic concurrency-control ideas — **Multi-Version Concurrency Control (MVCC)** for reads, **Strict Two-Phase Locking (Strict 2PL)** for writes, and a cycle-detecting **waits-for deadlock detector** — together with a **vacuum (garbage-collection)** pass that reclaims dead row versions.

---

## Course & Student Metadata
* **Course:** Advanced Database Management Systems (Advanced DBMS)
* **Author:** Abdul Kalam Azad
* **Roll No:** 24BCS10053
* **Language:** C++17

---

## Directory Structure

* [README.md](README.md) - This root document.
* [transaction-manager/](transaction-manager/) - The main project directory.
  * [main.cpp](transaction-manager/main.cpp) - The C++ implementation containing the transaction manager engine and verification scenarios.
  * [README.md](transaction-manager/README.md) - Detail-oriented documentation of the transaction manager's API and visibility logic.
  * [Makefile](transaction-manager/Makefile) - Project build system.
  * [run_tests.sh](transaction-manager/run_tests.sh) - Script that builds the project and runs the full test suite in one step.
  * [screenshot.png](transaction-manager/screenshot.png) - Captured run of the program outputs.

---

## Architecture at a Glance

This transaction manager unites three concurrency control mechanisms:

1. **MVCC (Multi-Version Concurrency Control) for Reads**
   * Writes do not modify rows in-place; they append a new version to a version chain.
   * Readers walk the version chain and fetch the latest version visible to their snapshot.
   * Snapshots are based on a global commit clock (`commitClock`) rather than transaction IDs, preventing anomalies where a smaller-ID transaction commits *after* a reader begins.

2. **Strict Two-Phase Locking (Strict 2PL) for Writes**
   * Writers acquire exclusive (X) locks on keys and hold them until transaction completion (commit or abort).
   * Readers acquire shared (S) locks to serialize concurrent read/write access.
   * Sole shared-lock holders can upgrade to exclusive locks dynamically without queue re-entry.

3. **Deadlock Detection**
   * When a lock request blocks, a dependency edge is registered in a Waits-For graph.
   * A Depth-First Search (DFS) is performed to find cycles.
   * In the event of a cycle, the youngest transaction (highest ID) is chosen as the victim and aborted, minimizing wasted transactional computation.

4. **Vacuum Garbage Collection**
   * Recovers memory by pruning outdated, non-visible row versions.
   * Identifies the minimum active snapshot timestamp across all executing transactions.
   * Safely deletes any version whose invalidator has committed prior to that horizon.

---

## Building and Testing

I wrote the program to run in two modes so it is easy to check my work:
1. **Demo mode (default):** prints a full trace of each scenario, so the behaviour can be read step by step.
2. **Test mode (`--test` / `-t`):** runs the same scenarios as assertions, stays quiet, and returns exit code `0` on success or `1` on failure.

A small `Makefile` and a `run_tests.sh` script wrap the build, run, and test steps.

### Compilation & Running Demos

```bash
cd transaction-manager
make build     # Compiles using g++ -std=c++17
make run       # Runs the original interactive demo scenarios
```

### Running the Test Suite (Programmatic Assertions)

```bash
cd transaction-manager
make test      # Runs tests under programmatic assertion mode
```

### One command to build, test, and run

To do everything in a single step:

```bash
cd transaction-manager
./run_tests.sh
```

It produces output like this:
```
=== 1. Building Transaction Manager ===
g++ -std=c++17 -Wall -Wextra -pthread -O3 main.cpp -o txmgr

=== 2. Running Programmatic Test Suite (quiet mode with assertions) ===
g++ -std=c++17 -Wall -Wextra -pthread -O3 main.cpp -o txmgr
./txmgr --test
========================================
Running Transaction Manager Test Suite
========================================
Test 1: Snapshot Isolation ... PASS
Test 2: Shared Locks ... PASS
Test 3: Blocking / SI Snapshot Read ... PASS
Test 4: Lock Upgrade (S -> X) ... PASS
Test 5: Deadlock Detection ... PASS
Test 6: Lost Update Prevention (SI) ... PASS
Test 7: Vacuum GC version pruning ... PASS
----------------------------------------
Result: 7 / 7 passed.
========================================
[SUCCESS] All programmatic assertions passed!

=== 3. Running Demo Mode (detailed stdout verbose output) ===
[SUCCESS] Demo mode ran successfully!

==========================================================
  ALL CHECKS PASSED
==========================================================
```


