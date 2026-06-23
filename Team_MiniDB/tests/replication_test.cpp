#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

#include "catalog/catalog.h"
#include "query/executor.h"
#include "replication/network_util.h"
#include "replication/replication.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;

void RunOnPrimary(Executor &ex, PrimaryReplicator &repl, const std::string &sql) {
  Lexer lex(sql);
  Parser parser(lex.Tokenize());
  Statement stmt = parser.ParseStatement();
  QueryResult r = ex.Execute(stmt);
  assert(r.ok);
  if (stmt.kind != StmtKind::SELECT) repl.Broadcast(sql);
}

int main() {
  const char *primary_db = "test_repl_primary.db", *primary_meta = "test_repl_primary.meta";
  const char *replica_db = "test_repl_replica.db", *replica_meta = "test_repl_replica.meta";
  for (const char *f : {primary_db, replica_db}) {
    std::remove(f);
    std::remove((std::string(f) + ".wal").c_str());
  }
  std::remove(primary_meta);
  std::remove(replica_meta);

  const int port = 17891;

  DiskManager pdm(primary_db);
  BufferPool pbpm(64, &pdm);
  Catalog pcat(&pbpm, primary_meta);
  Executor pex(&pcat);
  PrimaryReplicator replicator;
  replicator.Start(port);

  DiskManager rdm(replica_db);
  BufferPool rbpm(64, &rdm);
  Catalog rcat(&rbpm, replica_meta);
  Executor rex(&rcat);
  ReplicaApplier applier;

  // Give the listener a brief moment to be ready, then connect.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  bool connected = applier.Connect("127.0.0.1", port);
  assert(connected);
  // Regression (QA Bug 5): IsConnected() must already be true right after
  // Connect() succeeds, not only after the first statement has arrived.
  assert(applier.IsConnected());

  std::thread apply_thread([&] { applier.RunLoop(&rex); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  assert(replicator.ReplicaCount() == 1);

  // Regression (QA Bug 1): a replica that disappears without a clean TCP
  // close (we just slam the fd shut, like a kill -9 would look from the
  // primary's side) must not bring the primary down with SIGPIPE on the
  // next write to that dead socket. Without IgnoreSigPipeOnce()/
  // MSG_NOSIGNAL, this would terminate this entire test process.
  {
    int rogue_fd = ConnectToHost("127.0.0.1", port);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    close(rogue_fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (int i = 0; i < 5; i++) replicator.Broadcast("SELECT 1");  // would SIGPIPE pre-fix
  }

  RunOnPrimary(pex, replicator, "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR)");
  RunOnPrimary(pex, replicator, "INSERT INTO users VALUES (1, 'alice')");
  RunOnPrimary(pex, replicator, "INSERT INTO users VALUES (2, 'bob')");

  // Replication is asynchronous - poll briefly for the replica to catch up
  // instead of a fixed sleep, demonstrating "read consistency" (eventual).
  for (int i = 0; i < 50 && applier.AppliedCount() < 3; i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  assert(applier.AppliedCount() == 3);

  {
    Lexer lex("SELECT * FROM users");
    Parser parser(lex.Tokenize());
    Statement stmt = parser.ParseStatement();
    QueryResult r = rex.Execute(stmt);
    assert(r.ok);
    assert(r.rows.size() == 2);
  }

  // --- Failure scenario: "kill" the primary by stopping its listener and
  // closing replica connections; the replica must keep serving reads from
  // its last-known state instead of crashing. ---
  replicator.Stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  assert(!applier.IsConnected());

  {
    Lexer lex("SELECT * FROM users WHERE id = 1");
    Parser parser(lex.Tokenize());
    Statement stmt = parser.ParseStatement();
    QueryResult r = rex.Execute(stmt);
    assert(r.ok);
    assert(r.rows.size() == 1);
    assert(r.rows[0][1] == "alice");
  }

  apply_thread.join();

  for (const char *f : {primary_db, replica_db}) {
    std::remove(f);
    std::remove((std::string(f) + ".wal").c_str());
  }
  std::remove(primary_meta);
  std::remove(replica_meta);

  std::cout << "replication_test PASSED (replica caught up, then survived primary going away)\n";
  return 0;
}
