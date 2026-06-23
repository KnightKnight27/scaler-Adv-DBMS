#include "database.h"
#include "distributed/node.h"
#include "distributed/replication.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>

static void print_records(const std::vector<Record>& records) {
    if (records.empty()) {
        std::cout << "Empty set (0 rows)" << std::endl;
        return;
    }

    std::vector<std::string> cols;
    for (const auto& pair : records[0]) {
        cols.push_back(pair.first);
    }

    std::cout << "+";
    for (const auto& col : cols) {
        std::cout << std::string(col.length() + 4, '-') << "+";
    }
    std::cout << std::endl << "|";
    for (const auto& col : cols) {
        std::cout << "  " << col << "  |";
    }
    std::cout << std::endl << "+";
    for (const auto& col : cols) {
        std::cout << std::string(col.length() + 4, '-') << "+";
    }
    std::cout << std::endl;

    for (const auto& rec : records) {
        std::cout << "|";
        for (const auto& col : cols) {
            std::string val = "";
            auto it = rec.find(col);
            if (it != rec.end()) {
                val = it->second;
            }
            std::cout << "  " << val << std::string(std::max(0, (int)col.length() - (int)val.length()), ' ') << "  |";
        }
        std::cout << std::endl;
    }

    std::cout << "+";
    for (const auto& col : cols) {
        std::cout << std::string(col.length() + 4, '-') << "+";
    }
    std::cout << std::endl;
    std::cout << records.size() << " row(s) in set" << std::endl;
}

