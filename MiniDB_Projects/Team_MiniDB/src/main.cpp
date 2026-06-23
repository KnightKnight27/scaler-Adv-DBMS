// main.cpp — Member 1 demo: storage layer (Page, BufferPool, HeapFile)
// After Member 2 and Member 3 add their code, this becomes the full SQL REPL.

#include <iostream>

#include "common/Config.hpp"
#include "common/Types.hpp"
#include "storage/BufferPool.hpp"
#include "storage/HeapFile.hpp"
#include "storage/PageManager.hpp"

using namespace std;
using namespace minidb;

int main() {
    cout << "===========================================\n";
    cout << "  MiniDB — Storage Layer Demo (Member 1)\n";
    cout << "===========================================\n";

    BufferPool pool;
    PageManager pm(pool);
    pm.open(string(DATA_DIR) + "/demo.db");

    uint32_t page_id = pm.allocatePage(PageType::HEAP);
    HeapFile heap(pm, page_id);

    Row r1;
    r1["id"] = "1";
    r1["name"] = "Alice";
    heap.insertRow(r1);

    Row r2;
    r2["id"] = "2";
    r2["name"] = "Bob";
    heap.insertRow(r2);

    cout << "Inserted 2 rows into heap file.\n";
    cout << "Scanning all rows:\n";

    RowList rows = heap.scanAll();
    for (const Row& row : rows) {
        cout << "  | ";
        for (const auto& kv : row) cout << kv.first << "=" << kv.second << " ";
        cout << "|\n";
    }

    pm.flushAll();
    pm.close();
    cout << "Done. Pages flushed to disk.\n";
    return 0;
}
