// ============================================================
// DoraDB — Milestone 1 Test Driver
//
// Tests the storage engine foundation:
//   1. DiskManager: page allocation, read/write
//   2. Page: slotted page row insert/get/delete
//   3. BufferPool: fetch/unpin/eviction
//   4. HeapFile: insert/scan/delete rows with schema
//   5. Row serialization: Value→bytes→Value round-trip
// ============================================================

#include "common/config.h"
#include "common/types.h"
#include "storage/disk_manager.h"
#include "storage/page.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

// Helper to print pass/fail
static int tests_passed = 0;
static int tests_total = 0;

void CHECK(bool condition, const std::string& msg) {
    tests_total++;
    if (condition) {
        std::cout << "  [PASS] " << msg << "\n";
        tests_passed++;
    } else {
        std::cout << "  [FAIL] " << msg << "\n";
    }
}

// ============================================================
// Test 1: DiskManager
// ============================================================
void TestDiskManager() {
    std::cout << "\n=== Test: DiskManager ===\n";

    // Clean up any previous test file
    std::filesystem::remove_all("data");

    DiskManager dm("data/test_disk.db");
    CHECK(dm.GetNumPages() == 0, "starts with 0 pages");

    // Allocate and write a page
    int pid = dm.AllocatePage();
    CHECK(pid == 0, "first page is page 0");
    CHECK(dm.GetNumPages() == 1, "now has 1 page");

    char write_buf[PAGE_SIZE];
    memset(write_buf, 0, PAGE_SIZE);
    const char* msg = "Hello from DoraDB!";
    memcpy(write_buf, msg, strlen(msg));
    dm.WritePage(pid, write_buf);

    // Read it back
    char read_buf[PAGE_SIZE];
    dm.ReadPage(pid, read_buf);
    CHECK(memcmp(write_buf, read_buf, PAGE_SIZE) == 0, "read matches write");

    // Allocate more pages
    int p1 = dm.AllocatePage();
    int p2 = dm.AllocatePage();
    CHECK(p1 == 1 && p2 == 2, "sequential allocation");
    CHECK(dm.GetNumPages() == 3, "now has 3 pages");
}

// ============================================================
// Test 2: Slotted Page
// ============================================================
void TestPage() {
    std::cout << "\n=== Test: Slotted Page ===\n";

    Page page;
    page.Init(42);
    CHECK(page.GetPageId() == 42, "page ID set correctly");
    CHECK(page.GetNumSlots() == 0, "starts empty");
    CHECK(page.GetNextPageId() == INVALID_PAGE_ID, "no next page");

    // Insert a row
    const char* row1 = "first row data";
    int slot1 = page.InsertRow(row1, strlen(row1));
    CHECK(slot1 == 0, "first slot is 0");
    CHECK(page.GetNumSlots() == 1, "1 slot after insert");

    // Read it back
    char buf[PAGE_SIZE];
    int size;
    bool found = page.GetRow(0, buf, &size);
    CHECK(found, "row 0 found");
    CHECK(size == (int)strlen(row1), "correct size");
    CHECK(memcmp(buf, row1, size) == 0, "data matches");

    // Insert more rows
    const char* row2 = "second row here";
    int slot2 = page.InsertRow(row2, strlen(row2));
    CHECK(slot2 == 1, "second slot is 1");

    // Delete first row
    bool deleted = page.DeleteRow(0);
    CHECK(deleted, "delete returns true");
    found = page.GetRow(0, buf, &size);
    CHECK(!found, "deleted row not found");

    // Second row still accessible
    found = page.GetRow(1, buf, &size);
    CHECK(found, "row 1 still accessible after deleting row 0");
    CHECK(memcmp(buf, row2, strlen(row2)) == 0, "row 1 data intact");
}

// ============================================================
// Test 3: Row Serialization
// ============================================================
void TestSerialization() {
    std::cout << "\n=== Test: Row Serialization ===\n";

    Schema schema;
    schema.columns.push_back({"id", DataType::INT, 0});
    schema.columns.push_back({"name", DataType::VARCHAR, 50});
    schema.columns.push_back({"active", DataType::BOOL, 0});
    schema.pk_index = 0;

    // Create a row: (1, 'Alice', true)
    Row row = {Value::Int(1), Value::Varchar("Alice"), Value::Bool(true)};

    // Serialize
    char buf[PAGE_SIZE];
    int bytes = SerializeRow(row, schema, buf);
    CHECK(bytes > 0, "serialized to " + std::to_string(bytes) + " bytes");

    int expected = GetSerializedRowSize(row, schema);
    CHECK(bytes == expected, "size matches prediction");

    // Deserialize
    Row recovered = DeserializeRow(buf, bytes, schema);
    CHECK(recovered.size() == 3, "3 columns recovered");
    CHECK(recovered[0].int_val == 1, "id = 1");
    CHECK(recovered[1].str_val == "Alice", "name = Alice");
    CHECK(recovered[2].bool_val == true, "active = true");

    // Test with NULL
    Row row2 = {Value::Int(2), Value::Null(DataType::VARCHAR), Value::Bool(false)};
    bytes = SerializeRow(row2, schema, buf);
    Row recovered2 = DeserializeRow(buf, bytes, schema);
    CHECK(recovered2[1].is_null, "NULL varchar recovered");
    CHECK(recovered2[0].int_val == 2, "id = 2 around NULL");
}

