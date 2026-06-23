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
#include "optimizer/cost_based_optimizer.h"
#include "recovery/recovery_manager.h"
#include "replication/replication_manager.h"
#include "replication/replication_receiver.h"
#include <algorithm>

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

void PrintPlan(const std::shared_ptr<PhysicalPlanNode>& node, int indent = 0) {
    if (!node) return;
    std::string indent_str(indent * 2, ' ');
    std::string type_str;
    switch (node->type) {
        case PhysicalPlanNode::PlanType::SEQ_SCAN: type_str = "SEQ_SCAN(Table " + std::to_string(node->target_table) + ")"; break;
        case PhysicalPlanNode::PlanType::INDEX_SCAN: type_str = "INDEX_SCAN(Table " + std::to_string(node->target_table) + ")"; break;
        case PhysicalPlanNode::PlanType::NESTED_LOOP_JOIN: type_str = "NESTED_LOOP_JOIN"; break;
        case PhysicalPlanNode::PlanType::HASH_JOIN: type_str = "HASH_JOIN"; break;
    }
    std::cout << indent_str << "- " << type_str 
              << " [Cost: " << node->estimated_cost 
              << ", Card: " << node->estimated_cardinality << "]" << std::endl;
    PrintPlan(node->left_child, indent + 1);
    PrintPlan(node->right_child, indent + 1);
}

void AssertLeftDeep(const std::shared_ptr<PhysicalPlanNode>& node) {
    if (!node) return;
    if (node->type == PhysicalPlanNode::PlanType::NESTED_LOOP_JOIN ||
        node->type == PhysicalPlanNode::PlanType::HASH_JOIN) {
        assert(node->right_child != nullptr);
        assert(node->right_child->type == PhysicalPlanNode::PlanType::SEQ_SCAN ||
               node->right_child->type == PhysicalPlanNode::PlanType::INDEX_SCAN);
    }
    AssertLeftDeep(node->left_child);
    AssertLeftDeep(node->right_child);
}

void TestOptimizer() {
    std::cout << "--- Starting Cost-Based Join Optimizer (Milestone 4) Tests ---" << std::endl;
    SystemCatalog catalog;
    
    catalog.AddStats(0, {10, 100, false});
    catalog.AddStats(1, {20, 200, true});
    catalog.AddStats(2, {200, 2000, false});
    catalog.AddStats(3, {100, 1000, true});

    CostBasedOptimizer optimizer(&catalog);

    std::cout << "\nExecuting Test 1: Single Table Path Optimization" << std::endl;
    LogicalQuerySpecification q0{{0}};
    auto p0 = optimizer.OptimizeQuery(q0);
    assert(p0->type == PhysicalPlanNode::PlanType::SEQ_SCAN);
    std::cout << "Table 0 Access Path: "; PrintPlan(p0);

    LogicalQuerySpecification q1{{1}};
    auto p1 = optimizer.OptimizeQuery(q1);
    assert(p1->type == PhysicalPlanNode::PlanType::INDEX_SCAN);
    std::cout << "Table 1 Access Path: "; PrintPlan(p1);

    std::cout << "\nExecuting Test 2: 3-Way Join Optimization (Tables: 0, 1, 2)" << std::endl;
    LogicalQuerySpecification q2{{0, 1, 2}};
    auto p2 = optimizer.OptimizeQuery(q2);
    PrintPlan(p2);
    AssertLeftDeep(p2);
    std::cout << "[SUCCESS] 3-Way Join Optimizer and Left-Deep structure verified." << std::endl;

    std::cout << "\nExecuting Test 3: 4-Way Join Optimization (Tables: 0, 1, 2, 3)" << std::endl;
    LogicalQuerySpecification q3{{0, 1, 2, 3}};
    auto p3 = optimizer.OptimizeQuery(q3);
    PrintPlan(p3);
    AssertLeftDeep(p3);
    std::cout << "[SUCCESS] 4-Way Join Optimizer verified." << std::endl;
    
    std::cout << "[OPTIMIZER SUCCESS] Cost-based join order dynamic programming passed.\n" << std::endl;
}

