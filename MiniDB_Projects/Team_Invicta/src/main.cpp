// MiniDB interactive SQL shell. Reads statements terminated by ';' from stdin,
// executes them through the engine, and prints query results as a table. A few
// dot-commands (.tables, .explain, .help, .exit) aid the demo.
#include <iostream>
#include <string>
#include <vector>
#include "engine/database.h"

using namespace minidb;

namespace {

// Render a result set as an aligned ASCII table.
void PrintTable(const ExecResult &r) {
  std::vector<size_t> w(r.columns.size());
  for (size_t i = 0; i < r.columns.size(); ++i) w[i] = r.columns[i].size();
  for (const auto &row : r.rows)
    for (size_t i = 0; i < row.size(); ++i) w[i] = std::max(w[i], row[i].size());

  auto rule = [&]() {
    std::cout << "+";
    for (size_t i = 0; i < w.size(); ++i) std::cout << std::string(w[i] + 2, '-') << "+";
    std::cout << "\n";
  };
  auto emit = [&](const std::vector<std::string> &cells) {
    std::cout << "|";
    for (size_t i = 0; i < cells.size(); ++i)
      std::cout << " " << cells[i] << std::string(w[i] - cells[i].size(), ' ') << " |";
    std::cout << "\n";
  };

  rule();
  emit(r.columns);
  rule();
  for (const auto &row : r.rows) emit(row);
  rule();
  std::cout << r.rows.size() << " row(s)\n";
}

void PrintHelp() {
  std::cout <<
      "MiniDB shell commands:\n"
      "  .tables            list tables\n"
      "  .explain on|off    show/hide the query plan for SELECT\n"
      "  .help              this help\n"
      "  .exit / .quit      leave\n"
      "SQL: CREATE TABLE / INSERT / SELECT (WHERE, JOIN, COUNT(*)) / DELETE /\n"
      "     BEGIN; COMMIT; ROLLBACK;  (add USING LSM to CREATE for the LSM engine)\n";
}

}  // namespace

int main(int argc, char **argv) {
  std::string db_name = (argc > 1) ? argv[1] : "minidb";
  Database db(db_name);
  bool explain = false;
  bool interactive = true;

  std::cout << "MiniDB — Advanced DBMS capstone (Track C: LSM).  Database: "
            << db_name << "\nType .help for commands.\n";

  std::string pending, line;
  while (true) {
    if (interactive) std::cout << (pending.empty() ? "minidb> " : "    ...> ") << std::flush;
    if (!std::getline(std::cin, line)) break;

    // Strip an SQL line comment (-- ... to end of line), respecting single
    // quotes, so neither full-line nor inline comments pollute the buffer or
    // mask a following dot-command.
    {
      bool in_quote = false;
      for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\'') in_quote = !in_quote;
        else if (!in_quote && line[i] == '-' && i + 1 < line.size() && line[i + 1] == '-') {
          line.erase(i);
          break;
        }
      }
    }

    // Dot-commands act on a single line (only when no statement is pending).
    if (pending.empty() && !line.empty() && line[0] == '.') {
      if (line == ".exit" || line == ".quit") break;
      if (line == ".help") { PrintHelp(); continue; }
      if (line == ".tables") {
        for (const std::string &t : db.catalog().TableNames()) std::cout << "  " << t << "\n";
        continue;
      }
      if (line == ".explain on")  { explain = true;  std::cout << "explain on\n";  continue; }
      if (line == ".explain off") { explain = false; std::cout << "explain off\n"; continue; }
      std::cout << "unknown command (try .help)\n";
      continue;
    }

    pending += line + "\n";
    // Execute every complete (semicolon-terminated) statement in the buffer.
    size_t semi;
    while ((semi = pending.find(';')) != std::string::npos) {
      std::string stmt = pending.substr(0, semi + 1);
      pending.erase(0, semi + 1);
      // Skip whitespace-only fragments.
      if (stmt.find_first_not_of(" \t\r\n;") == std::string::npos) continue;

      ExecResult r = db.Execute(stmt);
      if (explain && !r.explain.empty()) std::cout << r.explain;
      if (!r.ok) {
        std::cout << r.message << "\n";
      } else if (r.is_query) {
        PrintTable(r);
      } else {
        std::cout << r.message << "\n";
      }
    }
    // Drop a whitespace-only remainder so the prompt resets and the next line
    // can be a dot-command.
    if (pending.find_first_not_of(" \t\r\n") == std::string::npos) pending.clear();
  }

  db.Flush();
  std::cout << "\nbye.\n";
  return 0;
}
