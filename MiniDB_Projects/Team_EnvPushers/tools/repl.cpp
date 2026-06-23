// Interactive SQL shell for MiniDB.
//
//   ./bin/minidb [data_dir]
//
// Type SQL terminated by ';'. Meta commands start with '.':
//   .tables           list tables
//   .schema <table>   show a table's columns
//   .explain on|off   toggle printing the query plan before results
//   .help  .exit
// Transactions: BEGIN; ... COMMIT; / ABORT; run across statements in a session.
#include <iostream>
#include <sstream>
#include <string>

#include "database.hpp"

using namespace minidb;

static void print_table(const ResultSet& r) {
    std::vector<size_t> w(r.columns.size());
    for (size_t i = 0; i < r.columns.size(); ++i) w[i] = r.columns[i].size();
    for (auto& row : r.rows)
        for (size_t i = 0; i < row.size(); ++i)
            w[i] = std::max(w[i], row[i].to_string().size());

    auto sep = [&]() {
        std::cout << "+";
        for (size_t i = 0; i < w.size(); ++i) { std::cout << std::string(w[i] + 2, '-') << "+"; }
        std::cout << "\n";
    };
    sep();
    std::cout << "|";
    for (size_t i = 0; i < r.columns.size(); ++i) {
        std::cout << " " << r.columns[i] << std::string(w[i] - r.columns[i].size(), ' ') << " |";
    }
    std::cout << "\n";
    sep();
    for (auto& row : r.rows) {
        std::cout << "|";
        for (size_t i = 0; i < row.size(); ++i) {
            std::string s = row[i].to_string();
            std::cout << " " << s << std::string(w[i] - s.size(), ' ') << " |";
        }
        std::cout << "\n";
    }
    sep();
    std::cout << r.rows.size() << " row(s)\n";
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "data";
    Database db(dir);
    Transaction* session = nullptr;   // non-null while inside BEGIN..COMMIT
    bool explain = false;

    std::cout << "MiniDB interactive shell. Data dir: " << dir
              << "\nType .help for commands, .exit to quit.\n";

    std::string buffer, line;
    auto prompt = [&]() { std::cout << (session ? "minidb*> " : "minidb> ") << std::flush; };
    prompt();

    while (std::getline(std::cin, line)) {
        // Meta commands (single line, no ';').
        std::string trimmed = line;
        if (!trimmed.empty() && trimmed[0] == '.') {
            std::istringstream is(trimmed);
            std::string cmd; is >> cmd;
            if (cmd == ".exit" || cmd == ".quit") break;
            if (cmd == ".help") {
                std::cout << ".tables  .schema <t>  .explain on|off  .exit\n"
                             "SQL: CREATE TABLE, INSERT, SELECT (WHERE/JOIN/GROUP BY/ORDER BY),\n"
                             "     DELETE, UPDATE, BEGIN/COMMIT/ABORT\n";
            } else if (cmd == ".tables") {
                for (auto& t : db.catalog().tables()) std::cout << "  " << t->name << "\n";
            } else if (cmd == ".schema") {
                std::string tn; is >> tn;
                TableInfo* t = db.catalog().get_table(tn);
                if (!t) std::cout << "no such table\n";
                else for (auto& c : t->schema.columns())
                    std::cout << "  " << c.name << " " << type_name(c.type)
                              << (c.is_primary_key ? " PRIMARY KEY" : "") << "\n";
            } else if (cmd == ".explain") {
                std::string v; is >> v; explain = (v == "on");
                std::cout << "explain " << (explain ? "on" : "off") << "\n";
            } else {
                std::cout << "unknown command\n";
            }
            prompt();
            continue;
        }

        buffer += line + "\n";
        size_t semi;
        while ((semi = buffer.find(';')) != std::string::npos) {
            std::string sql = buffer.substr(0, semi + 1);
            buffer.erase(0, semi + 1);

            // Detect transaction-control keywords for session management.
            std::istringstream is(sql);
            std::string kw; is >> kw;
            for (auto& ch : kw) ch = std::toupper((unsigned char)ch);

            try {
                if (kw == "BEGIN") {
                    if (session) { std::cout << "already in a transaction\n"; }
                    else { session = db.begin(); std::cout << "BEGIN\n"; }
                } else if (kw == "COMMIT") {
                    if (!session) std::cout << "no active transaction\n";
                    else { db.commit(session); session = nullptr; std::cout << "COMMIT\n"; }
                } else if (kw == "ABORT" || kw == "ROLLBACK") {
                    if (!session) std::cout << "no active transaction\n";
                    else { db.abort(session); session = nullptr; std::cout << "ABORT\n"; }
                } else {
                    ResultSet r = session ? db.execute(sql, session) : db.execute(sql);
                    if (!r.ok) { std::cout << "ERROR: " << r.message << "\n"; }
                    else if (r.is_select) {
                        if (explain) std::cout << r.explain;
                        print_table(r);
                    } else {
                        std::cout << r.message << "\n";
                    }
                }
            } catch (const std::exception& e) {
                std::cout << "ERROR: " << e.what() << "\n";
                if (session) { db.abort(session); session = nullptr; }
            }
        }
        prompt();
    }
    if (session) db.abort(session);
    std::cout << "\nbye\n";
    return 0;
}
