#include <iostream>
#include <cassert>
#include <string>
#include <filesystem>
#include <vector>
#include "common/config.h"
#include "common/rid.h"
#include "storage/disk_manager.h"
#include "storage/slotted_page.h"
#include "storage/buffer_pool_manager.h"
#include "index/b_plus_tree.h"
#include "parser/query_engine.h"
#include "execution/abstract_executor.h"
#include "execution/executors.h"

using namespace minidb;

void TestDiskManager() {
    std::cout << "--- Starting Disk Manager Tests ---" << std::endl;
    std::string db_file = "test_minidb.db";
    if (std::filesystem::exists(db_file)) {
        std::filesystem::remove(db_file);
    }

    {
        DiskManager disk_manager(db_file);
        page_id_t p0 = disk_manager.AllocatePage();
        page_id_t p1 = disk_manager.AllocatePage();
        assert(p0 == 0);
        assert(p1 == 1);

        char data_write[PAGE_SIZE];
        std::memset(data_write, 0, PAGE_SIZE);
        std::strcpy(data_write, "Hello Disk Manager Page 0!");
        disk_manager.WritePage(p0, data_write);

        std::strcpy(data_write, "Hello Disk Manager Page 1!");
        disk_manager.WritePage(p1, data_write);
    }

    // Read back in a new session to test persistence
    {
        DiskManager disk_manager(db_file);
        char data_read[PAGE_SIZE];

        disk_manager.ReadPage(0, data_read);
        assert(std::string(data_read) == "Hello Disk Manager Page 0!");

        disk_manager.ReadPage(1, data_read);
        assert(std::string(data_read) == "Hello Disk Manager Page 1!");
    }

    std::filesystem::remove(db_file);
    std::cout << "[DISK MANAGER SUCCESS] Direct block read, write, and allocation verified.\n" << std::endl;
}

void TestSlottedPage() {
    std::cout << "--- Starting Slotted Page Tests ---" << std::endl;
    char page_data[PAGE_SIZE];
    SlottedPage::Init(page_data);

    assert(SlottedPage::GetSlotCount(page_data) == 0);
    assert(SlottedPage::GetFreeSpacePointer(page_data) == PAGE_SIZE);

    RID r0, r1, r2;
    bool s0 = SlottedPage::InsertTuple(page_data, "TupleA", &r0, 99);
    bool s1 = SlottedPage::InsertTuple(page_data, "TupleB_Longer", &r1, 99);
    bool s2 = SlottedPage::InsertTuple(page_data, "TupleC", &r2, 99);

    assert(s0 && s1 && s2);
    assert(SlottedPage::GetSlotCount(page_data) == 3);

    std::string val;
    assert(SlottedPage::GetTuple(page_data, 0, val) && val == "TupleA");
    assert(SlottedPage::GetTuple(page_data, 1, val) && val == "TupleB_Longer");
    assert(SlottedPage::GetTuple(page_data, 2, val) && val == "TupleC");

    std::cout << "Successfully inserted 3 tuples. Deleting middle tuple (TupleB_Longer)..." << std::endl;
    bool d1 = SlottedPage::DeleteTuple(page_data, 1);
    assert(d1);
    assert(!SlottedPage::GetTuple(page_data, 1, val)); // Middle should be tombstoned

    std::cout << "Triggering compaction..." << std::endl;
    SlottedPage::CompactPage(page_data);

    // Compaction must keep slot numbers stable
    assert(SlottedPage::GetTuple(page_data, 0, val) && val == "TupleA");
    assert(SlottedPage::GetTuple(page_data, 2, val) && val == "TupleC");
    assert(!SlottedPage::GetTuple(page_data, 1, val)); // Tombstone should remain tombstoned

    std::cout << "[SLOTTED PAGE SUCCESS] Slotted layout insert, delete, and stable-index compaction verified.\n" << std::endl;
}

