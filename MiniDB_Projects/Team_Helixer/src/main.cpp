// MiniDB interactive shell (REPL).
//
//   ./minidb [db_base]      (default db_base = "minidb")
//
// Reads one SQL statement per line, runs it through the full engine, and prints
// results. Interactive transactions are supported via BEGIN / COMMIT / ABORT;
// any other statement outside a transaction runs auto-committed.
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include "engine/database.h"

using namespace minidb;

static std::string upper(std::string s) {
    for (char &c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
static std::string first_word(const std::string &s) {
    std::istringstream is(s);
    std::string w; is >> w; return upper(w);
}

// Pretty-print a SELECT result as an aligned text table.
static void print_result(const QueryResult &r) {
    if (!r.ok) { std::cout << "ERROR: " << r.message << "\n"; return; }
    if (r.columns.empty()) { std::cout << r.message << "\n"; return; }

    std::vector<size_t> w(r.columns.size());
    for (size_t i = 0; i < r.columns.size(); ++i) w[i] = r.columns[i].size();
    for (const auto &row : r.rows)
        for (size_t i = 0; i < row.size(); ++i)
            w[i] = std::max(w[i], row[i].to_string().size());

    auto sep = [&]() {
        std::cout << "+";
        for (size_t i = 0; i < w.size(); ++i) { std::cout << std::string(w[i] + 2, '-') << "+"; }
        std::cout << "\n";
    };
    sep();
    std::cout << "|";
    for (size_t i = 0; i < r.columns.size(); ++i)
        std::cout << " " << r.columns[i] << std::string(w[i] - r.columns[i].size(), ' ') << " |";
    std::cout << "\n";
    sep();
    for (const auto &row : r.rows) {
        std::cout << "|";
        for (size_t i = 0; i < row.size(); ++i) {
            std::string v = row[i].to_string();
            std::cout << " " << v << std::string(w[i] - v.size(), ' ') << " |";
        }
        std::cout << "\n";
    }
    sep();
    std::cout << r.message << "\n";
}

int main(int argc, char **argv) {
    std::string base = (argc > 1) ? argv[1] : "minidb";
    Database db(base);
    std::cout << "MiniDB shell  (db='" << base << "')\n"
                 "Type SQL ending without a semicolon needed. Commands: "
                 ".tables  .help  .exit\n";

    Transaction *cur = nullptr; // current interactive transaction (null=autocommit)
    std::string line;
    while (true) {
        std::cout << (cur ? "minidb*> " : "minidb> ") << std::flush;
        if (!std::getline(std::cin, line)) break;

        // Trim whitespace and a trailing ';'.
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n;");
        std::string sql = line.substr(a, b - a + 1);
        if (sql.empty()) continue;

        std::string kw = first_word(sql);
        if (kw == ".EXIT" || kw == ".QUIT") break;
        if (kw == ".HELP") {
            std::cout << "SQL: CREATE TABLE / INSERT / SELECT (WHERE, JOIN) / DELETE\n"
                         "Txn: BEGIN | COMMIT | ABORT\n"
                         "Meta: .tables  .exit\n";
            continue;
        }
        if (kw == ".TABLES") {
            for (auto &n : db.catalog().table_names()) std::cout << "  " << n << "\n";
            continue;
        }

        // Transaction control.
        if (kw == "BEGIN" || kw == "START") {
            if (cur) { std::cout << "already in a transaction\n"; continue; }
            cur = db.begin();
            std::cout << "BEGIN\n";
            continue;
        }
        if (kw == "COMMIT") {
            if (!cur) { std::cout << "no active transaction\n"; continue; }
            db.commit(cur); cur = nullptr;
            std::cout << "COMMIT\n";
            continue;
        }
        if (kw == "ABORT" || kw == "ROLLBACK") {
            if (!cur) { std::cout << "no active transaction\n"; continue; }
            db.abort(cur); cur = nullptr;
            std::cout << "ROLLBACK\n";
            continue;
        }

        // Regular statement. If a deadlock aborts the current txn, reset it.
        QueryResult r = db.execute(sql, cur);
        print_result(r);
        if (!r.ok && cur && r.message.find("aborted") != std::string::npos) cur = nullptr;
    }

    if (cur) db.abort(cur);
    std::cout << "bye\n";
    return 0;
}
