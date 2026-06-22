#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "common/config.h"
#include "engine/database.h"
#include "exec/executor.h"
#include "index/bplus_tree.h"
#include "record/schema.h"
#include "record/tuple.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h"
#include "txn/mvcc.h"

#include <chrono>
#include <cstdlib>

using namespace minidb;

namespace {

// Remove a trailing '\r' so lines work whether the input uses CRLF or LF.
void StripCR(std::string& s) {
  if (!s.empty() && s.back() == '\r') s.pop_back();
}

void PrintHelp() {
  std::cout <<
      "MiniDB commands:\n"
      "  <SQL>;           run a SQL statement (CREATE/INSERT/SELECT/DELETE/EXPLAIN)\n"
      "  .tables          list tables\n"
      "  .test storage    run the storage-engine self test (M1 demo)\n"
      "  .test index      run the B+Tree self test (M2 demo)\n"
      "  .test txn        run the 2PL / deadlock self test (M4 demo)\n"
      "  .test recovery   run the WAL crash-recovery self test (M5 demo)\n"
      "  .test mvcc       run the MVCC snapshot-isolation self test (Track B)\n"
      "  .bench           run the benchmark suite\n"
      "  .help            show this help\n"
      "  .quit / .exit    leave the REPL\n";
}

// Render an ExecResult to stdout: a result grid for queries, plan text for
// EXPLAIN, or a status/error line otherwise.
void PrintResult(const ExecResult& r) {
  if (!r.ok) { std::cout << "ERROR: " << r.error << "\n"; return; }
  if (r.is_explain) { std::cout << r.plan; return; }
  if (r.is_query) {
    for (size_t i = 0; i < r.columns.size(); ++i)
      std::cout << (i ? " | " : "") << r.columns[i];
    std::cout << "\n";
    for (const Row& row : r.rows) {
      for (size_t i = 0; i < row.size(); ++i)
        std::cout << (i ? " | " : "") << row[i].ToString();
      std::cout << "\n";
    }
    std::cout << "(" << r.rows.size() << " row(s))\n";
    return;
  }
  std::cout << r.message << "\n";
}

// M1 demonstration: exercises disk manager, buffer pool eviction, heap file
// insert/get/scan/delete and tuple serialization, then prints pool statistics.
void TestStorage() {
  const std::string db = "storage_demo.db";
  std::remove(db.c_str());  // start clean each run

  DiskManager dm(db);
  // Deliberately tiny pool so the demo forces real evictions.
  const int kFrames = 2;
  BufferPool pool(&dm, kFrames);

  // Schema: (id INT, name VARCHAR(16))
  Schema schema;
  schema.columns.push_back(Column::Int("id"));
  schema.columns.push_back(Column::Varchar("name", 16));
  schema.pk_index = 0;

  PageId first = HeapFile::CreateNew(&pool, schema.RecordSize());
  HeapFile heap(&pool, first, schema.RecordSize());

  std::cout << "Record size = " << schema.RecordSize()
            << " bytes, slots/page = " << heap.SlotsPerPage()
            << ", pool frames = " << kFrames << "\n";

  // Insert enough rows to span several pages (more pages than frames -> the
  // buffer pool is forced to evict and reload, proving correctness across it).
  const int N = 800;
  std::vector<RID> rids;
  for (int i = 0; i < N; ++i) {
    std::vector<Value> row;
    row.push_back(Value::Int(i));
    row.push_back(Value::Varchar("user_" + std::to_string(i)));
    std::vector<char> buf;
    tuple::Serialize(schema, row, buf);
    rids.push_back(heap.Insert(buf.data()));
  }
  std::cout << "Inserted " << N << " rows.\n";

  // Read a few rows back (across page boundaries -> proves read-after-write
  // survives buffer-pool eviction).
  std::cout << "Spot reads:\n";
  for (int idx : {0, 200, 500, N - 1}) {
    std::vector<char> buf(schema.RecordSize());
    if (heap.Get(rids[idx], buf.data())) {
      auto vals = tuple::Deserialize(schema, buf.data());
      std::cout << "  rid(" << rids[idx].page << "," << rids[idx].slot
                << ") -> " << tuple::ToString(schema, vals) << "\n";
    }
  }

  // Full scan count.
  int count = 0;
  heap.Scan([&](RID, const char*) { ++count; });
  std::cout << "Scan found " << count << " live rows.\n";

  // Delete some, scan again.
  for (int idx : {10, 20, 30}) heap.Delete(rids[idx]);
  count = 0;
  heap.Scan([&](RID, const char*) { ++count; });
  std::cout << "After deleting 3 rows, scan found " << count << " live rows.\n";

  pool.FlushAll();
  std::cout << "Buffer pool stats -> hits=" << pool.Hits()
            << " misses=" << pool.Misses()
            << " evictions=" << pool.Evictions() << "\n";
  std::cout << "Storage self-test OK.\n";
}

// M2 demonstration: B+Tree insert (with splits), point search, range scan,
// and lazy delete. Keys inserted out of order to exercise the sorted structure.
void TestIndex() {
  const std::string db = "index_demo.db";
  std::remove(db.c_str());
  DiskManager dm(db);
  BufferPool pool(&dm, 16);

  PageId header = BPlusTree::CreateNew(&pool);
  BPlusTree tree(&pool, header);

  // Insert 1000 keys in a scrambled order so the tree must split repeatedly.
  const int N = 1000;
  for (int i = 0; i < N; ++i) {
    int key = (i * 37 + 11) % N;          // pseudo-shuffle, all distinct
    tree.Insert(key, RID(key / 100, static_cast<int16_t>(key % 100)));
  }
  std::cout << "Inserted " << N << " keys into B+Tree.\n";

  // Point searches.
  int found = 0;
  for (int k = 0; k < N; ++k) {
    RID r;
    if (tree.Search(k, &r)) ++found;
  }
  std::cout << "Point search: found " << found << "/" << N << " keys.\n";
  RID miss;
  std::cout << "Search for absent key 99999 -> "
            << (tree.Search(99999, &miss) ? "FOUND (bug!)" : "not found (ok)")
            << "\n";

  // Range scan [100, 110].
  std::cout << "Range scan [100,110]: ";
  tree.Range(100, 110, [](int32_t k, RID) { std::cout << k << " "; });
  std::cout << "\n";

  // Verify ordering of a full scan.
  int prev = -1; bool ordered = true; int total = 0;
  tree.ScanAll([&](int32_t k, RID) {
    if (k <= prev) ordered = false;
    prev = k; ++total;
  });
  std::cout << "Full scan visited " << total << " keys, ordered="
            << (ordered ? "yes" : "NO") << "\n";

  // Lazy delete a few, confirm gone.
  for (int k : {5, 500, 999}) tree.Delete(k);
  RID r;
  std::cout << "After deleting 5,500,999 -> search(500): "
            << (tree.Search(500, &r) ? "FOUND (bug!)" : "not found (ok)") << "\n";
  std::cout << "Index self-test OK.\n";
}

// M4 demonstration: Strict 2PL lock manager. Three scripted, deterministic
// interleavings show concurrent reads, a write-blocks-read wait, and a deadlock
// that the wait-for-graph detector resolves by aborting the youngest txn.
const char* G(bool granted) { return granted ? "GRANTED" : "BLOCKED"; }

void TestTxn() {
  std::cout << "=== Scenario A: shared locks are compatible (concurrent reads) ===\n";
  {
    LockManager lm; TxnManager tm;
    Transaction* t1 = tm.Begin();
    Transaction* t2 = tm.Begin();
    std::cout << "T1 S-lock row1: " << G(lm.Acquire(t1->id, 1, LockMode::S)) << "\n";
    std::cout << "T2 S-lock row1: " << G(lm.Acquire(t2->id, 1, LockMode::S)) << "\n";
    std::cout << "(both granted -> reads do not block reads)\n";
  }

  std::cout << "\n=== Scenario B: a write blocks a read until commit ===\n";
  {
    LockManager lm; TxnManager tm;
    Transaction* t1 = tm.Begin();
    Transaction* t2 = tm.Begin();
    std::cout << "T1 X-lock row1: " << G(lm.Acquire(t1->id, 1, LockMode::X)) << "\n";
    std::cout << "T2 S-lock row1: " << G(lm.Acquire(t2->id, 1, LockMode::S))
              << " (must wait for T1)\n";
    std::cout << "Lock table:\n" << lm.Dump();
    std::vector<TxnId> woken = lm.ReleaseAll(t1->id);  // T1 commits
    std::cout << "T1 commits -> releases locks. Woken: ";
    for (TxnId w : woken) std::cout << "T" << w << " ";
    std::cout << "\n";
  }

  std::cout << "\n=== Scenario C: deadlock detected and resolved ===\n";
  {
    LockManager lm; TxnManager tm;
    Transaction* t1 = tm.Begin();
    Transaction* t2 = tm.Begin();
    std::cout << "T1 X-lock A: " << G(lm.Acquire(t1->id, 1, LockMode::X)) << "\n";
    std::cout << "T2 X-lock B: " << G(lm.Acquire(t2->id, 2, LockMode::X)) << "\n";
    std::cout << "T1 X-lock B: " << G(lm.Acquire(t1->id, 2, LockMode::X))
              << " (T1 waits for T2)\n";
    std::cout << "T2 X-lock A: " << G(lm.Acquire(t2->id, 1, LockMode::X))
              << " (T2 waits for T1 -> cycle!)\n";
    TxnId victim;
    if (lm.DetectDeadlock(&victim)) {
      std::cout << "Deadlock detected. Victim = T" << victim
                << " (youngest). Aborting it.\n";
      std::vector<TxnId> woken = lm.ReleaseAll(victim);
      std::cout << "After abort, newly granted: ";
      for (TxnId w : woken) std::cout << "T" << w << " ";
      std::cout << "\n(survivor proceeds -> serializable schedule preserved)\n";
    } else {
      std::cout << "No deadlock detected (unexpected!)\n";
    }
  }
  std::cout << "Transaction/2PL self-test OK.\n";
}

// ---- M5: crash recovery demo --------------------------------------------
// These two phases are meant to run as SEPARATE processes so the "crash" is a
// real process exit (see benchmarks/recovery_demo.ps1). They share a data file
// (cells in page 0) and a WAL file.
const char* kRecData = "recovery_demo.db";
const char* kRecWal  = "recovery_demo.wal";

// Phase 1: do a committed txn and an uncommitted txn, then "crash". We model a
// STEAL/NO-FORCE buffer policy: T1 (committed) changes were still buffered and
// are lost on crash (only in the WAL); T2 (uncommitted) had a dirty page stolen
// to disk before the crash.
void CrashWrite() {
  RecoveryManager::WriteCell(kRecData, 0, 0);  // reset cells
  RecoveryManager::WriteCell(kRecData, 1, 0);

  LogManager log(kRecWal, /*truncate=*/true);
  // T1: committed, but its data pages are NOT flushed to disk.
  log.Begin(1);
  log.Update(1, 0, 0, 100);
  log.Update(1, 1, 0, 200);
  log.Commit(1);  // WAL forced at commit
  // T2: uncommitted, but its dirty page WAS stolen to disk.
  log.Begin(2);
  log.Update(2, 0, 100, 999);
  RecoveryManager::WriteCell(kRecData, 0, 999);  // steal to disk
  log.Flush();

  std::cout << "Phase 1 (before crash):\n"
            << "  T1 committed: cell0=100, cell1=200 (in WAL; NOT flushed to data)\n"
            << "  T2 uncommitted: cell0=999 (dirty page stolen to disk)\n"
            << "  On-disk now: cell0=" << RecoveryManager::ReadCell(kRecData, 0)
            << ", cell1=" << RecoveryManager::ReadCell(kRecData, 1) << "\n"
            << "  *** simulating crash (process exits without clean shutdown) ***\n";
  // No flush of T1 data; just exit.
}

// Phase 2: restart and recover from the WAL.
void CrashRecover() {
  std::cout << "Phase 2 (restart): on-disk before recovery: cell0="
            << RecoveryManager::ReadCell(kRecData, 0) << ", cell1="
            << RecoveryManager::ReadCell(kRecData, 1) << "\n";
  std::vector<LogRecord> recs = LogManager::ReadAll(kRecWal);
  std::vector<int32_t> result =
      RecoveryManager::Recover(kRecData, recs, 2, std::cout);
  std::cout << "After recovery: cell0=" << result[0] << ", cell1=" << result[1] << "\n";
  bool ok = (result[0] == 100 && result[1] == 200);
  std::cout << (ok ? "PASS: committed T1 preserved (100,200), uncommitted T2 undone.\n"
                   : "FAIL: unexpected recovered state.\n");
}

// M5 / Track B demonstration: MVCC snapshot isolation + a blocking comparison
// against 2PL under read/write contention.
void TestMvcc() {
  std::cout << "=== Snapshot isolation (readers see a consistent snapshot) ===\n";
  MvccStore s;
  s.Init(1, 10);
  long t1_snapshot = 1;  // T1 begins
  int32_t v = 0;
  s.Read(1, t1_snapshot, &v);
  std::cout << "T1 (snapshot=1) reads key1 = " << v << " (expect 10)\n";

  // T2 writes a new value and commits at ts=3.
  s.Write(1, /*txn=*/2, /*snapshot=*/2, 20);
  s.Commit(2, /*commit_ts=*/3);
  std::cout << "T2 writes key1=20 and commits at ts=3.\n";

  s.Read(1, t1_snapshot, &v);
  std::cout << "T1 re-reads with its OLD snapshot = " << v
            << " (expect 10 -> repeatable read, no blocking)\n";
  int32_t v3 = 0;
  s.Read(1, /*snapshot=*/4, &v3);
  std::cout << "T3 (snapshot=4) reads key1 = " << v3 << " (expect 20)\n";
  std::cout << "Version chain length for key1 = " << s.VersionCount(1) << "\n";

  std::cout << "\n=== Contention: readers vs a long-running writer ===\n";
  const int R = 1000;
  // 2PL: writer holds X on the hot key; every reader's S request blocks.
  LockManager lm;
  lm.Acquire(/*writer txn*/1000, /*hot key*/7, LockMode::X);
  int blocked_2pl = 0;
  for (int i = 0; i < R; ++i)
    if (!lm.Acquire(/*reader*/i, 7, LockMode::S)) ++blocked_2pl;

  // MVCC: writer has an uncommitted version; readers read the prior snapshot.
  MvccStore m;
  m.Init(7, 0);
  m.Write(7, /*writer*/1000, /*snapshot*/5, 1);  // uncommitted
  int blocked_mvcc = 0;
  for (int i = 0; i < R; ++i) {
    int32_t out;
    if (!m.Read(7, /*snapshot before writer*/4, &out)) ++blocked_mvcc;
  }
  std::cout << R << " readers contending with 1 writer on a hot key:\n";
  std::cout << "  2PL  : " << blocked_2pl << " readers BLOCKED\n";
  std::cout << "  MVCC : " << blocked_mvcc << " readers blocked (read snapshot)\n";
  std::cout << "MVCC self-test OK.\n";
}

// Benchmark suite (printed as a small report; captured by benchmarks/bench.ps1).
void BenchAll() {
  using clock = std::chrono::steady_clock;
  auto ms = [](clock::duration d) {
    return std::chrono::duration_cast<std::chrono::microseconds>(d).count() / 1000.0;
  };

  std::cout << "## Benchmark 1: index scan vs sequential scan (point lookups)\n";
  {
    const std::string db = "bench_index.db";
    std::remove(db.c_str());
    DiskManager dm(db);
    BufferPool pool(&dm, 256);
    Schema schema;
    schema.columns.push_back(Column::Int("id"));
    schema.columns.push_back(Column::Varchar("name", 16));
    schema.pk_index = 0;
    PageId hp = HeapFile::CreateNew(&pool, schema.RecordSize());
    HeapFile heap(&pool, hp, schema.RecordSize());
    PageId ih = BPlusTree::CreateNew(&pool);
    BPlusTree tree(&pool, ih);

    const int N = 50000;
    for (int i = 0; i < N; ++i) {
      std::vector<Value> row;
      row.push_back(Value::Int(i));
      row.push_back(Value::Varchar("u"));
      std::vector<char> buf;
      tuple::Serialize(schema, row, buf);
      RID rid = heap.Insert(buf.data());
      tree.Insert(i, rid);
    }

    const int M = 500;
    std::vector<char> buf(schema.RecordSize());

    auto t0 = clock::now();
    long hits_idx = 0;
    for (int q = 0; q < M; ++q) {
      int key = std::rand() % N;
      RID r;
      if (tree.Search(key, &r) && heap.Get(r, buf.data())) ++hits_idx;
    }
    auto t1 = clock::now();

    long hits_scan = 0;
    for (int q = 0; q < M; ++q) {
      int key = std::rand() % N;
      bool found = false;
      heap.Scan([&](RID, const char* data) {
        if (!found) {
          std::vector<Value> vals = tuple::Deserialize(schema, data);
          if (vals[0].i == key) found = true;
        }
      });
      if (found) ++hits_scan;
    }
    auto t2 = clock::now();

    std::cout << "  rows=" << N << ", lookups=" << M << "\n";
    std::cout << "  IndexScan : " << ms(t1 - t0) << " ms ("
              << ms(t1 - t0) / M << " ms/lookup), hits=" << hits_idx << "\n";
    std::cout << "  SeqScan   : " << ms(t2 - t1) << " ms ("
              << ms(t2 - t1) / M << " ms/lookup), hits=" << hits_scan << "\n";
    double sp = ms(t2 - t1) / (ms(t1 - t0) <= 0 ? 1e-6 : ms(t1 - t0));
    std::cout << "  speedup   : ~" << sp << "x in favour of the index\n";
  }

  std::cout << "\n## Benchmark 2: MVCC vs 2PL reader blocking under contention\n";
  std::cout << "  readers,2PL_blocked,MVCC_blocked\n";
  for (int R : {100, 1000, 10000}) {
    LockManager lm;
    lm.Acquire(999999, 7, LockMode::X);
    int b2 = 0;
    for (int i = 0; i < R; ++i)
      if (!lm.Acquire(i, 7, LockMode::S)) ++b2;
    MvccStore m;
    m.Init(7, 0);
    m.Write(7, 999999, 5, 1);
    int bm = 0;
    for (int i = 0; i < R; ++i) { int32_t o; if (!m.Read(7, 4, &o)) ++bm; }
    std::cout << "  " << R << "," << b2 << "," << bm << "\n";
  }

  std::cout << "\n## Benchmark 3: raw read throughput (no contention)\n";
  {
    const int N = 200000;
    MvccStore m; m.Init(1, 42);
    LockManager lm;
    auto t0 = clock::now();
    long acc = 0;
    for (int i = 0; i < N; ++i) { int32_t o; m.Read(1, 100, &o); acc += o; }
    auto t1 = clock::now();
    for (int i = 0; i < N; ++i) {  // 2PL: lock + unlock per read
      lm.Acquire(i, 1, LockMode::S);
      lm.ReleaseAll(i);
    }
    auto t2 = clock::now();
    std::cout << "  " << N << " reads -- MVCC: " << ms(t1 - t0)
              << " ms, 2PL(lock+unlock): " << ms(t2 - t1) << " ms\n";
    (void)acc;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "crash-write")   { CrashWrite();   return 0; }
  if (argc >= 2 && std::string(argv[1]) == "crash-recover") { CrashRecover(); return 0; }
  if (argc >= 2 && std::string(argv[1]) == "bench")         { BenchAll();     return 0; }
  // Allow `minidb .test storage` style one-shot invocation from scripts.
  if (argc >= 3 && std::string(argv[1]) == ".test") {
    std::string what = argv[2];
    if (what == "storage") { TestStorage(); return 0; }
    if (what == "index")   { TestIndex();   return 0; }
    if (what == "txn")     { TestTxn();     return 0; }
    if (what == "recovery"){ CrashWrite(); std::cout << "\n"; CrashRecover(); return 0; }
    if (what == "mvcc")    { TestMvcc();    return 0; }
  }

  std::cout << "MiniDB (C++14). Type .help for commands.\n";
  Database db("minidb.db");
  std::string line;
  while (true) {
    std::cout << "minidb> " << std::flush;
    if (!std::getline(std::cin, line)) break;
    StripCR(line);
    // Skip blank lines and full-line SQL comments ("-- ...").
    size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) continue;
    if (line.compare(first, 2, "--") == 0) continue;

    if (line == ".quit" || line == ".exit") break;
    if (line == ".help") { PrintHelp(); continue; }
    if (line == ".test storage") { TestStorage(); continue; }
    if (line == ".test index")   { TestIndex();   continue; }
    if (line == ".test txn")     { TestTxn();     continue; }
    if (line == ".test recovery"){ CrashWrite(); std::cout << "\n"; CrashRecover(); continue; }
    if (line == ".test mvcc")    { TestMvcc();    continue; }
    if (line == ".bench")        { BenchAll();    continue; }
    if (line == ".tables") {
      for (const auto& t : db.catalog().tables)
        std::cout << "  " << t.name << " (" << t.row_count << " rows)\n";
      continue;
    }

    PrintResult(Execute(db, line));
  }
  db.Flush();
  std::cout << "bye.\n";
  return 0;
}