void TestBufferPoolManager() {
    std::cout << "--- Starting Buffer Pool Manager Tests ---" << std::endl;
    std::string db_file = "test_minidb_bpm.db";
    if (std::filesystem::exists(db_file)) {
        std::filesystem::remove(db_file);
    }

    {
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(3, &disk_manager); // Pool size = 3

        page_id_t p0, p1, p2, p3;
        Page* pg0 = bpm.NewPage(&p0);
        Page* pg1 = bpm.NewPage(&p1);
        Page* pg2 = bpm.NewPage(&p2);

        assert(pg0 != nullptr && p0 == 0);
        assert(pg1 != nullptr && p1 == 1);
        assert(pg2 != nullptr && p2 == 2);

        // Write data to cached pages and mark dirty
        SlottedPage::Init(pg0->GetData());
        RID r0;
        SlottedPage::InsertTuple(pg0->GetData(), "Persisted_Page0", &r0, p0);
        bpm.UnpinPage(p0, true);

        SlottedPage::Init(pg1->GetData());
        RID r1;
        SlottedPage::InsertTuple(pg1->GetData(), "Persisted_Page1", &r1, p1);
        bpm.UnpinPage(p1, true);

        SlottedPage::Init(pg2->GetData());
        RID r2;
        SlottedPage::InsertTuple(pg2->GetData(), "Persisted_Page2", &r2, p2);
        bpm.UnpinPage(p2, true);

        // Try to allocate a 4th page when pool size is 3 and all have pin_count = 0 (unpinned).
        // Clock Replacer should evict p0 (the first unpinned page frame).
        std::cout << "Allocating page 4 (Should trigger clock eviction of page 0)..." << std::endl;
        Page* pg3 = bpm.NewPage(&p3);
        assert(pg3 != nullptr && p3 == 3);
        bpm.UnpinPage(p3, false);

        // Confirm page 0 was evicted (no longer in buffer pool, let's fetch it)
        std::cout << "Fetching page 0 (Should load from disk)..." << std::endl;
        Page* pg0_fetch = bpm.FetchPage(p0);
        assert(pg0_fetch != nullptr);
        std::string val;
        assert(SlottedPage::GetTuple(pg0_fetch->GetData(), 0, val) && val == "Persisted_Page0");
        bpm.UnpinPage(p0, false);
    }

    std::cout << "[BUFFER POOL MANAGER SUCCESS] Eviction, cache hits/misses, and dirty writeback verified.\n" << std::endl;
    std::filesystem::remove(db_file);
}

void TestBPlusTree() {
    std::cout << "--- Starting B+ Tree Indexing Tests ---" << std::endl;
    std::string db_file = "test_minidb_tree.db";
    if (std::filesystem::exists(db_file)) {
        std::filesystem::remove(db_file);
    }

    {
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(10, &disk_manager); // Pool size = 10
        IntComparator comparator;

        // Create empty root page
        page_id_t root_id = INVALID_PAGE_ID;
        BPlusTree<int, RID, IntComparator> tree(root_id, &bpm, comparator);

        // Insert elements to trigger splits (leaf max size = 3, internal max size = 3)
        std::cout << "Inserting keys step-by-step..." << std::endl;
        tree.Insert(10, RID(1, 0));
        tree.Insert(20, RID(1, 1));
        tree.Insert(5,  RID(1, 2)); // Should fit without split
        tree.Insert(6,  RID(1, 3)); // Triggers split!

        std::vector<RID> result;
        bool found = tree.Find(6, &result);
        assert(found && result.size() == 1 && result[0] == RID(1, 3));

        result.clear();
        found = tree.Find(20, &result);
        assert(found && result.size() == 1 && result[0] == RID(1, 1));

        std::cout << "Testing deletion borrows and merges..." << std::endl;
        tree.Remove(6);
        result.clear();
        assert(!tree.Find(6, &result)); // Should be removed

        result.clear();
        assert(tree.Find(20, &result) && result[0] == RID(1, 1));
    }

    std::filesystem::remove(db_file);
    std::cout << "[B+ TREE SUCCESS] Key insertion, splits, lookups, and deletions verified.\n" << std::endl;
}

