// ===========================================================================
// AxiomDB CLI / REPL.
//
// A plain std::getline shell over the Database facade: read SQL (statements end
// with ';', may span lines), execute, print results.  Dot-commands handle
// meta-operations (.tables, .schema, .help, .crash, .exit).  Works
// interactively and with piped input (the demo scripts pipe SQL in).
//
//   axiomdb [db_path]      (default db_path: "axiomdb")
// ===========================================================================
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "engine/database.h"

using namespace axiomdb;

namespace {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// Print a SELECT result as an aligned text table.
void print_table(const ExecResult& r) {
  const size_t ncols = r.columns.size();
  std::vector<size_t> width(ncols);
  for (size_t c = 0; c < ncols; ++c) width[c] = r.columns[c].size();
  std::vector<std::vector<std::string>> cells;
  for (const auto& row : r.rows) {
    std::vector<std::string> line(ncols);
    for (size_t c = 0; c < ncols && c < row.size(); ++c) {
      line[c] = row[c].to_string();
      width[c] = std::max(width[c], line[c].size());
    }
    cells.push_back(std::move(line));
  }

  auto sep = [&] {
    std::cout << '+';
    for (size_t c = 0; c < ncols; ++c) {
      std::cout << std::string(width[c] + 2, '-') << '+';
    }
    std::cout << '\n';
  };
  auto print_row = [&](const std::vector<std::string>& line) {
    std::cout << '|';
    for (size_t c = 0; c < ncols; ++c) {
      std::string v = c < line.size() ? line[c] : "";
      std::cout << ' ' << v << std::string(width[c] - v.size(), ' ') << " |";
    }
    std::cout << '\n';
  };

  sep();
  print_row(r.columns);
  sep();
  for (const auto& line : cells) print_row(line);
  sep();
  std::cout << "(" << r.rows.size() << " row" << (r.rows.size() == 1 ? "" : "s") << ")\n";
}

void print_result(const ExecResult& r) {
  if (!r.ok) {
    std::cout << "Error: " << r.error << "\n";
    return;
  }
  if (r.is_select) {
    print_table(r);
  } else if (!r.message.empty()) {
    std::cout << r.message << "\n";
  } else {
    std::cout << "OK\n";
  }
}

void print_help() {
  std::cout <<
      "AxiomDB commands:\n"
      "  SQL: CREATE TABLE / INSERT / SELECT (WHERE/JOIN) / DELETE / EXPLAIN\n"
      "       BEGIN | COMMIT | ROLLBACK            -- explicit transactions\n"
      "  .tables            list tables\n"
      "  .schema <table>    show a table's columns\n"
      "  .crash             simulate a crash (skip clean shutdown) and exit\n"
      "  .help              this help\n"
      "  .exit / .quit      leave\n"
      "Statements end with ';'.\n";
}

// Returns true to keep running, false to exit.  *crash is set if .crash was used.
bool handle_dot(Database& db, const std::string& cmd, bool* crash) {
  std::istringstream iss(cmd);
  std::string word;
  iss >> word;
  if (word == ".exit" || word == ".quit") return false;
  if (word == ".help") { print_help(); return true; }
  if (word == ".crash") { *crash = true; return false; }
  if (word == ".tables") {
    for (const auto& name : db.catalog().table_names()) std::cout << name << "\n";
    return true;
  }
  if (word == ".schema") {
    std::string t;
    iss >> t;
    TableInfo* info = db.catalog().get_table(t);
    if (!info) { std::cout << "no such table '" << t << "'\n"; return true; }
    for (const Column& col : info->schema.columns()) {
      std::cout << "  " << col.name << " " << type_name(col.type)
                << (col.primary_key ? " PRIMARY KEY" : "") << "\n";
    }
    return true;
  }
  std::cout << "unknown command '" << word << "' (try .help)\n";
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string db_path = argc > 1 ? argv[1] : "axiomdb";
  Database db(db_path);
  bool interactive = ::isatty(STDIN_FILENO);

  if (interactive) {
    std::cout << "AxiomDB -- a mini relational database (LSM Storage Engine: LSM).\n"
                 "Type .help for commands. Database: " << db_path << "\n";
  }

  std::string buffer;
  std::string line;
  bool crash = false;
  while (true) {
    if (interactive) std::cout << (buffer.empty() ? "axiomdb> " : "      ...> ") << std::flush;
    if (!std::getline(std::cin, line)) break;

    std::string trimmed = trim(line);
    // Dot-commands are handled per line, only at a statement boundary (the
    // buffer holds at most leftover whitespace).
    if (trim(buffer).empty() && !trimmed.empty() && trimmed[0] == '.') {
      buffer.clear();
      if (!handle_dot(db, trimmed, &crash)) break;
      continue;
    }

    buffer += line + "\n";
    // Execute every complete (';'-terminated) statement in the buffer.
    size_t semi;
    while ((semi = buffer.find(';')) != std::string::npos) {
      std::string stmt = buffer.substr(0, semi);
      buffer.erase(0, semi + 1);
      if (!trim(stmt).empty()) print_result(db.run(stmt));
    }
    if (trim(buffer).empty()) buffer.clear();  // drop trailing whitespace
  }

  if (crash) {
    db.simulate_crash();  // exit without a clean checkpoint -> recovery on next open
    if (interactive) std::cout << "\n[simulated crash: no clean shutdown]\n";
  } else if (interactive) {
    std::cout << "\nBye.\n";
  }
  return 0;
}
