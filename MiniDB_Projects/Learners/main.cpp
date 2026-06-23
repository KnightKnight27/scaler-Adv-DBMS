#include "database.h"
#include "distributed/node.h"
#include "distributed/replication.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>

void test_storage_engine() {
    std::cout << "--- Running Storage Engine Tests ---" << std::endl;
    std::string test_dir = "./test_storage";
    fs_compat::remove_all(test_dir);
    
    // 1. Slotted Page test
    uint8_t buffer[PAGE_SIZE];
    std::memset(buffer, 0, PAGE_SIZE);
    Page page(0, buffer);
    std::string rec = "{\"id\": 1, \"name\": \"Alice\"}";
    int slot_id = page.insert_record(rec);
    assert(slot_id == 0);
    assert(page.get_record(0) == rec);
    
    {
        // 2. PageManager test
        PageManager pm(test_dir);
        int pid = pm.allocate_page("test_table");
        assert(pid == 0);
        pm.write_page("test_table", pid, buffer);
        
        uint8_t read_buffer[PAGE_SIZE];
        pm.read_page("test_table", pid, read_buffer);
        Page page_read(0, read_buffer);
        assert(page_read.get_record(0) == rec);
        
        // 3. BufferPool clock eviction test
        BufferPool bp(pm, 2);
        auto res1 = bp.fetch_page("test_table", 0);
        assert(res1.second != nullptr);
        bp.unpin_page("test_table", 0, false);
        
        int pid2 = pm.allocate_page("test_table");
        auto res2 = bp.fetch_page("test_table", pid2);
        bp.unpin_page("test_table", pid2, false);
        
        int pid3 = pm.allocate_page("test_table");
        auto res3 = bp.fetch_page("test_table", pid3);
        bp.unpin_page("test_table", pid3, false);
    }
    
    std::cout << "[PASS] Storage Engine Tests" << std::endl;
    fs_compat::remove_all(test_dir);
}

void test_bplus_tree() {
    std::cout << "--- Running B+ Tree Tests ---" << std::endl;
    BPlusTree tree(3); // Order 3
    
    // Insertions
    assert(tree.insert(10, {1, 0}));
    assert(tree.insert(20, {1, 1}));
    assert(tree.insert(5, {2, 0}));
    assert(tree.insert(15, {2, 1}));
    
    // Search
    std::pair<int, int> val;
    assert(tree.search(10, val));
    assert(val.first == 1 && val.second == 0);
    assert(tree.search(5, val));
    assert(val.first == 2 && val.second == 0);
    assert(!tree.search(99, val));
    
    // Range Search
    auto range_res = tree.range_search(5, 15);
    assert(range_res.size() == 3); // 5, 10, 15
    assert(range_res[0].first == 5);
    assert(range_res[1].first == 10);
    assert(range_res[2].first == 15);
    
    // Deletion
    assert(tree.delete_key(10));
    assert(!tree.search(10, val));
    
    std::cout << "[PASS] B+ Tree Tests" << std::endl;
}

void test_parser_optimizer() {
    std::cout << "--- Running Parser & Optimizer Tests ---" << std::endl;
    
    // SQL Parsing
    std::string sql1 = "SELECT id, name FROM students WHERE id = 5";
    SQLStatement stmt1 = SQLParser::parse(sql1);
    assert(stmt1.type == "SELECT");
    assert(stmt1.table == "students");
    assert(stmt1.columns.size() == 2);
    assert(stmt1.columns[0] == "id");
    assert(stmt1.columns[1] == "name");
    assert(stmt1.where.has_value());
    assert(stmt1.where->column == "id");
    assert(stmt1.where->op == "=");
    assert(stmt1.where->value == "5");

    std::string sql2 = "SELECT id FROM students JOIN grades ON students.id = grades.sid WHERE id > 10";
    SQLStatement stmt2 = SQLParser::parse(sql2);
    assert(stmt2.type == "SELECT");
    assert(stmt2.join.has_value());
    assert(stmt2.join->table == "grades");
    assert(stmt2.join->left_col == "students.id");
    assert(stmt2.join->right_col == "grades.sid");

    // Cost-Based Optimizer
    std::string test_dir = "./test_opt";
    fs_compat::remove_all(test_dir);
    {
        PageManager pm(test_dir);
        std::unordered_map<std::string, TableStats> stats;
        stats["students"] = TableStats{100, "id"};
        
        CostBasedOptimizer cbo(pm, stats);
        assert(cbo.get_cardinality("students") == 100);
        
        double sel = cbo.estimate_selectivity("students", stmt1.where);
        assert(sel == 0.01); // 1 / 100 selectivity on primary key
    }
    
    std::cout << "[PASS] Parser & Optimizer Tests" << std::endl;
    fs_compat::remove_all(test_dir);
}