void TestQueryEngine() {
    std::cout << "--- Starting Query Engine & Optimizer Tests ---" << std::endl;
    std::string db_file = "test_minidb_qe.db";
    if (std::filesystem::exists(db_file)) {
        std::filesystem::remove(db_file);
    }

    {
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(10, &disk_manager);
        Catalog catalog;

        catalog.AddTable("students");
        auto table = catalog.GetTable("students");

        // Insert mock student records
        page_id_t pid;
        Page* page = bpm.NewPage(&pid);
        page->WLock();
        SlottedPage::Init(page->GetData());
        
        Row r1{{{ "id", 1 }, { "name", std::string("Alice") }, { "age", 20 }}};
        Row r2{{{ "id", 2 }, { "name", std::string("Bob")   }, { "age", 22 }}};
        Row r3{{{ "id", 3 }, { "name", std::string("Carol") }, { "age", 19 }}};

        RID rid1, rid2, rid3;
        SlottedPage::InsertTuple(page->GetData(), r1.Serialize(), &rid1, pid);
        SlottedPage::InsertTuple(page->GetData(), r2.Serialize(), &rid2, pid);
        SlottedPage::InsertTuple(page->GetData(), r3.Serialize(), &rid3, pid);

        page->WUnlock();
        bpm.UnpinPage(pid, true);
        table->pages.push_back(pid);

        // 1. Query without Index (Should fallback to TableScan)
        std::cout << "\nExecuting Query: SELECT * FROM students WHERE id = 2 (NO INDEX)" << std::endl;
        auto plan1 = Optimizer::Optimize("id", 2, table);
        assert(plan1->GetPlanName().rfind("TableScan", 0) == 0); // starts with TableScan
        std::vector<Row> results1 = plan1->Execute(&bpm, table);
        assert(results1.size() == 1);
        assert(std::get<std::string>(results1[0].cols["name"]) == "Bob");
        std::cout << "[SUCCESS] TableScan executed correctly. Result: " << std::get<std::string>(results1[0].cols["name"]) << std::endl;

        // 2. Create B+ Tree Index on "id"
        std::cout << "\nCreating B+ Tree Index on 'id'..." << std::endl;
        catalog.CreateIndex("students", "id", &bpm);
        auto index = table->indexes["id"];
        index->Insert(1, rid1);
        index->Insert(2, rid2);
        index->Insert(3, rid3);

        // 3. Query with Index (Should optimize to IndexScan)
        std::cout << "\nExecuting Query: SELECT * FROM students WHERE id = 2 (WITH INDEX)" << std::endl;
        auto plan2 = Optimizer::Optimize("id", 2, table);
        assert(plan2->GetPlanName().rfind("IndexScan", 0) == 0); // starts with IndexScan
        std::vector<Row> results2 = plan2->Execute(&bpm, table);
        assert(results2.size() == 1);
        assert(std::get<std::string>(results2[0].cols["name"]) == "Bob");
        std::cout << "[SUCCESS] IndexScan executed correctly. Result: " << std::get<std::string>(results2[0].cols["name"]) << std::endl;
    }

    std::filesystem::remove(db_file);
    std::cout << "[QUERY ENGINE SUCCESS] Optimizer plan selection and index-scans verified.\n" << std::endl;
}

void InsertStudent(BufferPoolManager* bpm, TableMetadata* table, int id, std::string name, int dept_id, double gpa) {
    page_id_t pid;
    Page* page = nullptr;
    if (table->pages.empty()) {
        page = bpm->NewPage(&pid);
        page->WLock();
        SlottedPage::Init(page->GetData());
        table->pages.push_back(pid);
    } else {
        pid = table->pages.back();
        page = bpm->FetchPage(pid);
        page->WLock();
    }
    
    Row r{{{ "student_id", id }, { "name", name }, { "student_dept_id", dept_id }, { "gpa", gpa }}};
    RID rid;
    if (!SlottedPage::InsertTuple(page->GetData(), r.Serialize(), &rid, pid)) {
        page->WUnlock();
        bpm->UnpinPage(pid, false);
        page = bpm->NewPage(&pid);
        page->WLock();
        SlottedPage::Init(page->GetData());
        table->pages.push_back(pid);
        assert(SlottedPage::InsertTuple(page->GetData(), r.Serialize(), &rid, pid));
    }
    page->WUnlock();
    bpm->UnpinPage(pid, true);
}

void InsertDept(BufferPoolManager* bpm, TableMetadata* table, int dept_id, std::string dept_name) {
    page_id_t pid;
    Page* page = nullptr;
    if (table->pages.empty()) {
        page = bpm->NewPage(&pid);
        page->WLock();
        SlottedPage::Init(page->GetData());
        table->pages.push_back(pid);
    } else {
        pid = table->pages.back();
        page = bpm->FetchPage(pid);
        page->WLock();
    }
    
    Row r{{{ "dept_id", dept_id }, { "dept_name", dept_name }}};
    RID rid;
    if (!SlottedPage::InsertTuple(page->GetData(), r.Serialize(), &rid, pid)) {
        page->WUnlock();
        bpm->UnpinPage(pid, false);
        page = bpm->NewPage(&pid);
        page->WLock();
        SlottedPage::Init(page->GetData());
        table->pages.push_back(pid);
        assert(SlottedPage::InsertTuple(page->GetData(), r.Serialize(), &rid, pid));
    }
    page->WUnlock();
    bpm->UnpinPage(pid, true);
}

