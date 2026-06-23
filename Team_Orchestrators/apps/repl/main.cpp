// MiniDB interactive REPL.
//   Usage: minidb [database-base-path]   (default: "minidb")
// Files <base>.data and <base>.catalog hold the database.
#include "minidb/database.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace minidb;

namespace {

void print_result(const QueryResult& r) {
  if (!r.is_select) {
    std::cout << r.message << "\n";
    return;
  }
  const size_t ncol = r.columns.size();
  std::vector<size_t> width(ncol);
  for (size_t i = 0; i < ncol; ++i) width[i] = r.columns[i].size();
  std::vector<std::vector<std::string>> cells;
  for (const auto& row : r.rows) {
    std::vector<std::string> line;
    for (size_t i = 0; i < ncol; ++i) {
      std::string s = i < row.size() ? row[i].to_string() : "";
      width[i] = std::max(width[i], s.size());
      line.push_back(std::move(s));
    }
    cells.push_back(std::move(line));
  }

  auto sep = [&]() {
    std::cout << "+";
    for (size_t i = 0; i < ncol; ++i) {
      std::cout << std::string(width[i] + 2, '-') << "+";
    }
    std::cout << "\n";
  };
  auto print_row = [&](const std::vector<std::string>& cols) {
    std::cout << "|";
    for (size_t i = 0; i < ncol; ++i) {
      std::cout << " " << cols[i] << std::string(width[i] - cols[i].size(), ' ') << " |";
    }
    std::cout << "\n";
  };

  sep();
  print_row(r.columns);
  sep();
  for (const auto& line : cells) print_row(line);
  sep();
  std::cout << r.rows.size() << " row(s).\n";
}

// Strips a trailing "-- ..." line comment.
std::string strip_comment(const std::string& line) {
  size_t pos = line.find("--");
  return pos == std::string::npos ? line : line.substr(0, pos);
}

// Runs a ;-terminated SQL script from a stream, echoing statements and results.
void run_script(Database& db, std::istream& in) {
  std::string line;
  std::string buffer;
  while (std::getline(in, line)) {
    buffer += strip_comment(line);
    buffer += " ";
    size_t semi;
    while ((semi = buffer.find(';')) != std::string::npos) {
      std::string stmt = buffer.substr(0, semi + 1);
      buffer.erase(0, semi + 1);
      // Skip blank statements.
      bool blank = true;
      for (char c : stmt)
        if (c != ';' && !std::isspace(static_cast<unsigned char>(c))) { blank = false; break; }
      if (blank) continue;
      std::cout << "sql> " << stmt << "\n";
      try {
        print_result(db.execute(stmt));
      } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string base = argc > 1 ? argv[1] : "minidb";
  Database db(base);

  std::cout << "MiniDB (Track C). Database: " << base << "\n"
            << "Type SQL statements ending with ';'. Commands: .tables .exit\n";

  std::string line;
  std::string buffer;
  while (true) {
    std::cout << (buffer.empty() ? "minidb> " : "    ...> ") << std::flush;
    if (!std::getline(std::cin, line)) break;

    // Meta commands (only when not mid-statement).
    if (buffer.empty()) {
      std::string trimmed = line;
      if (trimmed == ".exit" || trimmed == ".quit") break;
      if (trimmed == ".help") {
        std::cout
            << "Statements: CREATE TABLE, CREATE INDEX, INSERT, SELECT (WHERE,\n"
            << "  INNER JOIN, ORDER BY), DELETE, ANALYZE, EXPLAIN,\n"
            << "  BEGIN / COMMIT / ROLLBACK.\n"
            << "Commands: .tables  .read <file>  .help  .exit\n";
        continue;
      }
      if (trimmed.rfind(".read ", 0) == 0) {
        std::string fname = trimmed.substr(6);
        // trim surrounding whitespace
        while (!fname.empty() && std::isspace(static_cast<unsigned char>(fname.front()))) fname.erase(fname.begin());
        while (!fname.empty() && std::isspace(static_cast<unsigned char>(fname.back()))) fname.pop_back();
        std::ifstream f(fname);
        if (!f) {
          std::cout << "cannot open '" << fname << "'\n";
          continue;
        }
        run_script(db, f);
        continue;
      }
      if (trimmed == ".tables") {
        auto names = db.table_names();
        if (names.empty()) std::cout << "(no tables)\n";
        for (const auto& n : names) std::cout << n << "\n";
        continue;
      }
    }

    buffer += line;
    buffer += " ";
    if (line.find(';') == std::string::npos) continue;  // statement not finished

    try {
      QueryResult r = db.execute(buffer);
      print_result(r);
    } catch (const std::exception& e) {
      std::cout << "Error: " << e.what() << "\n";
    }
    buffer.clear();
  }

  db.flush();
  std::cout << "Bye.\n";
  return 0;
}
