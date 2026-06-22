#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "engine/database.h"

using namespace minidb;

// Print a SELECT result as an aligned ASCII table.
static void PrintResult(const ExecutionResult &r) {
  if (!r.explain.empty()) { std::cout << "QUERY PLAN\n----------\n" << r.explain; return; }
  if (!r.ok) { std::cout << r.message << "\n"; return; }
  if (r.columns.empty()) {
    std::cout << r.message << "\n";
    return;
  }
  size_t n = r.columns.size();
  std::vector<size_t> w(n);
  for (size_t i = 0; i < n; i++) w[i] = r.columns[i].size();
  for (auto &row : r.rows)
    for (size_t i = 0; i < n && i < row.size(); i++) w[i] = std::max(w[i], row[i].size());

  auto sep = [&]() {
    std::cout << "+";
    for (size_t i = 0; i < n; i++) { std::cout << std::string(w[i] + 2, '-') << "+"; }
    std::cout << "\n";
  };
  sep();
  std::cout << "|";
  for (size_t i = 0; i < n; i++)
    std::cout << " " << r.columns[i] << std::string(w[i] - r.columns[i].size(), ' ') << " |";
  std::cout << "\n";
  sep();
  for (auto &row : r.rows) {
    std::cout << "|";
    for (size_t i = 0; i < n; i++) {
      std::string v = i < row.size() ? row[i] : "";
      std::cout << " " << v << std::string(w[i] - v.size(), ' ') << " |";
    }
    std::cout << "\n";
  }
  sep();
  std::cout << "(" << r.rows.size() << " row" << (r.rows.size() == 1 ? "" : "s") << ")\n";
}

static void PrintHelp() {
  std::cout <<
      "MiniDB commands:\n"
      "  <SQL>;            run a SQL statement (CREATE/INSERT/SELECT/DELETE/...)\n"
      "  EXPLAIN <query>;  show the chosen query plan\n"
      "  BEGIN; COMMIT; ROLLBACK;   explicit transactions\n"
      "  .tables           list tables\n"
      "  .crash            simulate a crash (drop unflushed state), then recover\n"
      "  .help             show this help\n"
      "  .exit / .quit     leave\n";
}

int main(int argc, char **argv) {
  std::string path = (argc > 1) ? argv[1] : "minidb_data/db";
  auto db = std::make_unique<Database>(path);
  bool interactive = true;

  std::cout << "MiniDB — Advanced DBMS capstone. Type .help for commands.\n";
  std::string buffer, line;
  auto prompt = [&]() { if (interactive) std::cout << (buffer.empty() ? "minidb> " : "   ...> ") << std::flush; };
  prompt();

  while (std::getline(std::cin, line)) {
    // Meta-commands (single line, no trailing ';').
    std::string trimmed = line;
    while (!trimmed.empty() && std::isspace((unsigned char)trimmed.front())) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace((unsigned char)trimmed.back())) trimmed.pop_back();
    // If only whitespace remains buffered, treat the buffer as empty.
    if (buffer.find_first_not_of(" \t\r\n") == std::string::npos) buffer.clear();
    if (buffer.empty() && (!trimmed.empty() && trimmed[0] == '.')) {
      if (trimmed == ".exit" || trimmed == ".quit") break;
      if (trimmed == ".help") PrintHelp();
      else if (trimmed == ".tables") PrintResult(db->Execute("SHOW TABLES"));
      else if (trimmed == ".crash") {
        db->SimulateCrash();
        std::cout << "** simulated crash: unflushed state dropped, reopening (recovery runs) **\n";
        db = std::make_unique<Database>(path);
        std::cout << "** recovery complete **\n";
      } else std::cout << "unknown command: " << trimmed << " (.help)\n";
      prompt();
      continue;
    }

    buffer += line + "\n";
    // Execute each complete (';'-terminated) statement in the buffer.
    size_t semi;
    while ((semi = buffer.find(';')) != std::string::npos) {
      std::string stmt = buffer.substr(0, semi);
      buffer.erase(0, semi + 1);
      bool blank = stmt.find_first_not_of(" \t\r\n") == std::string::npos;
      if (!blank) PrintResult(db->Execute(stmt));
    }
    prompt();
  }
  std::cout << "\nbye.\n";
  return 0;
}
