#include "wal_manager.cpp"
#include <iostream>

int main() {
    WALManager wal("wal.log");

    wal.append("PUT user:1 Alice");
    wal.append("PUT user:2 Bob");
    wal.append("DELETE user:1");

    auto records = wal.recover();

    for (const auto& record : records) {
        std::cout << record << '\n';
    }
}