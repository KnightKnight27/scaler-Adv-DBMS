#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include "catalog/database.hpp"
#include "query/lexer.hpp"
#include "query/parser.hpp"
#include "query/executor.hpp"

using namespace minidb;

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Render a query result as a simple text table.
static void printResult(const ExecResult& r) {
    if (!r.is_query) { std::cout << r.message << "\n"; return; }
    std::vector<size_t> w(r.columns.size());
    for (size_t i = 0; i < r.columns.size(); i++) w[i] = r.columns[i].size();
    for (const auto& row : r.rows)
        for (size_t i = 0; i < row.size(); i++) w[i] = std::max(w[i], row[i].toString().size());

    auto line = [&]() {
        std::cout << "+";
        for (size_t i = 0; i < w.size(); i++) { std::cout << std::string(w[i] + 2, '-') << "+"; }
        std::cout << "\n";
    };
    line();
    std::cout << "|";
    for (size_t i = 0; i < r.columns.size(); i++)
        std::cout << " " << r.columns[i] << std::string(w[i] - r.columns[i].size(), ' ') << " |";
    std::cout << "\n";
    line();
    for (const auto& row : r.rows) {
        std::cout << "|";
        for (size_t i = 0; i < row.size(); i++) {
            std::string v = row[i].toString();
            std::cout << " " << v << std::string(w[i] - v.size(), ' ') << " |";
        }
        std::cout << "\n";
    }
    line();
    std::cout << "(" << r.rows.size() << " row" << (r.rows.size() == 1 ? "" : "s") << ")\n";
}

int main(int argc, char** argv) {
    std::string db_path = argc > 1 ? argv[1] : "minidb";
    Database db(db_path);
    Executor exec(db);

    std::cerr << "MiniDB ready (data: " << db_path << ".dat). "
              << "End statements with ';'. Type EXIT to quit, CRASH to simulate a crash.\n";

    std::string buffer, line;
    while (std::getline(std::cin, line)) {
        std::string cmd = upper(trim(line));
        if (cmd == "EXIT" || cmd == "QUIT" || cmd == ".EXIT") break;
        if (cmd == "CRASH") {
            // Hard exit WITHOUT flushing the buffer pool: only the WAL survives,
            // so the next startup must reconstruct committed state via recovery.
            std::cerr << "*** simulating crash (buffer pool NOT flushed) ***\n";
            std::_Exit(1);
        }

        buffer += line + "\n";
        // Execute every complete (semicolon-terminated) statement in the buffer.
        size_t semi;
        while ((semi = buffer.find(';')) != std::string::npos) {
            std::string stmt = buffer.substr(0, semi + 1);
            buffer.erase(0, semi + 1);
            if (trim(stmt) == ";") continue;
            try {
                Lexer lx(stmt);
                Parser ps(lx.tokenize());
                printResult(exec.execute(ps.parse()));
            } catch (const std::exception& e) {
                std::cout << "ERROR: " << e.what() << "\n";
            }
        }
    }
    db.flush(); // clean shutdown
    return 0;
}