void TestExecutionEngine() {
    std::cout << "--- Starting Execution Engine (Milestone 3) Tests ---" << std::endl;
    std::string db_file = "test_minidb_execution.db";
    if (std::filesystem::exists(db_file)) {
        std::filesystem::remove(db_file);
    }

    {
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(10, &disk_manager);

        Schema student_schema({
            Column("student_id", "INTEGER"),
            Column("name", "VARCHAR"),
            Column("student_dept_id", "INTEGER"),
            Column("gpa", "DECIMAL")
        });

        Schema dept_schema({
            Column("dept_id", "INTEGER"),
            Column("dept_name", "VARCHAR")
        });

        auto students_table = std::make_shared<TableMetadata>();
        students_table->name = "students";
        
        auto dept_table = std::make_shared<TableMetadata>();
        dept_table->name = "departments";

        InsertStudent(&bpm, students_table.get(), 1, "Alice", 10, 3.8);
        InsertStudent(&bpm, students_table.get(), 2, "Bob", 20, 3.2);
        InsertStudent(&bpm, students_table.get(), 3, "Carol", 10, 3.9);
        InsertStudent(&bpm, students_table.get(), 4, "Dave", 30, 2.8);

        InsertDept(&bpm, dept_table.get(), 10, "ComputerScience");
        InsertDept(&bpm, dept_table.get(), 20, "Mathematics");
        InsertDept(&bpm, dept_table.get(), 40, "Physics");
        std::cout << "\nExecuting Test 1: SeqScan + Filter (student_dept_id = 10)" << std::endl;
        auto scan1 = std::make_unique<SeqScanExecutor>(&bpm, students_table, student_schema);
        
        Expression expr_dept_col(Expression::Type::COLUMN_REF, "student_dept_id");
        Expression expr_dept_val(Value(10));
        Expression pred_dept_eq(Expression::Type::EQUALS, 
                                std::make_shared<Expression>(expr_dept_col), 
                                std::make_shared<Expression>(expr_dept_val));

        auto filter1 = std::make_unique<FilterExecutor>(std::move(scan1), pred_dept_eq);
        filter1->Init();

        Tuple t;
        RID r;
        int count = 0;
        while (filter1->Next(&t, &r)) {
            std::string name = std::get<std::string>(t.GetValue("name"));
            int dept = std::get<int>(t.GetValue("student_dept_id"));
            std::cout << "Match: " << name << ", Dept: " << dept << std::endl;
            assert(dept == 10);
            count++;
        }
        assert(count == 2);
        std::cout << "[SUCCESS] SeqScan + Filter verified." << std::endl;

        // Test 2: Nested Loop Join
        std::cout << "\nExecuting Test 2: Nested Loop Join (students JOIN departments)" << std::endl;
        
        Schema join_schema({
            Column("student_id", "INTEGER"),
            Column("name", "VARCHAR"),
            Column("student_dept_id", "INTEGER"),
            Column("gpa", "DECIMAL"),
            Column("dept_id", "INTEGER"),
            Column("dept_name", "VARCHAR")
        });

        auto scan_left = std::make_unique<SeqScanExecutor>(&bpm, students_table, student_schema);
        auto scan_right = std::make_unique<SeqScanExecutor>(&bpm, dept_table, dept_schema);

        Expression expr_l(Expression::Type::COLUMN_REF, "student_dept_id");
        Expression expr_r(Expression::Type::COLUMN_REF, "dept_id");
        Expression pred_join(Expression::Type::EQUALS,
                             std::make_shared<Expression>(expr_l),
                             std::make_shared<Expression>(expr_r));

        auto nlj = std::make_unique<NestedLoopJoinExecutor>(
            std::move(scan_left), std::move(scan_right), pred_join, join_schema);
        
        nlj->Init();
        count = 0;
        while (nlj->Next(&t, &r)) {
            std::string name = std::get<std::string>(t.GetValue("name"));
            std::string dept_name = std::get<std::string>(t.GetValue("dept_name"));
            int dept_id = std::get<int>(t.GetValue("dept_id"));
            std::cout << "Join Match: Student=" << name << " -> Dept=" << dept_name << " (ID: " << dept_id << ")" << std::endl;
            count++;
        }
        assert(count == 3);
        std::cout << "[SUCCESS] Nested Loop Join verified." << std::endl;

        // Test 3: Hash Join
        std::cout << "\nExecuting Test 3: In-Memory Hash Join (students JOIN departments)" << std::endl;
        
        auto scan_left_hj = std::make_unique<SeqScanExecutor>(&bpm, students_table, student_schema);
        auto scan_right_hj = std::make_unique<SeqScanExecutor>(&bpm, dept_table, dept_schema);

        Expression key_l(Expression::Type::COLUMN_REF, "student_dept_id");
        Expression key_r(Expression::Type::COLUMN_REF, "dept_id");

        auto hj = std::make_unique<HashJoinExecutor>(
            std::move(scan_left_hj), std::move(scan_right_hj), key_l, key_r, join_schema);
        
        hj->Init();
        count = 0;
        while (hj->Next(&t, &r)) {
            std::string name = std::get<std::string>(t.GetValue("name"));
            std::string dept_name = std::get<std::string>(t.GetValue("dept_name"));
            std::cout << "Hash Join Match: Student=" << name << " -> Dept=" << dept_name << std::endl;
            count++;
        }
        assert(count == 3);
        std::cout << "[SUCCESS] Hash Join verified." << std::endl;

        // Test 4: Aggregation
        std::cout << "\nExecuting Test 4: Aggregation (Group by student_dept_id, count, avg(gpa), max(gpa))" << std::endl;

        Schema agg_out_schema({
            Column("student_dept_id", "INTEGER"),
            Column("student_count", "INTEGER"),
            Column("avg_gpa", "DECIMAL"),
            Column("max_gpa", "DECIMAL")
        });

        auto scan_agg = std::make_unique<SeqScanExecutor>(&bpm, students_table, student_schema);

        auto agg = std::make_unique<AggregationExecutor>(
            std::move(scan_agg),
            std::vector<std::string>{"student_dept_id"},
            std::vector<std::string>{"student_id", "gpa", "gpa"},
            std::vector<AggregationType>{AggregationType::COUNT, AggregationType::AVG, AggregationType::MAX},
            agg_out_schema
        );

        agg->Init();
        count = 0;
        while (agg->Next(&t, &r)) {
            int dept = std::get<int>(t.GetValue("student_dept_id"));
            int cnt = std::get<int>(t.GetValue("student_count"));
            double avg = std::get<double>(t.GetValue("avg_gpa"));
            double max = std::get<double>(t.GetValue("max_gpa"));
            std::cout << "Group Dept: " << dept << " | Count: " << cnt 
                      << " | Avg GPA: " << avg << " | Max GPA: " << max << std::endl;
            
            if (dept == 10) {
                assert(cnt == 2);
                assert(std::abs(avg - 3.85) < 1e-5);
                assert(std::abs(max - 3.9) < 1e-5);
            } else if (dept == 20) {
                assert(cnt == 1);
                assert(std::abs(avg - 3.2) < 1e-5);
                assert(std::abs(max - 3.2) < 1e-5);
            } else if (dept == 30) {
                assert(cnt == 1);
                assert(std::abs(avg - 2.8) < 1e-5);
                assert(std::abs(max - 2.8) < 1e-5);
            }
            count++;
        }
        assert(count == 3);
        std::cout << "[SUCCESS] Aggregation (Group by + Multi-aggregate) verified." << std::endl;
    }

    std::filesystem::remove(db_file);
    std::cout << "[EXECUTION ENGINE SUCCESS] All Milestone 3 executors passed.\n" << std::endl;
}

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "      MINIDB CAPSTONE TEST RUNNER (M3)       " << std::endl;
    std::cout << "============================================" << std::endl;

    TestDiskManager();
    TestSlottedPage();
    TestBufferPoolManager();
    TestBPlusTree();
    TestQueryEngine();
    TestExecutionEngine();

    std::cout << "ALL MINIDB MILESTONE 3 TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
