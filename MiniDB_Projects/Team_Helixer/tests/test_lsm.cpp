// Verifies the LSM tree: put/get across MemTable + multiple SSTables, overwrite
// precedence (newest wins), tombstone deletes, persistence of SSTables, and
// compaction (merges runs, drops tombstones, reclaims space).
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include "lsm/lsm_tree.h"

using namespace minidb;

int main() {
    const std::string dir = "test_lsm_dir";
    {
        // Small MemTable limit so several flushes (=> several SSTables) happen.
        LSMTree lsm(dir, /*memtable_limit=*/1000);
        const int N = 5000;
        for (int i = 0; i < N; ++i) lsm.put(i, "v" + std::to_string(i));
        assert(lsm.num_sstables() >= 4); // multiple flushed runs

        // Reads span MemTable + all SSTables.
        std::string v;
        assert(lsm.get(0, &v) && v == "v0");
        assert(lsm.get(4999, &v) && v == "v4999");

        // Overwrite: newest value must win over older SSTable copies.
        lsm.put(0, "updated0");
        assert(lsm.get(0, &v) && v == "updated0");

        // Tombstone delete hides the key even though it exists in old SSTables.
        lsm.remove(1234);
        assert(!lsm.get(1234, &v));

        // Compaction merges everything into one run and drops the tombstone.
        size_t before = lsm.total_disk_bytes();
        lsm.compact();
        assert(lsm.num_sstables() == 1);
        size_t after = lsm.total_disk_bytes();
        assert(after <= before); // overwrites + tombstone reclaimed

        // Data integrity preserved through compaction.
        assert(lsm.get(0, &v) && v == "updated0");
        assert(lsm.get(2500, &v) && v == "v2500");
        assert(!lsm.get(1234, &v)); // stays deleted
    }

    // Persistence: reopen an SSTable file directly and read it back.
    {
        // The compacted run is the only sst_*.dat file; load whichever id remains.
        // (We scan a few candidate ids since compaction advances next_id_.)
        bool loaded = false;
        for (int id = 0; id < 50 && !loaded; ++id) {
            std::string path = dir + "/sst_" + std::to_string(id) + ".dat";
            FILE *f = std::fopen(path.c_str(), "rb");
            if (!f) continue;
            std::fclose(f);
            SSTable t = SSTable::open(path);
            LSMEntry e;
            assert(t.get(2500, &e) && !e.tombstone && e.value == "v2500");
            loaded = true;
        }
        assert(loaded);
    }

    std::cout << "[OK] LSM: MemTable+SSTable get/put, newest-wins, tombstone "
                 "delete, compaction, persistence verified" << std::endl;
    // (Leaves test_lsm_dir on disk; harmless for a test run.)
    return 0;
}
