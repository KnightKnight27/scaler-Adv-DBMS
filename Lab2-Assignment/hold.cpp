// Opens students.db and holds it open briefly so the fd can be inspected via
// /proc/<PID>/fd — proving the DB is a plain file owned by THIS process.
// Build: g++ -O2 -o hold hold.cpp -lsqlite3
#include <sqlite3.h>
#include <iostream>
#include <unistd.h>
int main() {
    sqlite3* db = nullptr;
    sqlite3_open("students.db", &db);
    sqlite3_exec(db, "BEGIN; SELECT count(*) FROM students;", nullptr, nullptr, nullptr);
    std::cout << "holding students.db open, PID " << getpid() << "\n" << std::flush;
    sleep(3);
    sqlite3_close(db);
    return 0;
}
