#include <iostream>
#include <sstream>
#include <string>

#include "engine.h"

using namespace minidb;

namespace {

void print_result(const ResultSet& r) {
  if (!r.message.empty() && !r.is_query) {
    std::cout << r.message << "\n";
    return;
  }
  if (!r.is_query) return;

  std::vector<size_t> w(r.columns.size());
  for (size_t i = 0; i < r.columns.size(); i++) w[i] = r.columns[i].size();
  for (const auto& row : r.rows)
    for (size_t i = 0; i < row.size(); i++) w[i] = std::max(w[i], row[i].to_string().size());

  auto sep = [&] {
    std::cout << "+";
    for (size_t i = 0; i < w.size(); i++) std::cout << std::string(w[i] + 2, '-') << "+";
    std::cout << "\n";
  };
  sep();
  std::cout << "|";
  for (size_t i = 0; i < r.columns.size(); i++)
    std::cout << " " << r.columns[i] << std::string(w[i] - r.columns[i].size(), ' ') << " |";
  std::cout << "\n";
  sep();
  for (const auto& row : r.rows) {
    std::cout << "|";
    for (size_t i = 0; i < row.size(); i++) {
      std::string s = row[i].to_string();
      std::cout << " " << s << std::string(w[i] - s.size(), ' ') << " |";
    }
    std::cout << "\n";
  }
  sep();
  std::cout << "(" << r.rows.size() << " row" << (r.rows.size() == 1 ? "" : "s") << ")\n";
}

void run_sql(Database& db, const std::string& sql) {
  try {
    print_result(db.execute(sql));
  } catch (const std::exception& e) {
    std::cout << "ERROR: " << e.what() << "\n";
  }
}

void demo(Database& db) {
  const char* stmts[] = {
      "CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR, age INTEGER)",
      "CREATE TABLE orders (id INTEGER PRIMARY KEY, uid INTEGER, amount INTEGER)",
      "INSERT INTO users VALUES (1,'alice',30),(2,'bob',25),(3,'carol',41),(4,'dave',25)",
      "INSERT INTO orders VALUES (10,1,100),(11,1,250),(12,2,75),(13,3,300)",
      "SELECT * FROM users WHERE age > 26 ORDER BY age DESC",
      "SELECT id, name FROM users WHERE id = 2",
      "EXPLAIN SELECT id, name FROM users WHERE id = 2",
      "SELECT u.name, o.amount FROM users u JOIN orders o ON u.id = o.uid ORDER BY o.amount",
      "SELECT age, COUNT(*), SUM(age) FROM users GROUP BY age ORDER BY age",
      "SELECT COUNT(*), MIN(amount), MAX(amount), AVG(amount) FROM orders",
      "DELETE FROM users WHERE age = 25",
      "SELECT COUNT(*) FROM users",
  };
  for (const char* s : stmts) {
    std::cout << "\nminidb> " << s << "\n";
    run_sql(db, s);
  }
}

void crash_demo(Database& db) {
  std::cout << "=== Crash recovery demo ===\n";
  run_sql(db, "CREATE TABLE acct (id INTEGER PRIMARY KEY, bal INTEGER)");
  run_sql(db, "INSERT INTO acct VALUES (1,100),(2,200)");
  std::cout << "[committed two rows; flushing a checkpoint]\n";
  db.checkpoint();

  run_sql(db, "BEGIN");
  run_sql(db, "INSERT INTO acct VALUES (3,999)");
  std::cout << "[inserted id=3 inside an uncommitted transaction]\n";

  std::cout << "[*** simulating crash: dropping buffer pool, replaying WAL ***]\n";
  db.simulate_crash_and_recover();

  std::cout << "\nAfter recovery:\n";
  run_sql(db, "SELECT * FROM acct ORDER BY id");
  std::cout << "(rows 1 and 2 committed -> present; row 3 uncommitted -> gone)\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string dir = "minidb_data";
  CCMode mode = CCMode::TWO_PL;
  bool do_demo = false, do_crash = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--dir" && i + 1 < argc) dir = argv[++i];
    else if (a == "--mvcc") mode = CCMode::MVCC;
    else if (a == "--demo") do_demo = true;
    else if (a == "--crashdemo") do_crash = true;
    else if (a == "--help") {
      std::cout << "usage: minidb_cli [--dir PATH] [--mvcc] [--demo] [--crashdemo]\n";
      return 0;
    }
  }

  Database db(dir, mode);
  std::cout << "MiniDB (" << (mode == CCMode::MVCC ? "MVCC" : "2PL")
            << " mode), data dir: " << dir << "\n";

  if (do_crash) { crash_demo(db); return 0; }
  if (do_demo) { demo(db); return 0; }

  std::cout << "Enter SQL statements terminated by ';'. Ctrl-D to quit.\n";
  std::string buffer, line;
  while (std::getline(std::cin, line)) {
    buffer += line + "\n";
    size_t semi;
    while ((semi = buffer.find(';')) != std::string::npos) {
      std::string stmt = buffer.substr(0, semi);
      buffer.erase(0, semi + 1);
      if (stmt.find_first_not_of(" \t\n\r") != std::string::npos) run_sql(db, stmt);
    }
  }
  return 0;
}