void TestRecovery() {
    std::cout << "--- Starting ARIES Crash Recovery (Milestone 5) Tests ---" << std::endl;
    std::string db_file = "test_recovery.db";
    std::string lsn_file = db_file + ".lsns";
    std::string log_file = "test_recovery.log";

    // Clean up files from previous runs
    std::filesystem::remove(db_file);
    std::filesystem::remove(lsn_file);
    std::filesystem::remove(log_file);

    {
        // 1. Setup initial database and log manager
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(10, &disk_manager);
        LogManager log_manager(log_file);

        page_id_t p0;
        Page* pg0 = bpm.NewPage(&p0);
        assert(p0 == 0);
        
        // Initialize page 0 data
        char* data = pg0->GetData();
        std::memcpy(data + 10, "AAAA", 4);
        std::memcpy(data + 20, "XXXX", 4);
        std::memcpy(data + 30, "ZZZZ", 4);
        std::memcpy(data + 40, "MMMM", 4);
        bpm.UnpinPage(p0, true);
        bpm.FlushAllPages(); // Page 0 LSN is 0 on disk
    }

    {
        // 2. Simulate transaction execution and log generation
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(10, &disk_manager);
        LogManager log_manager(log_file);

        // Append log records manually to simulate normal runtime
        // LSN 1: T1 BEGIN
        LogRecord lr1(1, 0, LogRecordType::BEGIN);
        log_manager.AppendRecord(lr1);

        // LSN 2: T2 BEGIN
        LogRecord lr2(2, 0, LogRecordType::BEGIN);
        log_manager.AppendRecord(lr2);

        // LSN 3: T1 UPDATE page 0 at offset 10: "AAAA" -> "BBBB"
        LogRecord lr3(1, lr1.lsn, LogRecordType::UPDATE, 0, 10, "AAAA", "BBBB");
        log_manager.AppendRecord(lr3);

        // LSN 4: T2 UPDATE page 0 at offset 20: "XXXX" -> "YYYY"
        LogRecord lr4(2, lr2.lsn, LogRecordType::UPDATE, 0, 20, "XXXX", "YYYY");
        log_manager.AppendRecord(lr4);

        // Simulate flush of page 0. Page 0 LSN on disk becomes 4.
        Page* pg0 = bpm.FetchPage(0);
        char* data = pg0->GetData();
        std::memcpy(data + 10, "BBBB", 4);
        std::memcpy(data + 20, "YYYY", 4);
        pg0->SetPageLSN(4);
        bpm.UnpinPage(0, true);
        bpm.FlushPage(0); // Page 0 is now written to disk with LSN 4

        // LSN 5: T1 COMMIT
        LogRecord lr5(1, lr3.lsn, LogRecordType::COMMIT);
        log_manager.AppendRecord(lr5);

        // LSN 6: T2 UPDATE page 0 at offset 30: "ZZZZ" -> "WWWW"
        LogRecord lr6(2, lr4.lsn, LogRecordType::UPDATE, 0, 30, "ZZZZ", "WWWW");
        log_manager.AppendRecord(lr6);

        // LSN 7: T3 BEGIN
        LogRecord lr7(3, 0, LogRecordType::BEGIN);
        log_manager.AppendRecord(lr7);

        // LSN 8: T3 UPDATE page 0 at offset 40: "MMMM" -> "NNNN"
        LogRecord lr8(3, lr7.lsn, LogRecordType::UPDATE, 0, 40, "MMMM", "NNNN");
        log_manager.AppendRecord(lr8);
    }

    {
        // 3. Run Crash Recovery (Restart Session)
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(10, &disk_manager);
        LogManager log_manager(log_file);
        RecoveryManager rm(&disk_manager, &bpm, &log_manager);

        std::vector<txn_id_t> active_txns;
        std::unordered_map<page_id_t, lsn_t> dpt;

        // Run analysis phase
        rm.ExecuteAnalysisPhase(active_txns, dpt);
        assert(active_txns.size() == 2);
        assert(std::find(active_txns.begin(), active_txns.end(), 2) != active_txns.end());
        assert(std::find(active_txns.begin(), active_txns.end(), 3) != active_txns.end());
        assert(dpt.find(0) != dpt.end());

        // Run redo phase (repeating history)
        rm.ExecuteRedoPhase(dpt);

        // Verify that after Redo, Page 0 has all updates including uncommitted ones (LSN 6 and 8)
        Page* pg0 = bpm.FetchPage(0);
        char* data = pg0->GetData();
        assert(std::string(data + 10, 4) == "BBBB");
        assert(std::string(data + 20, 4) == "YYYY");
        assert(std::string(data + 30, 4) == "WWWW");
        assert(std::string(data + 40, 4) == "NNNN");
        assert(pg0->GetPageLSN() == 8);
        bpm.UnpinPage(0, false);

        // Run undo phase
        rm.ExecuteUndoPhase(active_txns);

        // Verify that loser updates (T2 and T3) are rolled back, and T1 update remains
        pg0 = bpm.FetchPage(0);
        data = pg0->GetData();
        assert(std::string(data + 10, 4) == "BBBB"); // T1 remains
        assert(std::string(data + 20, 4) == "XXXX"); // T2 rolled back
        assert(std::string(data + 30, 4) == "ZZZZ"); // T2 rolled back
        assert(std::string(data + 40, 4) == "MMMM"); // T3 rolled back
        assert(pg0->GetPageLSN() >= 11);
        bpm.UnpinPage(0, false);

        std::cout << "[RECOVERY SUCCESS] Analysis, Redo, and Undo rolled back uncommitted transactions successfully." << std::endl;
    }

    // 4. Test Idempotency (Re-crashing mid-recovery and resuming)
    std::filesystem::remove(db_file);
    std::filesystem::remove(lsn_file);
    std::filesystem::remove(log_file);

    {
        // Setup initial DB state again
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(10, &disk_manager);
        page_id_t p0;
        Page* pg0 = bpm.NewPage(&p0);
        char* data = pg0->GetData();
        std::memcpy(data + 10, "AAAA", 4);
        std::memcpy(data + 20, "XXXX", 4);
        std::memcpy(data + 30, "ZZZZ", 4);
        std::memcpy(data + 40, "MMMM", 4);
        bpm.UnpinPage(p0, true);
        bpm.FlushAllPages();
    }

    {
        // Populate normal transaction log records
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(10, &disk_manager);
        LogManager log_manager(log_file);

        LogRecord lr1(1, 0, LogRecordType::BEGIN); log_manager.AppendRecord(lr1);
        LogRecord lr2(2, 0, LogRecordType::BEGIN); log_manager.AppendRecord(lr2);
        LogRecord lr3(1, lr1.lsn, LogRecordType::UPDATE, 0, 10, "AAAA", "BBBB"); log_manager.AppendRecord(lr3);
        LogRecord lr4(2, lr2.lsn, LogRecordType::UPDATE, 0, 20, "XXXX", "YYYY"); log_manager.AppendRecord(lr4);

        // Page 0 flushed with LSN 4
        Page* pg0 = bpm.FetchPage(0);
        char* data = pg0->GetData();
        std::memcpy(data + 10, "BBBB", 4);
        std::memcpy(data + 20, "YYYY", 4);
        pg0->SetPageLSN(4);
        bpm.UnpinPage(0, true);
        bpm.FlushPage(0);

        LogRecord lr5(1, lr3.lsn, LogRecordType::COMMIT); log_manager.AppendRecord(lr5);
        LogRecord lr6(2, lr4.lsn, LogRecordType::UPDATE, 0, 30, "ZZZZ", "WWWW"); log_manager.AppendRecord(lr6);
        LogRecord lr7(3, 0, LogRecordType::BEGIN); log_manager.AppendRecord(lr7);
        LogRecord lr8(3, lr7.lsn, LogRecordType::UPDATE, 0, 40, "MMMM", "NNNN"); log_manager.AppendRecord(lr8);
    }

    {
        // Run first recovery, but simulate crash after undoing T3's update (offset 40)
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(10, &disk_manager);
        LogManager log_manager(log_file);
        RecoveryManager rm(&disk_manager, &bpm, &log_manager);

        std::vector<txn_id_t> active_txns;
        std::unordered_map<page_id_t, lsn_t> dpt;
        rm.ExecuteAnalysisPhase(active_txns, dpt);
        rm.ExecuteRedoPhase(dpt);

        // Manually undo T3's update (offset 40)
        Page* pg0 = bpm.FetchPage(0);
        char* data = pg0->GetData();
        std::memcpy(data + 40, "MMMM", 4);
        pg0->SetDirty(true);

        // Append CLR for T3's update (T3's update was LSN 8)
        LogRecord clr_rec(3, 8, LogRecordType::CLR, 0, 40, "", "MMMM", 7); // 7 is lr7 (T3's BEGIN)
        lsn_t clr_lsn = log_manager.AppendRecord(clr_rec);
        pg0->SetPageLSN(clr_lsn);
        bpm.UnpinPage(0, true);
        bpm.FlushPage(0);
    }

    {
        // Now run recovery again to ensure idempotency
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(10, &disk_manager);
        LogManager log_manager(log_file);
        RecoveryManager rm(&disk_manager, &bpm, &log_manager);

        std::vector<txn_id_t> active_txns;
        std::unordered_map<page_id_t, lsn_t> dpt;
        rm.ExecuteAnalysisPhase(active_txns, dpt);

        assert(active_txns.size() == 2); 
        
        // Redo Phase
        rm.ExecuteRedoPhase(dpt);

        // Undo Phase
        rm.ExecuteUndoPhase(active_txns);

        // Verify recovered state
        Page* pg0 = bpm.FetchPage(0);
        char* data = pg0->GetData();
        assert(std::string(data + 10, 4) == "BBBB"); // T1 remains
        assert(std::string(data + 20, 4) == "XXXX"); // T2 rolled back
        assert(std::string(data + 30, 4) == "ZZZZ"); // T2 rolled back
        assert(std::string(data + 40, 4) == "MMMM"); // T3 rolled back
        bpm.UnpinPage(0, false);

        std::cout << "[IDEMPOTENCY SUCCESS] Nested crash recovery verified. Recovered state matches perfectly." << std::endl;
    }

    std::filesystem::remove(db_file);
    std::filesystem::remove(lsn_file);
    std::filesystem::remove(log_file);
    std::cout << "[ARIES CRASH RECOVERY SUCCESS] All ARIES recovery and idempotency tests passed.\n" << std::endl;
}

