// ============================================================================
// main.cpp  --  The MiniDB REPL (Read-Eval-Print Loop).
//
// Reads SQL statements (terminated by ';'), runs them, and prints results.  It
// also manages the *session transaction*: BEGIN starts one, COMMIT/ROLLBACK end
// it, and any statement outside BEGIN runs in its own auto-commit transaction.
//
// Meta-commands (start with '.'):
//   .tables          list tables
//   .crash           simulate a power failure (exit WITHOUT flushing) -- used to
//                    demonstrate crash recovery on the next startup
//   .exit / .quit    clean shutdown
//   .help            show help
//
// Usage:
//   ./minidb [db_path]              interactive
//   ./minidb [db_path] < script.sql run a script
// ============================================================================
#include <unistd.h>
#include <cstdlib>
#include "common/common.h"
#include "engine/database.h"
#include "sql/parser.h"

using namespace minidb;

// Print a SELECT result as an aligned text table.
static void printTable(const Database::Result &r) {
  if (!r.plan.empty()) cout << "-- plan: " << r.plan << "\n";
  size_t ncol = r.columns.size();
  vector<size_t> w(ncol);
  for (size_t i = 0; i < ncol; ++i) w[i] = r.columns[i].size();
  for (auto &row : r.rows)
    for (size_t i = 0; i < ncol; ++i) w[i] = max(w[i], row.value(i).toString().size());

  auto sep = [&]() {
    cout << "+";
    for (size_t i = 0; i < ncol; ++i) { cout << string(w[i] + 2, '-') << "+"; }
    cout << "\n";
  };
  sep();
  cout << "|";
  for (size_t i = 0; i < ncol; ++i)
    cout << " " << r.columns[i] << string(w[i] - r.columns[i].size(), ' ') << " |";
  cout << "\n";
  sep();
  for (auto &row : r.rows) {
    cout << "|";
    for (size_t i = 0; i < ncol; ++i) {
      string v = row.value(i).toString();
      cout << " " << v << string(w[i] - v.size(), ' ') << " |";
    }
    cout << "\n";
  }
  sep();
  cout << r.rows.size() << " row(s)\n";
}

int main(int argc, char **argv) {
  string path = (argc > 1) ? argv[1] : "minidb_data";
  Database db(path);
  Transaction *session = nullptr;     // non-null while inside an explicit BEGIN
  bool interactive = isatty(fileno(stdin));

  if (interactive) {
    cout << "MiniDB -- type SQL ending in ';'. Meta: .help .tables .crash .exit\n";
  }

  string buffer, line;
  while (true) {
    if (interactive && buffer.empty())
      cout << (session ? "minidb*> " : "minidb> ") << flush;
    if (!getline(cin, line)) break;

    // Meta-commands and comments are handled line-by-line.
    string trimmed = line;
    while (!trimmed.empty() && isspace((unsigned char)trimmed.front())) trimmed.erase(trimmed.begin());
    if (trimmed.empty() || trimmed.rfind("--", 0) == 0) continue;
    if (trimmed[0] == '.') {
      if (trimmed == ".exit" || trimmed == ".quit") break;
      if (trimmed == ".crash") {
        cout << "*** simulating crash (no flush) ***\n";
        cout.flush();
        _Exit(0);              // die without running destructors -> no flush
      }
      if (trimmed == ".tables") {
        for (auto &n : db.catalog()->tableNames()) cout << "  " << n << "\n";
        continue;
      }
      if (trimmed == ".help") {
        cout << "SQL: CREATE TABLE/INDEX, INSERT, SELECT (WHERE/JOIN), DELETE, "
                     "BEGIN, COMMIT, ROLLBACK\nMeta: .tables .crash .exit\n";
        continue;
      }
      cout << "unknown command: " << trimmed << "\n";
      continue;
    }

    // Accumulate until we see a ';' that ends the statement.
    buffer += line + " ";
    if (buffer.find(';') == string::npos) continue;
    string sql = buffer;
    buffer.clear();

    try {
      unique_ptr<Statement> stmt = Parser::parse(sql);

      // Transaction-control statements manage the session.
      if (stmt->type == StmtType::kBegin) {
        if (session) { cout << "already in a transaction\n"; continue; }
        session = db.begin();
        cout << "BEGIN (txn " << session->id() << ")\n";
        continue;
      }
      if (stmt->type == StmtType::kCommit) {
        if (!session) { cout << "no active transaction\n"; continue; }
        db.commit(session); session = nullptr;
        cout << "COMMIT\n";
        continue;
      }
      if (stmt->type == StmtType::kAbort) {
        if (!session) { cout << "no active transaction\n"; continue; }
        db.abort(session); session = nullptr;
        cout << "ROLLBACK\n";
        continue;
      }

      // Data statements: run in the session txn, or auto-commit.
      Database::Result r;
      if (session) {
        r = db.execute(stmt.get(), session);
      } else if (stmt->type == StmtType::kCreateTable ||
                 stmt->type == StmtType::kCreateIndex) {
        r = db.execute(stmt.get(), nullptr);
      } else {
        Transaction *txn = db.begin();
        try { r = db.execute(stmt.get(), txn); db.commit(txn); }
        catch (...) { db.abort(txn); throw; }
      }

      if (stmt->type == StmtType::kSelect) printTable(r);
      else cout << r.message << "\n";

    } catch (const DBException &e) {
      cout << "Error: " << e.what() << "\n";
      // A failed statement inside an explicit txn aborts the whole transaction.
      if (session) { db.abort(session); session = nullptr; cout << "(transaction rolled back)\n"; }
    }
  }

  if (session) db.abort(session);   // unfinished txn on exit: roll back
  if (interactive) cout << "bye\n";
  return 0;
}