// ============================================================
// Test 4: BufferPool
// ============================================================
void TestBufferPool() {
    std::cout << "\n=== Test: BufferPool ===\n";

    std::filesystem::remove_all("data");
    DiskManager* dm = new DiskManager("data/test_bp.db");

    // Small pool (4 frames) to test eviction easily
    BufferPool pool(dm, 4);

    // Allocate and write pages
    int p0, p1, p2, p3;
    Page* pg0 = pool.NewPage(p0);
    pg0->Init(p0);
    const char* msg = "page zero";
    pg0->InsertRow(msg, strlen(msg));
    pool.UnpinPage(p0, true);

    pool.NewPage(p1);
    pool.UnpinPage(p1, true);
    pool.NewPage(p2);
    pool.UnpinPage(p2, true);
    pool.NewPage(p3);
    pool.UnpinPage(p3, true);

    CHECK(true, "4 pages allocated without crash");

    // All 4 frames full. Allocating another should evict one.
    int p4;
    pool.NewPage(p4);
    pool.UnpinPage(p4, true);
    CHECK(true, "5th page triggers eviction OK");

    // Fetch page 0 back (was possibly evicted)
    Page* fetched = pool.FetchPage(p0);
    char buf[PAGE_SIZE];
    int size;
    bool found = fetched->GetRow(0, buf, &size);
    CHECK(found, "page 0 row survived eviction + re-fetch");
    CHECK(memcmp(buf, msg, strlen(msg)) == 0, "data intact after eviction");
    pool.UnpinPage(p0, false);

    pool.FlushAll();
    delete dm;
}

// ============================================================
// Test 5: HeapFile with Schema
// ============================================================
void TestHeapFile() {
    std::cout << "\n=== Test: HeapFile ===\n";

    std::filesystem::remove_all("data");
    DiskManager* dm = new DiskManager("data/test_heap.db");
    BufferPool pool(dm, 16);

    HeapFile heap(&pool);
    int first = heap.Create();
    CHECK(first >= 0, "heap file created with first page " + std::to_string(first));

    // Set up schema: students(id INT, name VARCHAR(50), active BOOL)
    Schema schema;
    schema.columns.push_back({"id", DataType::INT, 0});
    schema.columns.push_back({"name", DataType::VARCHAR, 50});
    schema.columns.push_back({"active", DataType::BOOL, 0});
    schema.pk_index = 0;

    // Insert some rows
    std::vector<RID> rids;
    std::vector<std::string> names = {"Alice", "Bob", "Charlie", "Diana", "Eve"};
    for (int i = 0; i < 5; i++) {
        Row row = {Value::Int(i + 1), Value::Varchar(names[i]), Value::Bool(i % 2 == 0)};
        char buf[PAGE_SIZE];
        int size = SerializeRow(row, schema, buf);
        RID rid = heap.InsertRow(buf, size);
        rids.push_back(rid);
    }
    CHECK(rids.size() == 5, "5 rows inserted");

    // Read back by RID
    char buf[PAGE_SIZE];
    int size;
    bool found = heap.GetRow(rids[0], buf, &size);
    CHECK(found, "row 0 found by RID");
    Row r = DeserializeRow(buf, size, schema);
    CHECK(r[0].int_val == 1, "id = 1");
    CHECK(r[1].str_val == "Alice", "name = Alice");

    // Delete row 2 (Charlie)
    heap.DeleteRow(rids[2]);

    // Scan all rows — should get 4 (Alice, Bob, Diana, Eve)
    int count = 0;
    heap.Scan([&](const RID& /*rid*/, const char* data, int sz) {
        Row row = DeserializeRow(data, sz, schema);
        count++;
    });
    CHECK(count == 4, "scan returns 4 rows after delete");

    // Bulk insert to test multi-page
    for (int i = 10; i < 200; i++) {
        Row row = {Value::Int(i), Value::Varchar("Student_" + std::to_string(i)),
                   Value::Bool(true)};
        char buf2[PAGE_SIZE];
        int sz = SerializeRow(row, schema, buf2);
        heap.InsertRow(buf2, sz);
    }

    count = 0;
    heap.Scan([&](const RID&, const char*, int) { count++; });
    CHECK(count == 194, "194 rows after bulk insert (4 + 190)");

    pool.FlushAll();
    delete dm;
}

// ============================================================
// Main
// ============================================================
int main() {
    std::cout << "========================================\n";
    std::cout << "  DoraDB — Milestone 1 Tests\n";
    std::cout << "  Storage Engine + Buffer Pool\n";
    std::cout << "========================================\n";

    TestDiskManager();
    TestSerialization();
    TestPage();
    TestBufferPool();
    TestHeapFile();

    std::cout << "\n========================================\n";
    std::cout << "  Results: " << tests_passed << "/" << tests_total << " passed\n";
    std::cout << "========================================\n";

    // Clean up test data
    std::filesystem::remove_all("data");

    return (tests_passed == tests_total) ? 0 : 1;
}
