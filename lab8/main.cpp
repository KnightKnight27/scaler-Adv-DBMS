#include "transaction_manager.h"

#include <iostream>

using namespace std;

int main()
{
    TransactionManager tm;

    Transaction first = tm.begin();
    Transaction second = tm.begin();

    tm.read(first, "A");
    tm.write(first, "A", 110);

    tm.read(second, "B");
    tm.write(second, "B", 190);

    cout << "\nCreating deadlock case\n";

    tm.write(first, "B", 210);
    tm.write(second, "A", 90);

    tm.show();

    cout << "\nContinuing after deadlock handling\n";

    tm.write(first, "B", 210);

    tm.commit(first);
    tm.commit(second);

    Transaction verify = tm.begin();

    tm.read(verify, "A");
    tm.read(verify, "B");

    tm.commit(verify);

    tm.show();

    return 0;
}