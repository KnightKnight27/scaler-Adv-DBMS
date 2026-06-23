// Benchmark for Track D: measures replication lag - the time between a
// statement committing on the primary and the replica having applied it -
// across batches of writes, and reports the replica's read throughput
// once caught up (no lock contention with a writer, since replicas are
// read-only).
#include <chrono>
#include <cstdio>
#include <thread>

#include "catalog/catalog.h"
#include "query/executor.h"
#include "replication/replication.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;
using Clock = std::chrono::steady_clock;

void RunOnPrimary(Executor &ex, PrimaryReplicator &repl, const std::string &sql) {
  Lexer lex(sql);
  Parser parser(lex.Tokenize());
  Statement stmt = parser.ParseStatement();
  ex.Execute(stmt);
  if (stmt.kind != StmtKind::SELECT) repl.Broadcast(sql);
}

int main() {
  const char *pdb = "bench_repl_primary.db", *pmeta = "bench_repl_primary.meta";
  const char *rdb = "bench_repl_replica.db", *rmeta = "bench_repl_replica.meta";
  for (auto f : {pdb, rdb}) { std::remove(f); std::remove((std::string(f) + ".wal").c_str()); }
  std::remove(pmeta);
  std::remove(rmeta);

  const int port = 17999;
  const int n_writes = 500;

  DiskManager pdm(pdb);
  BufferPool pbpm(128, &pdm);
  Catalog pcat(&pbpm, pmeta);
  Executor pex(&pcat);
  PrimaryReplicator replicator;
  replicator.Start(port);

  DiskManager rdm(rdb);
  BufferPool rbpm(128, &rdm);
  Catalog rcat(&rbpm, rmeta);
  Executor rex(&rcat);
  ReplicaApplier applier;

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  applier.Connect("127.0.0.1", port);
  std::thread apply_thread([&] { applier.RunLoop(&rex); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  RunOnPrimary(pex, replicator, "CREATE TABLE t (id INT PRIMARY KEY, v VARCHAR)");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto t0 = Clock::now();
  for (int i = 0; i < n_writes; i++) RunOnPrimary(pex, replicator, "INSERT INTO t VALUES (" + std::to_string(i) + ", 'x')");
  auto t_primary_done = Clock::now();

  while (applier.AppliedCount() < n_writes + 1) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  auto t_replica_caught_up = Clock::now();

  double primary_ms = std::chrono::duration<double, std::milli>(t_primary_done - t0).count();
  double lag_ms = std::chrono::duration<double, std::milli>(t_replica_caught_up - t_primary_done).count();
  printf("Primary applied %d writes in %.2fms (%.1f writes/sec)\n", n_writes, primary_ms, n_writes / (primary_ms / 1000.0));
  printf("Replica caught up %.2fms after the primary's last write (replication lag)\n", lag_ms);

  replicator.Stop();
  apply_thread.join();

  for (auto f : {pdb, rdb}) { std::remove(f); std::remove((std::string(f) + ".wal").c_str()); }
  std::remove(pmeta);
  std::remove(rmeta);
  return 0;
}
