#include <iostream>
#include <thread>

#include "../catalog/catalog.h"
#include "../query/executor.h"
#include "../sql/lexer.h"
#include "../sql/parser.h"
#include "../storage/buffer_pool.h"
#include "../storage/disk_manager.h"
#include "replication.h"

using namespace minidb;

// Usage: minidb_replica <db_file> <primary_host> <primary_port>
// Connects to a primary and applies its replicated statement stream on a
// background thread, while the foreground thread serves read-only SQL
// queries against the (possibly slightly lagging) local copy - this is
// what "read consistency" and "failure scenario" mean for this track: a
// SELECT issued here reflects whatever the replica has applied so far,
// and continues to work using last-known data if the primary disappears.
int main(int argc, char **argv) {
  std::string db_file = argc > 1 ? argv[1] : "replica.db";
  std::string host = argc > 2 ? argv[2] : "127.0.0.1";
  int port = argc > 3 ? std::atoi(argv[3]) : 6000;

  DiskManager dm(db_file);
  BufferPool bpm(128, &dm);
  Catalog catalog(&bpm, db_file + ".meta");
  Executor executor(&catalog);

  ReplicaApplier applier;
  bool connected = applier.Connect(host, port);
  if (!connected) {
    std::cout << "Could not connect to primary at " << host << ":" << port
              << " - serving reads from last-known local state.\n";
  } else {
    std::cout << "Connected to primary at " << host << ":" << port << ". Replicating...\n";
  }

  std::thread apply_thread;
  if (connected) apply_thread = std::thread([&] { applier.RunLoop(&executor); });

  std::cout << "MiniDB REPLICA (read-only REPL). db file: " << db_file << "  ('quit' to exit, 'status' for replication state)\n";
  std::string line;
  while (true) {
    std::cout << "replica> ";
    if (!std::getline(std::cin, line)) break;
    if (line == "quit" || line == "exit") break;
    if (line.empty()) continue;
    if (line == "status") {
      std::cout << (applier.IsConnected() ? "CONNECTED to primary" : "DISCONNECTED from primary (stale reads)")
                << " - " << applier.AppliedCount() << " statement(s) applied\n";
      continue;
    }
    try {
      Lexer lex(line);
      Parser parser(lex.Tokenize());
      Statement stmt = parser.ParseStatement();
      if (stmt.kind != StmtKind::SELECT) {
        std::cout << "ERROR: replicas are read-only; write to the primary instead\n";
        continue;
      }
      QueryResult r = executor.Execute(stmt);
      for (auto &row : r.rows) {
        for (size_t i = 0; i < row.size(); i++) std::cout << row[i] << (i + 1 < row.size() ? " | " : "\n");
      }
      std::cout << r.message << "\n";
    } catch (const std::exception &e) {
      std::cout << "ERROR: " << e.what() << "\n";
    }
  }
  if (apply_thread.joinable()) {
    // RunLoop blocks on recv(); closing isn't strictly needed for a CLI
    // exit, the process teardown reclaims the socket.
    apply_thread.detach();
  }
  return 0;
}