void test_query_execution() {
    std::cout << "--- Running Query Execution Tests ---" << std::endl;
    std::string test_dir = "./test_exec";
    fs_compat::remove_all(test_dir);
    
    {
        Database db(test_dir, false);
        db.execute_update("INSERT INTO students VALUES (1, 'Alice', 20)");
        db.execute_update("INSERT INTO students VALUES (2, 'Bob', 22)");
        db.execute_update("INSERT INTO students VALUES (3, 'Charlie', 21)");
        
        // Query check
        auto res = db.execute_query("SELECT id, name FROM students WHERE id = 2");
        assert(res.size() == 1);
        assert(res[0]["id"] == "2");
        assert(res[0]["name"] == "Bob");
        
        // Range Query
        auto res_range = db.execute_query("SELECT id, name FROM students WHERE id >= 2");
        std::cout << "DEBUG: res_range size = " << res_range.size() << std::endl;
        for (const auto& r : res_range) {
            std::cout << "  DEBUG record:";
            for (const auto& pair : r) {
                std::cout << " " << pair.first << "=" << pair.second;
            }
            std::cout << std::endl;
        }
        assert(res_range.size() == 2); // Bob, Charlie
        
        // Delete Check
        int affected = db.execute_update("DELETE FROM students WHERE id = 1");
        assert(affected == 1);
        
        auto res_after = db.execute_query("SELECT * FROM students");
        assert(res_after.size() == 2); // Bob, Charlie remain
    }
    
    std::cout << "[PASS] Query Execution Tests" << std::endl;
    fs_compat::remove_all(test_dir);
}

void test_transactions_concurrency() {
    std::cout << "--- Running Concurrency & Deadlock Tests ---" << std::endl;
    std::string test_dir = "./test_tx";
    fs_compat::remove_all(test_dir);
    
    {
        Database db(test_dir, false);
        db.execute_update("INSERT INTO students VALUES (1, 'Alice', 20)");
        db.execute_update("INSERT INTO students VALUES (2, 'Bob', 22)");
    
        // Test Strict 2PL & Deadlock Detection
        Transaction* t1 = db.begin_transaction();
        Transaction* t2 = db.begin_transaction();
    
        t1->acquire_exclusive("students", {0, 0}); // locks slot 0 (Alice)
        t2->acquire_exclusive("students", {0, 1}); // locks slot 1 (Bob)
    
        bool t1_aborted = false;
        bool t2_aborted = false;
    
        Thread thread1([&]() {
            try {
                Sleep(50);
                t1->acquire_exclusive("students", {0, 1}); // requests lock on Bob (causes deadlock cycle)
                t1->commit();
            } catch (const DeadlockException& e) {
                t1_aborted = true;
                t1->abort();
            }
        });
    
        Thread thread2([&]() {
            try {
                Sleep(50);
                t2->acquire_exclusive("students", {0, 0}); // requests lock on Alice
                t2->commit();
            } catch (const DeadlockException& e) {
                t2_aborted = true;
                t2->abort();
            }
        });
    
        thread1.join();
        thread2.join();
    
        // Verify deadlock detected and at least one transaction aborted
        assert(t1_aborted || t2_aborted);
        delete t1;
        delete t2;
    }
    
    std::cout << "[PASS] Concurrency & Deadlock Tests" << std::endl;
    fs_compat::remove_all(test_dir);
}

