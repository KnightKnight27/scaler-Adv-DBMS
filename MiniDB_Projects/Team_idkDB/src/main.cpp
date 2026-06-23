#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "minidb/common/trace.h"
#include "minidb/db/database.h"
#include "minidb/query/parser.h"

int main(int argc, char **argv) {
  const std::filesystem::path path =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("data/demo");
  minidb::Trace::SetEnabled(true);

  try {
    minidb::Database db(path);
    std::cout << "MiniDB educational relational engine\n";
    std::cout << "Database path: " << path << "\n";
    std::cout << "Try: INSERT users 1 Sushant\n";
    std::cout << "     SELECT users WHERE id=1\n";
    std::cout << "     DELETE users WHERE id=1\n";
    std::cout << "     SELECT users JOIN profiles ON users.id=profiles.id\n";
    std::cout << "     BEGIN / COMMIT / ABORT\n";
    std::cout << "Type HELP or EXIT.\n";

    minidb::Parser parser;
    std::string line;
    while (std::cout << "minidb> " && std::getline(std::cin, line)) {
      if (line.empty()) continue;
      try {
        const auto query = parser.Parse(line);
        if (query.type == minidb::QueryType::Exit) break;
        const auto result = db.Execute(line);
        std::cout << result.message << '\n';
        if (!result.plan.explanation.empty()) {
          std::cout << "[OPTIMIZER] " << result.plan.explanation
                    << " (seq_cost=" << result.plan.sequential_cost
                    << ", index_cost=" << result.plan.index_cost << ")\n";
        }
      } catch (const std::exception &error) {
        std::cout << "error: " << error.what() << '\n';
      }
    }
    db.Flush();
  } catch (const std::exception &error) {
    std::cerr << "fatal: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
