#include <cctype>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "catalog/database.hpp"
#include "query/executor.hpp"
#include "query/lexer.hpp"
#include "query/parser.hpp"

// REPL: read SQL from stdin, split on ';', run each statement
// RECOVER replays the WAL; CRASH hard-exits without flushing

namespace {

bool has_content(const std::string& s) {
    for (char c : s) if (!std::isspace(static_cast<unsigned char>(c))) return true;
    return false;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

void run_statement(const std::string& sql, Database& db) {
    std::cout << "minidb> " << trim(sql) << ";\n";
    try {
        Lexer lexer(sql);
        Parser parser(lexer.tokenize());
        Statement stmt = parser.parse();
        execute_statement(stmt, db, std::cout);
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir = (argc > 1) ? argv[1] : ".";
    Database db(dir);

    std::string input((std::istreambuf_iterator<char>(std::cin)),
                      std::istreambuf_iterator<char>());

    std::size_t i = 0;
    while (i < input.size()) {
        std::size_t semi = input.find(';', i);
        std::string chunk = (semi == std::string::npos) ? input.substr(i) : input.substr(i, semi - i);
        std::size_t next = (semi == std::string::npos) ? input.size() : semi + 1;
        i = next;

        if (!has_content(chunk)) continue;
        std::string cmd = upper(trim(chunk));

        if (cmd == "RECOVER") {
            std::cout << "minidb> RECOVER;\n";
            db.recover(std::cout);
            std::cout << "\n";
            continue;
        }
        if (cmd == "CRASH") {
            std::cout << "minidb> CRASH;\n*** simulating crash: exiting without flushing the buffer pool ***\n";
            std::cout.flush();
            std::_Exit(1);  // skip destructors -> pages not flushed
        }
        run_statement(chunk, db);
    }
    return 0;
}
