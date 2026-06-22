// MiniDB interactive shell. Reads SQL statements (terminated by ';' or newline)
// and prints results. A small set of dot-commands control the session.
#include <iostream>
#include <sstream>
#include <string>
#include "engine/database.h"

using namespace minidb;

static void PrintResult(const QueryResult& r, bool show_plan) {
  if (show_plan && !r.plan.empty()) {
    std::cout << "-- plan: " << r.plan << "\n";
  }
  if (!r.is_select) {
    std::cout << r.message << "\n";
    return;
  }
  // Header.
  std::string header;
  for (size_t i = 0; i < r.schema.size(); i++) {
    if (i) header += " | ";
    header += r.schema[i].name;
  }
  std::cout << header << "\n";
  std::cout << std::string(header.size(), '-') << "\n";
  for (auto& row : r.rows) {
    for (size_t i = 0; i < row.size(); i++) {
      if (i) std::cout << " | ";
      std::cout << row[i].ToString();
    }
    std::cout << "\n";
  }
  std::cout << "(" << r.rows.size() << " row(s))\n";
}

int main(int argc, char** argv) {
  std::string db_path = (argc > 1) ? argv[1] : "minidb";
  Database db(db_path);
  bool show_plan = false;

  std::cout << "MiniDB shell. Database: " << db_path
            << "\nType SQL ending with ';'. Commands: .tables  .plan on|off  .exit\n";

  std::string line, buffer;
  while (true) {
    std::cout << (buffer.empty() ? "minidb> " : "   ...> ");
    if (!std::getline(std::cin, line)) break;

    // Dot-commands (only when no statement is mid-entry).
    if (buffer.empty() && !line.empty() && line[0] == '.') {
      if (line == ".exit" || line == ".quit") break;
      if (line == ".tables") {
        for (auto& n : db.GetCatalog()->TableNames()) std::cout << n << "\n";
        continue;
      }
      if (line == ".plan on") { show_plan = true; std::cout << "plan display on\n"; continue; }
      if (line == ".plan off") { show_plan = false; std::cout << "plan display off\n"; continue; }
      std::cout << "unknown command\n";
      continue;
    }

    buffer += line + " ";
    if (buffer.find(';') == std::string::npos) continue;  // need more input

    // Execute each ';'-terminated statement in the buffer.
    std::stringstream ss(buffer);
    std::string stmt;
    while (std::getline(ss, stmt, ';')) {
      bool only_ws = stmt.find_first_not_of(" \t\r\n") == std::string::npos;
      if (only_ws) continue;
      try {
        PrintResult(db.Execute(stmt), show_plan);
      } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
      }
    }
    buffer.clear();
  }
  std::cout << "bye\n";
  return 0;
}
