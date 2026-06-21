#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "storage/buffer_pool.hpp"
#include "storage/disk_manager.hpp"
#include "storage/heap_file.hpp"

// Phase 1 driver: proves the storage spine persists tuples to disk.
// Later phases replace this with a SQL REPL.

static const char* kDbFile = "minidb_data.db";

static void write_phase() {
    DiskManager disk(kDbFile);
    BufferPool pool(disk);
    HeapFile heap(pool, disk);

    const std::vector<std::string> rows = {
        "1|Kartik|20", "2|Krishank|30", "3|Sandip|15", "4|Nitish|17", "5|Kp|20",
    };
    for (const std::string& r : rows) {
        RowID rid = heap.insert(r);
        std::cout << "  insert '" << r << "' -> (page=" << rid.page_id
                  << ", slot=" << rid.slot << ")\n";
    }
    pool.flush_all();  // push dirty pages to disk so they outlive this process
    std::cout << "  flushed " << disk.num_pages() << " page(s) to " << kDbFile << "\n";
}

static void scan_phase() {
    DiskManager disk(kDbFile);
    BufferPool pool(disk);
    HeapFile heap(pool, disk);

    std::cout << "  reading " << disk.num_pages() << " page(s) back from disk:\n";
    for (const auto& [rid, tuple] : heap.scan()) {
        std::cout << "    (page=" << rid.page_id << ", slot=" << rid.slot
                  << ") -> " << tuple << "\n";
    }
}

int main(int argc, char** argv) {
    std::string cmd = (argc > 1) ? argv[1] : "demo";

    if (cmd == "init") {
        write_phase();
    } else if (cmd == "scan") {
        scan_phase();
    } else {
        // Self-contained demo: start from a clean file, write, then read it
        // back through brand-new objects so the data can only have come from
        // disk — not from anything cached in memory.
        std::remove(kDbFile);
        std::cout << "== WRITE phase ==\n";
        write_phase();
        std::cout << "\n== READ phase (fresh objects, reopened file) ==\n";
        scan_phase();
        std::cout << "\nTip: run './minidb init' then './minidb scan' to prove it across two processes.\n";
    }
    return 0;
}
