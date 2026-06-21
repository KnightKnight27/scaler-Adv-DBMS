#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "catalog/catalog.hpp"
#include "query/executor.hpp"
#include "query/lexer.hpp"
#include "query/parser.hpp"

// MiniDB REPL. Reads SQL from stdin, splits on ';', and runs each statement
// against a Catalog. Per-table data files live in the directory given as argv[1]
// (default "."). Each statement's plan and result print below it.

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

void run_statement(const std::string& sql, Catalog& cat) {
    std::cout << "minidb> " << trim(sql) << ";\n";
    try {
        Lexer lexer(sql);
        Parser parser(lexer.tokenize());
        Statement stmt = parser.parse();
        execute_statement(stmt, cat, std::cout);
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir = (argc > 1) ? argv[1] : ".";
    Catalog catalog(dir);

    // Read all of stdin, then execute one statement per ';'-separated chunk.
    std::string input((std::istreambuf_iterator<char>(std::cin)),
                      std::istreambuf_iterator<char>());

    std::size_t i = 0;
    while (i < input.size()) {
        std::size_t semi = input.find(';', i);
        std::string chunk = (semi == std::string::npos) ? input.substr(i) : input.substr(i, semi - i);
        if (has_content(chunk)) run_statement(chunk, catalog);
        if (semi == std::string::npos) break;
        i = semi + 1;
    }
    return 0;
}