void test_crash_recovery() {
    std::cout << "--- Running ARIES Crash Recovery Tests ---" << std::endl;
    std::string test_dir = "./test_recovery";
    fs_compat::remove_all(test_dir);
    
    // Step 1: Initialize database, write committed changes and uncommitted ones, and force mock crash
    {
        Database db(test_dir, true);
        db.execute_update("INSERT INTO students VALUES (1, 'Alice', 20)"); // Auto-committed
        
        Transaction* txn = db.begin_transaction();
        db.execute_update("INSERT INTO students VALUES (2, 'Bob', 22)", txn); // Uncommitted
        
        // Simulate a crash: don't commit txn, don't clean up buffer pool properly, just let database close
    }

    // Step 2: Restart database. Startup ARIES recovery runs automatically!
    {
        Database db(test_dir, true);
        
        // Alice should be present (committed)
        auto res1 = db.execute_query("SELECT id, name FROM students WHERE id = 1");
        assert(res1.size() == 1);
        assert(res1[0]["name"] == "Alice");
        
        // Bob should NOT be present (uncommitted transaction should have been rolled back by ARIES Undo)
        auto res2 = db.execute_query("SELECT id, name FROM students WHERE id = 2");
        assert(res2.size() == 0);
    }
    
    std::cout << "[PASS] ARIES Crash Recovery Tests" << std::endl;
    fs_compat::remove_all(test_dir);
}

void test_distributed_replication() {
    std::cout << "--- Running Distributed Replication Tests ---" << std::endl;
    std::string p_dir = "./test_primary";
    std::string r_dir = "./test_replica";
    fs_compat::remove_all(p_dir);
    fs_compat::remove_all(r_dir);

    {
        // Create Nodes
        Node primary("PrimaryNode", p_dir, true);
        Node replica("ReplicaNode", r_dir, false);
    
        ReplicationManager rm(&primary);
        rm.add_replica(&replica);
    
        // 1. Write on primary, replicate to replica
        primary.execute_update("INSERT INTO students VALUES (1, 'Alice', 20)");
        rm.replicate();
    
        // Verify replica matches
        auto res_r = replica.execute_query("SELECT id, name FROM students WHERE id = 1");
        assert(res_r.size() == 1);
        assert(res_r[0]["name"] == "Alice");
    
        // 2. Simulate replica offline, write more to primary
        replica.is_online = false;
        primary.execute_update("INSERT INTO students VALUES (2, 'Bob', 22)");
        rm.replicate(); // Replica is offline, skipped
    
        // Turn replica back online, check it was stale
        replica.is_online = true;
        auto res_stale = replica.execute_query("SELECT id, name FROM students WHERE id = 2");
        assert(res_stale.size() == 0);
    
        // 3. Catch-up Sync
        rm.replicate();
        auto res_catchup = replica.execute_query("SELECT id, name FROM students WHERE id = 2");
        assert(res_catchup.size() == 1);
        assert(res_catchup[0]["name"] == "Bob");
    }
    
    std::cout << "[PASS] Distributed Replication Tests" << std::endl;
    fs_compat::remove_all(p_dir);
    fs_compat::remove_all(r_dir);
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "   MiniDB C++ Automated Test Suite      " << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        test_storage_engine();
        test_bplus_tree();
        test_parser_optimizer();
        test_query_execution();
        test_transactions_concurrency();
        test_crash_recovery();
        test_distributed_replication();
        
        std::cout << "========================================" << std::endl;
        std::cout << "        ALL TESTS PASSED SUCCESSFULLY!  " << std::endl;
        std::cout << "========================================" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "!!! TEST RUN FAILURE: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
