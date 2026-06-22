#include "TransactionManager.hpp"

void SetupBaseData(TransactionManager& tm) {
    int t0 = tm.Begin();
    tm.Write(t0, 100, 500); 
    tm.Write(t0, 200, 800); 
    tm.Commit(t0);
    std::cout << "--- Base Data Inserted ---\n\n";
}

int main() {
    TransactionManager tm;
    SetupBaseData(tm);

    std::cout << "=== Scenario 1: MVCC Snapshot Isolation (Readers don't block writers) ===\n";
    int t1 = tm.Begin();
    int t2 = tm.Begin();

    int val;
    tm.Read(t1, 100, val);     
    tm.Write(t2, 100, 999);    
    tm.Read(t1, 100, val);     
    tm.Commit(t2);
    tm.Read(t1, 100, val);     
    tm.Commit(t1);

    std::cout << "\n=== Scenario 2: Write-Write Conflict (2PL Blocks) ===\n";
    int t3 = tm.Begin();
    int t4 = tm.Begin();

    tm.Write(t3, 200, 1000);    
    tm.Write(t4, 200, 2000);    
    tm.Commit(t3);             

    tm.Abort(t4); 

    std::cout << "\n=== Scenario 3: Deadlock Detection & Resolution ===\n";
    int t5 = tm.Begin();
    int t6 = tm.Begin();

    tm.Write(t5, 100, 1111);
    
    tm.Write(t6, 200, 2222);

    tm.Write(t5, 200, 3333);

    tm.Write(t6, 100, 4444); 

    tm.Commit(t5);

    return 0;
}