// MiniDB interactive shell. Reads SQL statements (terminated by ';' or newline)
// and prints results as an ASCII table. Usage:
//   minidb [db_file]        interactive REPL
//   minidb db_file < script run a script of statements

#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "common/exception.h"
#include "engine/database.h"

using namespace minidb;

static void PrintResult(const ResultSet &rs) {
  if (!rs.is_query) {
    std::cout << rs.message << "\n";
    return;
  }
  // Compute column widths.
  std::vector<size_t> width(rs.columns.size());
  for (size_t i = 0; i < rs.columns.size(); i++) width[i] = rs.columns[i].size();
  for (const auto &row : rs.rows) {
    for (size_t i = 0; i < row.size(); i++) width[i] = std::max(width[i], row[i].ToString().size());
  }
  auto sep = [&]() {
    std::cout << "+";
    for (size_t w : width) std::cout << std::string(w + 2, '-') << "+";
    std::cout << "\n";
  };
  auto print_row = [&](const std::vector<std::string> &cells) {
    std::cout << "|";
    for (size_t i = 0; i < cells.size(); i++) {
      std::cout << " " << cells[i] << std::string(width[i] - cells[i].size(), ' ') << " |";
    }
    std::cout << "\n";
  };
  sep();
  print_row(rs.columns);
  sep();
  for (const auto &row : rs.rows) {
    std::vector<std::string> cells;
    for (const auto &v : row) cells.push_back(v.ToString());
    print_row(cells);
  }
  sep();
  std::cout << "(" << rs.rows.size() << " row" << (rs.rows.size() == 1 ? "" : "s") << ")\n";
}

int main(int argc, char **argv) {
  std::string db_file = (argc > 1) ? argv[1] : "minidb.db";
  Database db(db_file);
  const bool interactive = ::isatty(0);

  if (interactive) {
    std::cout << "MiniDB shell — database '" << db_file << "'. End statements with ';'. Ctrl-D to exit.\n";
  }

  std::string buffer;
  std::string line;
  while (true) {
    if (interactive && buffer.empty()) std::cout << "minidb> " << std::flush;
    if (!std::getline(std::cin, line)) break;
    buffer += line + "\n";

    // Execute on ';' boundaries; allow multiple statements per line.
    size_t semi;
    while ((semi = buffer.find(';')) != std::string::npos) {
      std::string stmt = buffer.substr(0, semi);
      buffer.erase(0, semi + 1);
      // skip empty/whitespace-only statements
      if (stmt.find_first_not_of(" \t\r\n") == std::string::npos) continue;
      try {
        PrintResult(db.Execute(stmt));
      } catch (const std::exception &e) {
        std::cout << "ERROR: " << e.what() << "\n";
      }
    }
  }
  return 0;
}
