#include "catch.hpp"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "exec/executor.h"
#include "parser/parser.h"
#include "recovery/log_manager.h"
#include "storage/disk_storage_manager.h"
#include "txn/concurrency_lock_manager.h"
#include "txn/transaction.h"
#include "txn/txn_manager.h"

using namespace axiomdb;
using namespace std::chrono_literals;

namespace {
struct Stack {
  DiskStorageManager disk;
  BufferPoolManager pool;
  LogManager wal;
  Catalog catalog;
  ConcurrencyLockManager lock_mgr;
  TxnManager txn_mgr;
  Executor exec;

  explicit Stack(const std::string& base)
      : disk(base + ".wdb"),
        pool(&disk),
        wal(base + ".wal"),
        catalog(&pool, base + ".catalog"),
        txn_mgr(&lock_mgr, &wal, &catalog),
        exec(catalog) {}

  void ddl(const std::string& sql) {
    auto p = parse_sql(sql);
    exec.execute(p.statement.get(), nullptr);
  }
  ExecResult run(Transaction* txn, const std::string& sql) {
    auto p = parse_sql(sql);
    ExecContext ctx{txn, &lock_mgr, &wal};
    return exec.execute(p.statement.get(), &ctx);
  }
};

std::string base_for(const char* tag) {
  return std::string("/tmp/axiomdb_conc_") + tag + "_" + std::to_string(::getpid());
}
void cleanup(const std::string& base) {
  ::remove((base + ".wdb").c_str());
  ::remove((base + ".catalog").c_str());
  ::remove((base + ".wal").c_str());
}
}  // namespace

TEST_CASE("a writer blocks on another transaction's lock until it commits", "[concurrency]") {
  std::string base = base_for("block");
  cleanup(base);
  Stack s(base);
  s.ddl("CREATE TABLE a (id INT PRIMARY KEY, v INT)");

  auto t1 = s.txn_mgr.begin();
  REQUIRE(s.run(t1.get(), "INSERT INTO a VALUES (1,10)").ok);  // t1 holds X on table a

  auto t2 = s.txn_mgr.begin();
  std::atomic<bool> t2_done{false};
  std::thread th([&] {
    s.run(t2.get(), "INSERT INTO a VALUES (2,20)");  // must block behind t1
    t2_done = true;
  });

  std::this_thread::sleep_for(60ms);
  REQUIRE_FALSE(t2_done.load());  // still blocked

  s.txn_mgr.commit(t1.get());     // release the lock
  th.join();
  REQUIRE(t2_done.load());
  s.txn_mgr.commit(t2.get());
  cleanup(base);
}

TEST_CASE("two transactions deadlock; exactly one is aborted as victim", "[concurrency][deadlock]") {
  std::string base = base_for("deadlock");
  cleanup(base);
  Stack s(base);
  s.ddl("CREATE TABLE a (id INT PRIMARY KEY, v INT)");
  s.ddl("CREATE TABLE b (id INT PRIMARY KEY, v INT)");

  auto t1 = s.txn_mgr.begin();
  auto t2 = s.txn_mgr.begin();
  REQUIRE(s.run(t1.get(), "INSERT INTO a VALUES (10,1)").ok);  // t1: X on a
  REQUIRE(s.run(t2.get(), "INSERT INTO b VALUES (20,1)").ok);  // t2: X on b

  std::atomic<int> victims{0}, winners{0};
  std::thread th1([&] {
    auto r = s.run(t1.get(), "INSERT INTO b VALUES (11,1)");   // t1 wants b -> waits on t2
    if (r.ok) ++winners; else { ++victims; s.txn_mgr.abort(t1.get()); }
  });
  std::thread th2([&] {
    auto r = s.run(t2.get(), "INSERT INTO a VALUES (21,1)");   // t2 wants a -> waits on t1 -> cycle
    if (r.ok) ++winners; else { ++victims; s.txn_mgr.abort(t2.get()); }
  });
  th1.join();
  th2.join();

  REQUIRE(victims.load() == 1);  // the cycle is broken by aborting one txn
  REQUIRE(winners.load() == 1);  // the other completes

  if (t1->state() != TxnState::Aborted) s.txn_mgr.commit(t1.get());
  if (t2->state() != TxnState::Aborted) s.txn_mgr.commit(t2.get());
  cleanup(base);
}
