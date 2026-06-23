#include <cstdio>
#include <iostream>
#include <string>

#include "common/exception.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"

using namespace minidb;

// M1 demo: exercises the storage stack end to end and reports buffer-pool
// behaviour. A deliberately small pool forces evictions so the clock-sweep
// policy and dirty write-back are actually used.
static int storage_selftest(const std::string& path) {
    std::remove(path.c_str());  // start from a clean file

    DiskManager disk(path);
    BufferPool  pool(/*frames=*/8, &disk);  // small on purpose, so the data spills past the pool

    PageId first = HeapFile::create(&pool);
    HeapFile heap(&pool, first);

    const int N = 2000;  // spans more pages than the pool has frames -> forces eviction
    for (int i = 0; i < N; ++i)
        heap.insert("row-" + std::to_string(i) + "-payload-data");
    pool.flush_all();

    int count = 0;
    RID rid;
    std::string val;
    for (auto it = heap.begin(); it.next(rid, val); ) ++count;

    std::cout << "inserted=" << N << " scanned=" << count << "\n"
              << "db pages=" << disk.num_pages() << "\n"
              << "buffer pool: hits=" << pool.hits()
              << " misses=" << pool.misses()
              << " evictions=" << pool.evictions() << "\n";
    return count == N ? 0 : 1;
}

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    try {
        if (cmd == "selftest") {
            std::string path = argc > 2 ? argv[2] : "minidb_selftest.db";
            return storage_selftest(path);
        }
        std::cout << "MiniDB (M1: storage engine)\n"
                  << "usage:\n"
                  << "  minidb selftest [dbfile]   run the storage-layer self test\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
