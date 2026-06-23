#include <iostream>
#include <string>

#include "Database.hpp"

using namespace std;
using namespace minidb;

int main() {
    cout << "===========================================\n";
    cout << "  MiniDB — Minimal Educational Database\n";
    cout << "  Type SQL ending with ;  (exit to quit)\n";
    cout << "===========================================\n";

    Database db;
    if (!db.open("default")) {
        cerr << "Failed to open database.\n";
        return 1;
    }

    int current_txn = 0;
    string line;
    while (true) {
        cout << "minidb> ";
        if (!getline(cin, line)) break;
        if (line == "exit" || line == "quit") break;
        if (line.empty()) continue;
        if (line == "SET BATCH ON") { db.setBatchMode(true); cout << "OK: batch mode on\n"; continue; }
        if (line == "SET BATCH OFF") { db.setBatchMode(false); cout << "OK: batch mode off\n"; continue; }
        cout << db.executeSQL(line, current_txn);
    }

    db.close();
    cout << "Goodbye.\n";
    return 0;
}
