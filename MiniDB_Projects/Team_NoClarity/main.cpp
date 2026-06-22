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

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "      MINIDB CAPSTONE TEST RUNNER (M2)       " << std::endl;
    std::cout << "============================================" << std::endl;

    TestDiskManager();
    TestSlottedPage();
    TestBufferPoolManager();
    TestBPlusTree();
    TestQueryEngine();

    std::cout << "ALL MINIDB MILESTONE 2 TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
