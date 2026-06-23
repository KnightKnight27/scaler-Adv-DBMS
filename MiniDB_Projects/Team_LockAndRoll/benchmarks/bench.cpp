// concurrency benchmark: 2PL vs MVCC.
// workload is long read-only scans next to point writers — where MVCC wins
// because its readers don't block on the writers' row locks. fsync-on-commit is
// off so we measure concurrency-control cost, not disk latency.
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "engine.h"

using namespace minidb;
using Clock = std::chrono::steady_clock;

namespace {

struct Result {
  double seconds = 0;
  long reads = 0;
  long writes = 0;
  long aborts = 0;
};

Result run_workload(CCMode mode, const std::string& dir, int rows, int readers,
                    int writers, int reads_each, int writes_each) {
  Database db(dir, mode, 1024);
  db.set_durable(false);  // isolate CC cost from fsync latency
  db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER)");
  {
    std::string ins = "INSERT INTO t VALUES ";
    for (int i = 0; i < rows; i++)
      ins += (i ? "," : "") + std::string("(") + std::to_string(i) + ",0)";
    db.execute(ins);
  }
  TableInfo* t = db.catalog().get_table("t");
  std::atomic<long> aborts{0};

  auto reader = [&]() {
    for (int i = 0; i < reads_each; i++) {
      Transaction* txn = db.begin(true);
      long n = 0;
      db.scan_table(txn, t, [&](RID, const Tuple&) { n++; return true; });
      db.commit(txn);
      (void)n;
    }
  };
  auto writer = [&](int seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> key_dist(0, rows - 1);
    long local_aborts = 0;
    for (int i = 0; i < writes_each; i++) {
      for (;;) {
        Transaction* txn = db.begin(true);
        try {
          int k = key_dist(rng);
          db.delete_row(txn, t, k);
          db.insert_row(txn, t, Tuple({Value((int64_t)k), Value((int64_t)i)}));
          db.commit(txn);
          break;
        } catch (const AbortException&) {
          db.abort(txn);
          local_aborts++;
        }
      }
    }
    aborts += local_aborts;
  };

  auto start = Clock::now();
  std::vector<std::thread> pool;
  for (int i = 0; i < readers; i++) pool.emplace_back(reader);
  for (int i = 0; i < writers; i++) pool.emplace_back(writer, i + 100);
  for (auto& th : pool) th.join();
  double secs = std::chrono::duration<double>(Clock::now() - start).count();
  return {secs, (long)readers * reads_each, (long)writers * writes_each, aborts.load()};
}

void report(const char* name, const Result& r) {
  double read_tps = r.reads / r.seconds;
  double write_tps = r.writes / r.seconds;
  std::cout << "  " << name << ": " << r.seconds << "s | reads " << r.reads << " ("
            << (long)read_tps << "/s) | writes " << r.writes << " (" << (long)write_tps
            << "/s) | aborts " << r.aborts << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  int rows = 500, readers = 6, writers = 2, reads_each = 400, writes_each = 4000;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto val = [&]() { return std::stoi(argv[++i]); };
    if (a == "--rows") rows = val();
    else if (a == "--readers") readers = val();
    else if (a == "--writers") writers = val();
    else if (a == "--reads-each") reads_each = val();
    else if (a == "--writes-each") writes_each = val();
  }

  std::cout << "MiniDB concurrency benchmark (long readers + point writers)\n"
            << "  rows=" << rows << " readers=" << readers << " writers=" << writers
            << " reads/reader=" << reads_each << " writes/writer=" << writes_each << "\n\n";

  Result tpl = run_workload(CCMode::TWO_PL, "bench_2pl", rows, readers, writers, reads_each,
                            writes_each);
  Result mvcc = run_workload(CCMode::MVCC, "bench_mvcc", rows, readers, writers, reads_each,
                             writes_each);
  std::cout << "Results:\n";
  report("2PL ", tpl);
  report("MVCC", mvcc);
  double total_2pl = (tpl.reads + tpl.writes) / tpl.seconds;
  double total_mvcc = (mvcc.reads + mvcc.writes) / mvcc.seconds;
  std::cout << "\n  Total throughput  2PL: " << (long)total_2pl << " txn/s,  MVCC: "
            << (long)total_mvcc << " txn/s\n";
  std::cout << "  MVCC speedup over 2PL: " << (total_mvcc / total_2pl) << "x\n";
  return 0;
}
