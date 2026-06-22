// Tests for the storage engine: row encoding, slotted pages, buffer pool, heap.
#include <filesystem>

#include "check.h"
#include "storage.h"

using namespace minidb;

int main() {
    // round-trip encoding
    Row a{Value::Int(1), Value::Text("Alice"), Value::Int(95)};
    CHECK(decodeRow(encodeRow(a)) == a);
    Row b{Value::Int(-7), Value::Text("")};
    CHECK(decodeRow(encodeRow(b)) == b);

    // slotted page
    Page p = Page::empty();
    int s0 = p.insert(encodeRow({Value::Int(1), Value::Text("a")}));
    int s1 = p.insert(encodeRow({Value::Int(2), Value::Text("b")}));
    CHECK(s0 == 0 && s1 == 1);
    std::string out;
    CHECK(p.get(s0, out) && decodeRow(out) == (Row{Value::Int(1), Value::Text("a")}));
    p.erase(s0);
    CHECK(!p.get(s0, out));                  // tombstoned
    CHECK(p.get(s1, out));                    // neighbour intact

    // buffer pool eviction + write-back across a multi-page heap
    std::filesystem::remove("t_storage_test.tbl");
    {
        DiskManager d("t_storage_test.tbl");
        BufferPool bp(&d, 2);
        HeapFile h(&d, &bp);
        for (int i = 0; i < 50; ++i)
            h.insert(encodeRow({Value::Int(i), Value::Text(std::string(300, 'v'))}));
        bp.flushAll();
        CHECK(d.numPages() > 1);
    }
    {
        DiskManager d("t_storage_test.tbl");
        BufferPool bp(&d, 2);
        int count = 0;
        long sum = 0;
        for (int pg = 0; pg < d.numPages(); ++pg) {
            Page* page = bp.fetch(pg);
            for (int s = 0; s < page->numSlots(); ++s) {
                std::string rec;
                if (page->get(s, rec)) { sum += decodeRow(rec)[0].i; count++; }
            }
        }
        CHECK(count == 50);
        CHECK(sum == 1225);                   // 0+1+...+49
        CHECK(bp.stats.evictions > 0);        // scanning more pages than the pool holds
    }
    std::filesystem::remove("t_storage_test.tbl");

    REPORT();
}