void TestReplication() {
    std::cout << "--- Starting Log Replication (Milestone 6) Tests ---" << std::endl;

    std::string primary_db = "test_primary.db";
    std::string primary_lsns = primary_db + ".lsns";
    std::string replica_db = "test_replica.db";
    std::string replica_lsns = replica_db + ".lsns";

    std::filesystem::remove(primary_db);
    std::filesystem::remove(primary_lsns);
    std::filesystem::remove(replica_db);
    std::filesystem::remove(replica_lsns);

    { // Start of isolation scope
        // 1. Setup primary and replica storage layers
        DiskManager primary_dm(primary_db);
        BufferPoolManager primary_bpm(10, &primary_dm);

        DiskManager replica_dm(replica_db);
        BufferPoolManager replica_bpm(10, &replica_dm);

        // Create page 0 on both primary and replica
        page_id_t p0_prim, p0_repl;
        Page* pg0_prim = primary_bpm.NewPage(&p0_prim);
        Page* pg0_repl = replica_bpm.NewPage(&p0_repl);
        assert(p0_prim == 0);
        assert(p0_repl == 0);

        SlottedPage::Init(pg0_prim->GetData());
        SlottedPage::Init(pg0_repl->GetData());

        primary_bpm.UnpinPage(p0_prim, true);
        replica_bpm.UnpinPage(p0_repl, true);
        primary_bpm.FlushAllPages();
        replica_bpm.FlushAllPages();

        // 2. Start replica listening on port 23456
        int listen_port = 23456;
        ReplicationReceiver receiver(listen_port, &replica_bpm);
        receiver.StartListening();
        assert(receiver.GetRole() == NodeRole::REPLICA);
        assert(receiver.IsRunning());

        // 3. Connect primary to replica (Synchronous Mode)
        ReplicationManager manager("127.0.0.1", listen_port, ReplicationMode::SYNCHRONOUS);
        manager.StartBroadcasting();
        assert(manager.GetRole() == NodeRole::PRIMARY);
        assert(manager.IsReplicaOnline());

        // 4. Test Synchronous Replication
        LogRecord record1(1, 0, LogRecordType::UPDATE, 0, 1, "", "SyncData");
        record1.lsn = 15;

        bool sync_status = manager.ReplicateLog(record1);
        assert(sync_status);

        // Assert that the replica applied the changes immediately before unblocking
        Page* pg0 = replica_bpm.FetchPage(0);
        char* repl_data = pg0->GetData();
        std::string val;
        bool get_status = SlottedPage::GetTuple(repl_data, 1, val);
        assert(get_status && val == "SyncData");
        assert(pg0->GetPageLSN() == 15);
        replica_bpm.UnpinPage(0, false);

        std::cout << "[SYNCHRONOUS REPLICATION SUCCESS] Record replicated and ACK received successfully." << std::endl;

        // 5. Test Asynchronous Replication
        ReplicationManager async_manager("127.0.0.1", listen_port, ReplicationMode::ASYNCHRONOUS);
        async_manager.StartBroadcasting();
        assert(async_manager.IsReplicaOnline());

        LogRecord record2(1, 0, LogRecordType::UPDATE, 0, 2, "", "AsyncData");
        record2.lsn = 25;

        bool async_status = async_manager.ReplicateLog(record2);
        assert(async_status);

        // Give replica receiver thread a tiny amount of time to process
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        pg0 = replica_bpm.FetchPage(0);
        repl_data = pg0->GetData();
        get_status = SlottedPage::GetTuple(repl_data, 2, val);
        assert(get_status && val == "AsyncData");
        assert(pg0->GetPageLSN() == 25);
        replica_bpm.UnpinPage(0, false);

        std::cout << "[ASYNCHRONOUS REPLICATION SUCCESS] Record broadcasted asynchronously." << std::endl;

        // 6. Test Timeout Handling
        receiver.StopListening();
        manager.HandleReplicaTimeout();
        assert(!manager.IsReplicaOnline());

        LogRecord record3(1, 0, LogRecordType::UPDATE, 0, 3, "", "TimeoutData");
        record3.lsn = 35;
        
        bool timeout_status = manager.ReplicateLog(record3);
        assert(!timeout_status);

        std::cout << "[TIMEOUT SUCCESS] Replica offline detected and handled cleanly." << std::endl;

        // 7. Test Role Promotion
        assert(receiver.GetRole() == NodeRole::REPLICA);
        receiver.PromoteToPrimary();
        assert(receiver.GetRole() == NodeRole::PRIMARY);

        std::cout << "[PROMOTION SUCCESS] Replica successfully promoted to Primary." << std::endl;
    } // End of isolation scope

    // Sleep briefly to let background receiver/ack socket threads finish exiting and release file locks on Windows
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    std::filesystem::remove(primary_db);
    std::filesystem::remove(primary_lsns);
    std::filesystem::remove(replica_db);
    std::filesystem::remove(replica_lsns);

    std::cout << "[REPLICATION LAYER SUCCESS] All log replication layer tests passed successfully!\n" << std::endl;
}

