#include "db_engine.hpp"

#include <iostream>

int main() {
    DbEngine db;

    TxContext t1 = db.open_txn();
    TxContext t2 = db.open_txn();

    db.read(t1, "A");
    db.write(t1, "A", 110);

    db.read(t2, "B");
    db.write(t2, "B", 190);

    std::cout << "\n=== cross-write deadlock setup ===\n";
    db.write(t1, "B", 210);  // T1 needs B held by T2
    db.write(t2, "A", 90);   // T2 needs A held by T1 → cycle → one aborts

    db.print_state();

    std::cout << "\n=== after deadlock resolution ===\n";
    db.write(t1, "B", 210);  // retry now that loser released locks
    db.commit(t1);
    db.commit(t2);

    TxContext t3 = db.open_txn();
    db.read(t3, "A");
    db.read(t3, "B");
    db.commit(t3);

    db.print_state();
    return 0;
}
