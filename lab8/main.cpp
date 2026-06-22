#include "transaction_manager.h"

#include <iostream>

using namespace std;

int main()
{
    TransactionManager manager;

    Transaction t1 = manager.begin();
    Transaction t2 = manager.begin();

    manager.read(t1, "A");
    manager.write(t1, "A", 110);

    manager.read(t2, "B");
    manager.write(t2, "B", 190);

    cout << "\nDeadlock scenario\n";
    manager.write(t1, "B", 210);
    manager.write(t2, "A", 90);

    manager.show();

    cout << "\nAfter deadlock resolution\n";
    manager.write(t1, "B", 210);
    manager.commit(t1);
    manager.commit(t2);

    Transaction t3 = manager.begin();
    manager.read(t3, "A");
    manager.read(t3, "B");
    manager.commit(t3);

    manager.show();
    return 0;
}