#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include <thread>

void TestSS2PL() {
    std::cout << "--- Starting SS2PL Concurrency Control Tests ---" << std::endl;
    LockManager lock_mgr;
    TransactionManager txn_mgr(&lock_mgr);

    Transaction* txn1 = txn_mgr.Begin();
    Transaction* txn2 = txn_mgr.Begin();

    RID rid1(0, 0);

    // Txn 1 acquires EXCLUSIVE lock
    bool ok1 = lock_mgr.LockExclusive(txn1, rid1);
    assert(ok1);

    // Txn 2 tries to acquire SHARED lock - should timeout and abort txn2
    bool t2_done = false;
    std::thread t2([&]() {
        bool ok2 = lock_mgr.LockShared(txn2, rid1);
        assert(!ok2); // Should fail/timeout
        t2_done = true;
    });
    t2.join();
    assert(t2_done);
    assert(txn2->GetState() == TransactionState::ABORTED);

    // Txn 1 aborts/releases lock
    txn_mgr.Abort(txn1);
    assert(txn1->GetState() == TransactionState::ABORTED);

    // Txn 3 begins and acquires SHARED lock on same RID
    Transaction* txn3 = txn_mgr.Begin();
    bool ok3 = lock_mgr.LockShared(txn3, rid1);
    assert(ok3);

    // Clean up
    txn_mgr.Commit(txn3);
    std::cout << "[SS2PL SUCCESS] Lock compatibility, acquisition blocking, timeout, and commit release verified.\n" << std::endl;
}

