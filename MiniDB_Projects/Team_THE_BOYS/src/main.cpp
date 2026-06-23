#include <filesystem>
#include <iostream>
#include <string>

#include "engine/database.h"
#include "executor/execution_metrics.h"

namespace {

void PrintHelp() {
    std::cout << "MiniDB — Team THE BOYS\n"
              << "Commands: SQL statements ending with ;\n"
              << "  CREATE TABLE, INSERT, SELECT, DELETE, BEGIN, COMMIT, ROLLBACK\n"
              << "  SET EXEC_MODE ROW | SET EXEC_MODE BATCH\n"
              << "  CHECKPOINT, CRASH, .recover, .stats, .quit\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string db_path = (argc > 1) ? argv[1] : "data";
    std::filesystem::create_directories(db_path);

    minidb::Database db(db_path);
    PrintHelp();

    std::string line;
    while (true) {
        std::cout << "minidb> ";
        if (!std::getline(std::cin, line)) break;
        if (line == ".quit" || line == "quit" || line == "exit") break;
        if (line == ".help") {
            PrintHelp();
            continue;
        }
        if (line == ".recover") {
            db.Recover();
            std::cout << "Recovery complete.\n";
            continue;
        }
        if (line == ".stats") {
            auto m = minidb::ExecutionMetricsHolder::Get();
            std::cout << "Buffer pool hits: " << db.buffer_pool()->HitCount()
                      << " misses: " << db.buffer_pool()->MissCount() << '\n';
            std::cout << "Last query tuples scanned: " << m.tuples_scanned
                      << " output: " << m.tuples_output << " batches: " << m.batches_processed
                      << " columnar: " << (m.used_columnar_filter ? "yes" : "no") << '\n';
            continue;
        }
        if (line == "CRASH;" || line == "CRASH") {
            std::cout << "Simulating crash...\n";
            db.Crash();
        }
        try {
            std::cout << db.ExecuteSql(line);
        } catch (const std::exception& ex) {
            std::cout << "ERROR: " << ex.what() << '\n';
        }
    }
    return 0;
}
