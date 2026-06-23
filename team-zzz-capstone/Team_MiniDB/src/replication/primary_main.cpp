#include <iostream>
#include <sstream>

#include "../catalog/catalog.h"
#include "../query/executor.h"
#include "../sql/lexer.h"
#include "../sql/parser.h"
#include "../storage/buffer_pool.h"
#include "../storage/disk_manager.h"
#include "replication.h"

using namespace minidb;

// Usage: minidb_primary <db_file> <port>
// Runs an interactive SQL REPL exactly like minidb, plus: every DDL/DML
// statement that succeeds is broadcast to all connected replicas so they
// can apply it to their own copy of the database.
int main(int argc, char **argv) {
  std::string db_file = argc > 1 ? argv[1] : "primary.db";
  int port = argc > 2 ? std::atoi(argv[2]) : 6000;

  DiskManager dm(db_file);
  BufferPool bpm(128, &dm);
  Catalog catalog(&bpm, db_file + ".meta");
  Executor executor(&catalog);

  PrimaryReplicator replicator;
  replicator.Start(port);
  std::cout << "MiniDB PRIMARY listening for replicas on port " << port << "\n";
  std::cout << "db file: " << db_file << "  (type SQL statements; 'quit' to exit)\n";

  std::string line;
  while (true) {
    std::cout << "primary> ";
    if (!std::getline(std::cin, line)) break;
    if (line == "quit" || line == "exit") break;
    if (line.empty()) continue;

    try {
      Lexer lex(line);
      Parser parser(lex.Tokenize());
      Statement stmt = parser.ParseStatement();
      QueryResult r = executor.Execute(stmt);
      std::cout << (r.ok ? "OK: " : "ERROR: ") << r.message << "\n";
      if (r.ok && stmt.kind != StmtKind::SELECT) {
        replicator.Broadcast(line);
        std::cout << "  (replicated to " << replicator.ReplicaCount() << " replica(s))\n";
      } else if (stmt.kind == StmtKind::SELECT) {
        for (auto &row : r.rows) {
          for (size_t i = 0; i < row.size(); i++) std::cout << row[i] << (i + 1 < row.size() ? " | " : "\n");
        }
      }
    } catch (const std::exception &e) {
      std::cout << "ERROR: " << e.what() << "\n";
    }
  }
  return 0;
}