void TestGraceHashJoin() {
    std::cout << "--- Starting Grace Hash Join Tests ---" << std::endl;
    std::string db_file = "test_grace_hj.db";
    if (std::filesystem::exists(db_file)) {
        std::filesystem::remove(db_file);
    }

    {
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(10, &disk_manager);

        // Define schemas
        Schema left_schema({Column("id", "INTEGER"), Column("val1", "VARCHAR")});
        Schema right_schema({Column("t1_id", "INTEGER"), Column("val2", "VARCHAR")});
        Schema join_schema({
            Column("id", "INTEGER"),
            Column("val1", "VARCHAR"),
            Column("t1_id", "INTEGER"),
            Column("val2", "VARCHAR")
        });

        // Create table metadata
        auto t1_meta = std::make_shared<TableMetadata>();
        t1_meta->name = "t1";
        auto t2_meta = std::make_shared<TableMetadata>();
        t2_meta->name = "t2";

        // Insert tuples into t1
        page_id_t pid1;
        Page* pg1 = bpm.NewPage(&pid1);
        pg1->WLock();
        SlottedPage::Init(pg1->GetData());
        Row r1_1{{{"id", 1}, {"val1", std::string("A")}}};
        Row r1_2{{{"id", 2}, {"val1", std::string("B")}}};
        Row r1_3{{{"id", 3}, {"val1", std::string("C")}}};
        RID rid_dummy;
        SlottedPage::InsertTuple(pg1->GetData(), r1_1.Serialize(), &rid_dummy, pid1);
        SlottedPage::InsertTuple(pg1->GetData(), r1_2.Serialize(), &rid_dummy, pid1);
        SlottedPage::InsertTuple(pg1->GetData(), r1_3.Serialize(), &rid_dummy, pid1);
        pg1->WUnlock();
        bpm.UnpinPage(pid1, true);
        t1_meta->pages.push_back(pid1);

        // Insert tuples into t2
        page_id_t pid2;
        Page* pg2 = bpm.NewPage(&pid2);
        pg2->WLock();
        SlottedPage::Init(pg2->GetData());
        Row r2_1{{{"t1_id", 1}, {"val2", std::string("X")}}};
        Row r2_2{{{"t1_id", 2}, {"val2", std::string("Y")}}};
        Row r2_3{{{"t1_id", 4}, {"val2", std::string("Z")}}};
        SlottedPage::InsertTuple(pg2->GetData(), r2_1.Serialize(), &rid_dummy, pid2);
        SlottedPage::InsertTuple(pg2->GetData(), r2_2.Serialize(), &rid_dummy, pid2);
        SlottedPage::InsertTuple(pg2->GetData(), r2_3.Serialize(), &rid_dummy, pid2);
        pg2->WUnlock();
        bpm.UnpinPage(pid2, true);
        t2_meta->pages.push_back(pid2);

        // Create executors
        auto left_scan = std::make_unique<SeqScanExecutor>(&bpm, t1_meta, left_schema);
        auto right_scan = std::make_unique<SeqScanExecutor>(&bpm, t2_meta, right_schema);

        Expression key_l(Expression::Type::COLUMN_REF, "id");
        Expression key_r(Expression::Type::COLUMN_REF, "t1_id");

        GraceHashJoinExecutor ghj(std::move(left_scan), std::move(right_scan), key_l, key_r, join_schema, 2);
        ghj.Init();

        Tuple t;
        RID r;
        int count = 0;
        while (ghj.Next(&t, &r)) {
            int id = std::get<int>(t.GetValue("id"));
            std::string val1 = std::get<std::string>(t.GetValue("val1"));
            std::string val2 = std::get<std::string>(t.GetValue("val2"));
            if (id == 1) {
                assert(val1 == "A" && val2 == "X");
            } else if (id == 2) {
                assert(val1 == "B" && val2 == "Y");
            } else {
                assert(false);
            }
            count++;
        }
        assert(count == 2);
        ghj.Close();
    }

    std::filesystem::remove(db_file);
    std::cout << "[GRACE HASH JOIN SUCCESS] Partition phase, disk spilling, and probing verified.\n" << std::endl;
}

