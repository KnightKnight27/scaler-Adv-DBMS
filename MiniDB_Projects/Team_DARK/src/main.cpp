#include "concurrency/transaction_manager.h"
#include "execution/executor.h"

#include <iostream>
#include <string>

int main() {
    minidb::TransactionManager transaction_manager;
    minidb::QueryEngine engine(&transaction_manager);
    engine.SeedDemoData();

    std::cout << "miniDB SQL shell. Type SQL ending with ';' or EXIT to quit.\n";

    std::string line;
  while (true) {
        std::cout << "minidb> ";
        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line == "EXIT" || line == "exit" || line == "quit") {
            break;
        }
        if (line.empty()) {
            continue;
        }

        try {
            const minidb::QueryResult result = engine.ExecuteSql(line);
            if (result.rows.empty()) {
                std::cout << "OK\n";
            } else {
                for (const std::string& row : result.rows) {
                    std::cout << row << '\n';
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "ERROR: " << ex.what() << '\n';
        }
    }

    return 0;
}