void run_sql_console() {
    std::string db_dir = "./demo_sql_db";
    fs_compat::remove_all(db_dir);
    
    Database db(db_dir, true);
    db.execute_update("INSERT INTO students VALUES (1, 'Alice', 20)");
    db.execute_update("INSERT INTO students VALUES (2, 'Bob', 22)");
    db.execute_update("INSERT INTO students VALUES (3, 'Charlie', 21)");

    std::cout << "\n=========================================" << std::endl;
    std::cout << "        MiniDB Interactive SQL Console   " << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "Initialized 'students' table: columns (id, name, age)." << std::endl;
    std::cout << "Write your SELECT, INSERT, or DELETE queries. Type 'exit' to return.\n" << std::endl;

    std::string input;
    while (true) {
        std::cout << "minidb> ";
        std::getline(std::cin, input);
        if (input == "exit" || input == "quit") {
            break;
        }
        if (input.empty()) continue;

        try {
            if (input.rfind("SELECT", 0) == 0 || input.rfind("select", 0) == 0) {
                auto res = db.execute_query(input);
                print_records(res);
            } else if (input.rfind("INSERT", 0) == 0 || input.rfind("insert", 0) == 0 ||
                       input.rfind("DELETE", 0) == 0 || input.rfind("delete", 0) == 0) {
                int count = db.execute_update(input);
                std::cout << "Query OK, " << count << " row(s) affected." << std::endl;
            } else {
                std::cout << "Unsupported syntax! Use SELECT, INSERT, or DELETE." << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    fs_compat::remove_all(db_dir);
}

void step_through_milestones() {
    std::cout << "\n=========================================" << std::endl;
    std::cout << "        Milestone Walkthroughs           " << std::endl;
    std::cout << "=========================================" << std::endl;
    
    // M1: Slotted Page Layout
    std::cout << "\n[Milestone 1: Slotted-Page Layout]" << std::endl;
    uint8_t buffer[PAGE_SIZE];
    std::memset(buffer, 0, PAGE_SIZE);
    Page page(0, buffer);
    page.insert_record("{\"id\": 1, \"name\": \"Alice\"}");
    page.insert_record("{\"id\": 2, \"name\": \"Bob\"}");
    std::cout << " -> Inserted Alice (Slot 0) and Bob (Slot 1)" << std::endl;
    std::cout << " -> num_slots: " << page.num_slots << std::endl;
    std::cout << " -> free_space_ptr: " << page.free_space_ptr << std::endl;
    
    page.delete_record(0);
    std::cout << " -> Deleted Alice (Slot 0) and compacted page" << std::endl;
    std::cout << " -> slot 0 (Alice) offset: " << page.get_slot(0).first << ", length: " << page.get_slot(0).second << std::endl;
    std::cout << " -> slot 1 (Bob) offset: " << page.get_slot(1).first << ", length: " << page.get_slot(1).second << std::endl;

    // M2: B+ Tree
    std::cout << "\n[Milestone 2: B+ Tree Key-Indexing]" << std::endl;
    BPlusTree tree(3);
    std::cout << " -> Inserting PK keys: 10, 20, 5..." << std::endl;
    tree.insert(10, {0, 0});
    tree.insert(20, {0, 1});
    tree.insert(5, {0, 2});
    std::pair<int, int> rid;
    if (tree.search(20, rid)) {
        std::cout << " -> B+ Tree Search for key 20: FOUND at RID (" << rid.first << ", " << rid.second << ")" << std::endl;
    }

    // M3: Cost-Based Optimizer
    std::cout << "\n[Milestone 3: Volcano Executors & Cost-Based Optimizer]" << std::endl;
    std::string test_dir = "./demo_cbo";
    fs_compat::remove_all(test_dir);
    PageManager pm(test_dir);
    std::unordered_map<std::string, TableStats> stats;
    stats["students"] = TableStats{100, "id"};
    CostBasedOptimizer cbo(pm, stats);
    
    std::string scan_type;
    double cost = cbo.cost_scan("students", SQLParser::parse("SELECT * FROM students WHERE id = 5").where, true, scan_type);
    std::cout << " -> Query: SELECT * FROM students WHERE id = 5" << std::endl;
    std::cout << " -> CBO Chosen Plan: " << scan_type << std::endl;
    std::cout << " -> Estimated select cost: " << cost << " page reads" << std::endl;
    fs_compat::remove_all(test_dir);

    // M4: Strict 2PL Concurrency
    std::cout << "\n[Milestone 4: Strict 2PL & Waits-For Graph Deadlocks]" << std::endl;
    fs_compat::remove_all("./demo_locks");
    Database lock_db("./demo_locks", false);
    lock_db.execute_update("INSERT INTO students VALUES (1, 'Alice', 20)");
    lock_db.execute_update("INSERT INTO students VALUES (2, 'Bob', 22)");
    
    Transaction* t1 = lock_db.begin_transaction();
    Transaction* t2 = lock_db.begin_transaction();
    t1->acquire_exclusive("students", {0, 0});
    t2->acquire_exclusive("students", {0, 1});
    std::cout << " -> Transaction 1 locked Alice; Transaction 2 locked Bob." << std::endl;
    
    std::cout << " -> Running Waits-For Graph cycle solver..." << std::endl;
    bool deadlock_occurred = false;
    Thread th([&]() {
        try {
            Sleep(50);
            t1->acquire_exclusive("students", {0, 1}); // requests Bob
        } catch (const DeadlockException& e) {
            std::cout << " -> SUCCESS: " << e.what() << std::endl;
            t1->abort();
            deadlock_occurred = true;
        }
    });

    try {
        Sleep(50);
        t2->acquire_exclusive("students", {0, 0}); // requests Alice (triggers cycle)
    } catch (const DeadlockException& e) {
        std::cout << " -> SUCCESS: " << e.what() << std::endl;
        t2->abort();
        deadlock_occurred = true;
    }

    th.join();
    delete t1;
    delete t2;
    fs_compat::remove_all("./demo_locks");

    // M5: ARIES Recovery
    std::cout << "\n[Milestone 5: ARIES Crash Recovery (Analysis, Redo, Undo)]" << std::endl;
    fs_compat::remove_all("./demo_rec");
    {
        Database db("./demo_rec", true);
        db.execute_update("INSERT INTO students VALUES (10, 'RecoveredCommit', 30)"); // Auto-committed
        Transaction* t = db.begin_transaction();
        db.execute_update("INSERT INTO students VALUES (20, 'RecoveredRollback', 40)", t); // Uncommitted
        std::cout << " -> Simulating sudden database crash. committed 10, uncommitted 20." << std::endl;
    }
    
    // Restart db
    std::cout << " -> Restarting database engine and running ARIES..." << std::endl;
    Database db_restored("./demo_rec", true);
    auto res_commit = db_restored.execute_query("SELECT id, name FROM students WHERE id = 10");
    auto res_abort = db_restored.execute_query("SELECT id, name FROM students WHERE id = 20");
    
    std::cout << " -> Checking Alice (id = 10): " << (res_commit.size() == 1 ? "EXISTS (Redo replayed)" : "MISSING") << std::endl;
    std::cout << " -> Checking Bob (id = 20): " << (res_abort.size() == 0 ? "ABSENT (Undo rolled back)" : "EXISTS") << std::endl;
    fs_compat::remove_all("./demo_rec");

    // Track D: Distributed Replication
    std::cout << "\n[Track D: Distributed Replication & Network Catch-up]" << std::endl;
    fs_compat::remove_all("./demo_p");
    fs_compat::remove_all("./demo_r");
    {
        Node primary("Primary", "./demo_p", true);
        Node replica("Replica", "./demo_r", false);
        ReplicationManager rm(&primary);
        rm.add_replica(&replica);

        std::cout << " -> Primary is online, replica is online." << std::endl;
        primary.execute_update("INSERT INTO students VALUES (1, 'Replicated_Committed', 99)");
        rm.replicate();
        auto rep_res = replica.execute_query("SELECT id, name FROM students WHERE id = 1");
        std::cout << " -> Checking replica: id = 1: " << (rep_res.size() == 1 ? "EXISTS" : "MISSING") << std::endl;

        std::cout << " -> Simulating replica going offline (Network Partition)..." << std::endl;
        replica.is_online = false;
        primary.execute_update("INSERT INTO students VALUES (2, 'Replica_Stale', 101)");
        std::cout << " -> Primary accepted write: id = 2." << std::endl;

        std::cout << " -> Replica comes back online. Running replication manager catch-up diff sync..." << std::endl;
        replica.is_online = true;
        rm.replicate();
        auto rep_res2 = replica.execute_query("SELECT id, name FROM students WHERE id = 2");
        std::cout << " -> Checking replica: id = 2 after catch-up: " << (rep_res2.size() == 1 ? "EXISTS (Sync catch-up replayed)" : "MISSING") << std::endl;
    }
    fs_compat::remove_all("./demo_p");
    fs_compat::remove_all("./demo_r");
    
    std::cout << "\nWalkthrough completed!" << std::endl;
}

int main() {
    std::string choice;
    while (true) {
        std::cout << "\n=========================================" << std::endl;
        std::cout << "       MiniDB Interactive Viva System    " << std::endl;
        std::cout << "=========================================" << std::endl;
        std::cout << "1. Run SQL Console (SELECT, INSERT, DELETE)" << std::endl;
        std::cout << "2. Run Milestone-by-Milestone Walkthroughs" << std::endl;
        std::cout << "3. Exit" << std::endl;
        std::cout << "Select choice: ";
        
        std::getline(std::cin, choice);
        if (choice == "1") {
            run_sql_console();
        } else if (choice == "2") {
            step_through_milestones();
        } else if (choice == "3") {
            break;
        } else {
            std::cout << "Invalid choice! Please select 1, 2, or 3." << std::endl;
        }
    }
    return 0;
}
