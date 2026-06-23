#include "transaction_manager.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>

using namespace std;

mutex log_mtx;
void print_log(const string& msg) {
    lock_guard<mutex> lk(log_mtx);
    cout << msg << endl;
}

void TestMVCC() {
    print_log("--- Test MVCC ---");
    LockManager lm;
    Database db(lm);
    TransactionManager tm(db, lm);
    
    db.InsertInitialData(100, "Version 1", 1);
    
    auto t1 = tm.Begin(); // Start T1
    print_log("T" + to_string(t1->id) + " started.");
    
    auto t2 = tm.Begin(); // Start T2
    print_log("T" + to_string(t2->id) + " started.");
    
    // T1 reads
    auto val = db.Read(*t1, 100, tm.GetCommittedTxns());
    print_log("T" + to_string(t1->id) + " reads Row 100: " + val.value_or("NULL"));
    
    // T2 updates
    db.Update(*t2, 100, "Version 2");
    print_log("T" + to_string(t2->id) + " updates Row 100 to 'Version 2'.");
    tm.Commit(*t2);
    print_log("T" + to_string(t2->id) + " commits.");
    
    // T1 reads again (should see 'Version 1' because of snapshot isolation)
    val = db.Read(*t1, 100, tm.GetCommittedTxns());
    print_log("T" + to_string(t1->id) + " reads Row 100 again (MVCC Snapshot): " + val.value_or("NULL"));
    
    tm.Commit(*t1);
    
    auto t3 = tm.Begin();
    val = db.Read(*t3, 100, tm.GetCommittedTxns());
    print_log("T" + to_string(t3->id) + " reads Row 100: " + val.value_or("NULL"));
    tm.Commit(*t3);
    print_log("");
}

void TestDeadlock() {
    print_log("--- Test 2PL Deadlock Detection ---");
    LockManager lm;
    Database db(lm);
    TransactionManager tm(db, lm);
    
    db.InsertInitialData(1, "A", 1);
    db.InsertInitialData(2, "B", 1);
    
    auto t1 = tm.Begin();
    auto t2 = tm.Begin();
    
    std::thread thread1([&]() {
        print_log("T" + to_string(t1->id) + " updating Row 1...");
        db.Update(*t1, 1, "A2");
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // wait for T2 to acquire row 2
        
        print_log("T" + to_string(t1->id) + " trying to update Row 2...");
        if (!db.Update(*t1, 2, "B2")) {
            print_log("T" + to_string(t1->id) + " aborted due to DEADLOCK!");
            tm.Abort(*t1);
        } else {
            print_log("T" + to_string(t1->id) + " successfully updated Row 2.");
            tm.Commit(*t1);
        }
    });
    
    std::thread thread2([&]() {
        print_log("T" + to_string(t2->id) + " updating Row 2...");
        db.Update(*t2, 2, "B2");
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // wait for T1 to acquire row 1
        
        print_log("T" + to_string(t2->id) + " trying to update Row 1...");
        if (!db.Update(*t2, 1, "A2")) {
            print_log("T" + to_string(t2->id) + " aborted due to DEADLOCK!");
            tm.Abort(*t2);
        } else {
            print_log("T" + to_string(t2->id) + " successfully updated Row 1.");
            tm.Commit(*t2);
        }
    });
    
    thread1.join();
    thread2.join();
}

int main() {
    TestMVCC();
    TestDeadlock();
    return 0;
}