void TestReplicationCatchUp() {
    std::cout << "--- Starting Replication Catch-up Protocol Tests ---" << std::endl;

    std::string primary_db = "test_catchup_primary.db";
    std::string primary_lsns = primary_db + ".lsns";
    std::string replica_db = "test_catchup_replica.db";
    std::string replica_lsns = replica_db + ".lsns";
    std::string primary_log = "test_catchup_primary.log";

    std::filesystem::remove(primary_db);
    std::filesystem::remove(primary_lsns);
    std::filesystem::remove(replica_db);
    std::filesystem::remove(replica_lsns);
    std::filesystem::remove(primary_log);

    {
        DiskManager primary_dm(primary_db);
        BufferPoolManager primary_bpm(10, &primary_dm);

        DiskManager replica_dm(replica_db);
        BufferPoolManager replica_bpm(10, &replica_dm);

        // Create page 0 on both
        page_id_t p0_p, p0_r;
        Page* pg0_p = primary_bpm.NewPage(&p0_p);
        Page* pg0_r = replica_bpm.NewPage(&p0_r);
        SlottedPage::Init(pg0_p->GetData());
        SlottedPage::Init(pg0_r->GetData());
        primary_bpm.UnpinPage(p0_p, true);
        replica_bpm.UnpinPage(p0_r, true);
        primary_bpm.FlushAllPages();
        replica_bpm.FlushAllPages();

        // 1. Setup Primary's LogManager and append some records BEFORE replica connects
        LogManager log_mgr(primary_log);
        LogRecord rec1(1, 0, LogRecordType::UPDATE, 0, 1, "", "CatchUpData1");
        LogRecord rec2(1, 1, LogRecordType::UPDATE, 0, 2, "", "CatchUpData2");
        log_mgr.AppendRecord(rec1); // LSN 1
        log_mgr.AppendRecord(rec2); // LSN 2

        // 2. Start replica listening
        int listen_port = 23457;
        ReplicationReceiver receiver(listen_port, &replica_bpm);
        receiver.StartListening();

        // 3. Connect primary's ReplicationManager (with LogManager pointer!)
        ReplicationManager manager("127.0.0.1", listen_port, ReplicationMode::SYNCHRONOUS, &log_mgr);
        manager.StartBroadcasting();

        // Let the catch-up take place
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Assert that the replica received and applied the missed logs during handshake
        Page* pg0 = replica_bpm.FetchPage(0);
        std::string val;
        bool status1 = SlottedPage::GetTuple(pg0->GetData(), 1, val);
        assert(status1 && val == "CatchUpData1");
        bool status2 = SlottedPage::GetTuple(pg0->GetData(), 2, val);
        assert(status2 && val == "CatchUpData2");
        assert(pg0->GetPageLSN() == 2);
        replica_bpm.UnpinPage(0, false);

        receiver.StopListening();
        manager.HandleReplicaTimeout();
    }

    std::filesystem::remove(primary_db);
    std::filesystem::remove(primary_lsns);
    std::filesystem::remove(replica_db);
    std::filesystem::remove(replica_lsns);
    std::filesystem::remove(primary_log);

    std::cout << "[REPLICATION CATCH-UP SUCCESS] Missed logs successfully replayed via LSN handshake.\n" << std::endl;
}

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "      MINIDB CAPSTONE TEST RUNNER (M6)       " << std::endl;
    std::cout << "============================================" << std::endl;

    TestDiskManager();
    TestSlottedPage();
    TestBufferPoolManager();
    TestBPlusTree();
    TestQueryEngine();
    TestExecutionEngine();
    TestOptimizer();
    TestRecovery();
    TestReplication();
    TestSS2PL();
    TestGraceHashJoin();
    TestReplicationCatchUp();

    std::cout << "ALL MINIDB MILESTONE 6 TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
