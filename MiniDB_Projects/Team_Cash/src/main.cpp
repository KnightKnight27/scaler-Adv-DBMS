// MiniDB interactive SQL shell.
#include <iostream>
#include <string>

#include "engine.h"

using namespace minidb;

static void printResult(const Result& r) {
    if (r.kind == Result::Message || r.kind == Result::Explain) {
        std::cout << r.text << "\n";
        return;
    }
    // tabular output
    std::vector<size_t> w(r.headers.size());
    for (size_t i = 0; i < r.headers.size(); ++i) w[i] = r.headers[i].size();
    std::vector<std::vector<std::string>> cells;
    for (const Row& row : r.rows) {
        std::vector<std::string> line;
        for (size_t i = 0; i < row.size(); ++i) {
            std::string s = row[i].toString();
            if (i < w.size()) w[i] = std::max(w[i], s.size());
            line.push_back(s);
        }
        cells.push_back(line);
    }
    auto bar = [&]() {
        std::cout << "+";
        for (size_t x : w) std::cout << std::string(x + 2, '-') << "+";
        std::cout << "\n";
    };
    auto pad = [](const std::string& s, size_t n) { return s + std::string(n - s.size(), ' '); };
    bar();
    std::cout << "|";
    for (size_t i = 0; i < r.headers.size(); ++i) std::cout << " " << pad(r.headers[i], w[i]) << " |";
    std::cout << "\n";
    bar();
    for (auto& line : cells) {
        std::cout << "|";
        for (size_t i = 0; i < line.size(); ++i) std::cout << " " << pad(line[i], w[i]) << " |";
        std::cout << "\n";
    }
    bar();
    std::cout << r.rows.size() << " row(s)\n";
}

static const char* HELP =
    "Commands:\n"
    "  CREATE TABLE name (col INT|TEXT, ...);\n"
    "  INSERT INTO name VALUES (...);\n"
    "  SELECT * | cols FROM name [JOIN t2 ON a=b] [WHERE col <op> val [AND ...]];\n"
    "  DELETE FROM name [WHERE ...];\n"
    "  .explain <SELECT ...>   show the chosen query plan\n"
    "  .tables                 list tables\n"
    "  .help                   this message\n"
    "  .exit                   quit";

int main(int argc, char** argv) {
    std::string dataDir = (argc > 1) ? argv[1] : "team_cash_data";
    Engine engine(dataDir);
    std::cout << "MiniDB (Team_Cash) - type .help for commands, .exit to quit\n";

    std::string line;
    while (true) {
        std::cout << "minidb> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line == ".exit" || line == ".quit") break;
        if (line == ".help") { std::cout << HELP << "\n"; continue; }
        if (line == ".tables") {
            auto names = engine.catalog().tableNames();
            if (names.empty()) std::cout << "(none)\n";
            for (auto& n : names) std::cout << n << "\n";
            continue;
        }
        try {
            if (line.rfind(".explain ", 0) == 0)
                printResult(engine.explain(line.substr(9)));
            else
                printResult(engine.execute(line));
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }
    engine.close();
    std::cout << "Goodbye!\n";
    return 0;
}
