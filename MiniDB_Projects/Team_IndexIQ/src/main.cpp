#include "db.h"
#include <iostream>
#include <string>

static void print_rows(const std::vector<Row>& rows) {
    for (auto& row : rows) {
        for (int i = 0; i < (int)row.size(); i++) {
            if (i) std::cout << " | ";
            std::cout << row[i];
        }
        std::cout << "\n";
    }
}

int main(int argc, char* argv[]) {
    std::string data_dir = "./data";
    if (argc > 1) data_dir = argv[1];

    Database db(data_dir);
    std::cout << "MiniDB ready. Type SQL or EXIT.\n";

    std::string line;
    while (true) {
        std::cout << (db.current_txn() ? "txn> " : "minidb> ");
        if (!std::getline(std::cin, line)) break;
        if (line.empty() || line[0] == '-' || line[0] == '#') continue;
        if (line == "EXIT" || line == "exit" || line == "quit") break;

        try {
            auto rows = db.execute(line);
            print_rows(rows);
            if (rows.empty() && line.rfind("INSERT", 0) != 0
                             && line.rfind("insert", 0) != 0
                             && line.rfind("DELETE", 0) != 0
                             && line.rfind("delete", 0) != 0
                             && line.rfind("CREATE", 0) != 0
                             && line.rfind("create", 0) != 0
                             && line.rfind("BEGIN",  0) != 0
                             && line.rfind("begin",  0) != 0
                             && line.rfind("COMMIT", 0) != 0
                             && line.rfind("commit", 0) != 0
                             && line.rfind("ROLLBACK",0)!= 0
                             && line.rfind("rollback",0)!= 0) {
                std::cout << "(no rows)\n";
            } else if (rows.empty()) {
                std::cout << "OK\n";
            }
        } catch (std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
    return 0;
}
