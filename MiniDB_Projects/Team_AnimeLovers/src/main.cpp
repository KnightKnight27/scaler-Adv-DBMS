#include "engine.h"
#include <fstream>
#include <iostream>
#include <string>

// main.cpp — Interactive REPL and batch script runner.
//
// Usage:
//   ./minidb --db /path/to/db               # interactive shell
//   ./minidb --db /path/to/db -f script.sql # run SQL file, then exit

int main(int argc, char** argv) {
    std::string db_path = "/tmp/minidb_default";
    std::string script;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) db_path = argv[++i];
        if (arg == "-f"   && i + 1 < argc) script  = argv[++i];
    }

    minidb::Database db(db_path);

    auto rec = db.recovery_stats();
    if (rec.tables > 0 || rec.rows_redone > 0) {
        std::cout << "[Recovery] replayed " << rec.tables << " table(s), "
                  << rec.rows_redone << " row(s).\n";
    }

    auto run = [&](std::istream& in, bool interactive) {
        std::string line, sql;
        while (true) {
            if (interactive) std::cout << (sql.empty() ? "minidb> " : "     -> ");
            if (!std::getline(in, line)) break;
            if (line.empty() || line[0] == '-' && line.size() > 1 && line[1] == '-') continue;
            sql += (sql.empty() ? "" : " ") + line;
            // Execute when we see a semicolon or on a non-continuation line.
            if (sql.back() == ';') sql.pop_back();
            if (!sql.empty()) {
                minidb::print_result(db.execute(sql));
                sql.clear();
            }
        }
    };

    if (!script.empty()) {
        std::ifstream f(script);
        if (!f.is_open()) { std::cerr << "Cannot open: " << script << '\n'; return 1; }
        run(f, false);
    } else {
        std::cout << "MiniDB — Team AnimeLovers.  Type SQL; CTRL-D to quit.\n";
        run(std::cin, true);
    }
    return 0;
}
