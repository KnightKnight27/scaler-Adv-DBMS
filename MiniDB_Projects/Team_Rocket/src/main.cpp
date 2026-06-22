// MiniDB interactive shell.
//
//   minidb [dbpath]        start a shell against dbpath (default: "minidb")
//   minidb [dbpath] < file  run a script of semicolon-separated statements
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "minidb/db.h"

using namespace minidb;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static void print(const Result& r) {
    if (!r.plan.empty()) {
        std::cout << "-- plan --\n" << r.plan;
    }
    if (!r.ok) {
        std::cout << "Error: " << r.message << "\n";
        return;
    }
    if (r.columns.empty()) {
        std::cout << r.message << "\n";
        return;
    }
    std::vector<size_t> w(r.columns.size());
    for (size_t i = 0; i < r.columns.size(); ++i) w[i] = r.columns[i].size();
    for (const auto& row : r.rows)
        for (size_t i = 0; i < row.size(); ++i) w[i] = std::max(w[i], row[i].size());

    auto line = [&]() {
        std::cout << "+";
        for (size_t i = 0; i < w.size(); ++i) std::cout << std::string(w[i] + 2, '-') << "+";
        std::cout << "\n";
    };
    auto emit = [&](const std::vector<std::string>& cells) {
        std::cout << "|";
        for (size_t i = 0; i < cells.size(); ++i)
            std::cout << " " << cells[i] << std::string(w[i] - cells[i].size(), ' ') << " |";
        std::cout << "\n";
    };

    line();
    emit(r.columns);
    line();
    for (const auto& row : r.rows) emit(row);
    line();
    std::cout << r.message << "\n";
}

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "minidb";
    Database db(path);
    bool interactive = isatty(0);

    std::string buf, line;
    if (interactive) std::cout << "MiniDB shell. End statements with ';'. Ctrl-D to exit.\nminidb> ";
    while (std::getline(std::cin, line)) {
        size_t comment = line.find("--");  // strip line comments
        if (comment != std::string::npos) line.erase(comment);
        buf += line + "\n";
        size_t semi;
        while ((semi = buf.find(';')) != std::string::npos) {
            std::string stmt = trim(buf.substr(0, semi));
            buf.erase(0, semi + 1);
            if (stmt.empty()) continue;
            if (interactive) std::cout << "\n";
            print(db.execute(stmt));
        }
        if (interactive) std::cout << "minidb> ";
    }
    if (interactive) std::cout << "\n";
    db.checkpoint();
    return 0;
}
