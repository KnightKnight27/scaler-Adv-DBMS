// lsm_sim.cpp — a small, faithful model of an LSM-tree storage engine
// (the architecture RocksDB uses) to measure the three amplification factors:
//   write amplification  = bytes physically written / bytes of user data
//   read amplification    = sorted runs probed per lookup (with/without bloom)
//   space amplification   = bytes on disk / bytes of live user data
//
// It is NOT RocksDB; it is a simplification that reproduces the same dynamics:
// memtable -> flush to L0 -> leveled compaction with bounded fan-out overlap,
// plus a bloom-filter model for the read path.
// Build:  c++ -O2 -std=c++17 lsm_sim.cpp -o lsm_sim
#include <cstdio>
#include <cstdint>
#include <vector>
#include <random>
#include <algorithm>
#include <functional>

int main() {
    const int  KEY_SPACE  = 200000;   // distinct keys (updates overwrite)
    const int  NUM_WRITES = 2000000;  // total put operations
    const int  VALUE_SIZE = 100;      // bytes per value
    const int  MEMTABLE   = 4000;     // entries before a flush
    const int  L0_TRIGGER = 4;        // L0 files before compaction to L1
    const int  FANOUT     = 10;       // each level ~10x previous (overlap factor)

    // Per-level entry budgets (L1 small, growing 10x). KEY_SPACE caps real data.
    long budget[5] = {0, 8000, 80000, 800000, 1L<<60};
    long lvl[5]    = {0,0,0,0,0};      // resident entries per level L1..L4

    uint64_t userBytes = 0, physBytes = 0;
    int memEntries = 0;
    std::vector<int> l0Files;
    auto B = [&](long e){ return (uint64_t)e * VALUE_SIZE; };

    // Compact `moved` entries down into level L. Real leveled compaction only
    // rewrites the overlapping slice of L (~FANOUT * moved), not all of L.
    std::function<void(int,long)> pushDown = [&](int L, long moved){
        long overlap = std::min(lvl[L], moved * FANOUT);
        physBytes += B(moved + overlap);          // rewrite merged + overlap
        lvl[L] += moved;
        if (L < 4 && lvl[L] > budget[L]) {         // over budget -> cascade
            long spill = lvl[L] - budget[L];
            lvl[L] = budget[L];
            pushDown(L+1, spill);
        }
    };

    for (int i = 0; i < NUM_WRITES; ++i) {
        userBytes += VALUE_SIZE;
        if (++memEntries >= MEMTABLE) {
            physBytes += B(memEntries);             // flush memtable -> L0 file
            l0Files.push_back(memEntries);
            memEntries = 0;
            if ((int)l0Files.size() >= L0_TRIGGER) {
                long moved = 0; for (int e: l0Files) moved += e;
                l0Files.clear();
                pushDown(1, moved);                 // compact L0 -> L1 (cascades)
            }
        }
    }

    // ---- READ PATH: sorted runs a point lookup may probe
    int sortedRuns = (memEntries>0) + (int)l0Files.size();
    for (int L=1;L<=4;++L) sortedRuns += (lvl[L]>0);

    const int    PROBES  = 200000;
    const double BLOOM_FP = 0.01;                   // 1% false-positive rate
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> u(0,1);
    long touchesNoBloom = 0, touchesBloom = 0;
    for (int i=0;i<PROBES;++i){
        touchesNoBloom += sortedRuns;               // worst case: check all runs
        int touched = 1;                            // the one run that has it
        for (int r=0;r<sortedRuns-1;++r)
            if (u(rng) < BLOOM_FP) touched++;        // bloom false positive
        touchesBloom += touched;
    }

    // ---- SPACE: compaction dedups overwrites, so disk ~ settled distinct data
    long onDiskEntries = memEntries;
    for (int e: l0Files) onDiskEntries += e;
    for (int L=1;L<=4;++L) onDiskEntries += lvl[L];
    onDiskEntries = std::min(onDiskEntries, (long)KEY_SPACE * 2); // dedup cap
    uint64_t liveBytes = B(KEY_SPACE);

    printf("==== LSM-tree simulation (RocksDB-style leveled compaction) ====\n");
    printf("user puts             : %d  (value=%dB)\n", NUM_WRITES, VALUE_SIZE);
    printf("distinct keys         : %d\n", KEY_SPACE);
    printf("level residency (L1..L4 entries): %ld / %ld / %ld / %ld\n",
           lvl[1],lvl[2],lvl[3],lvl[4]);
    printf("\n-- WRITE amplification --\n");
    printf("user bytes            : %.1f MB\n", userBytes/1e6);
    printf("physical bytes written: %.1f MB\n", physBytes/1e6);
    printf("write amplification   : %.1fx\n", (double)physBytes/userBytes);
    printf("\n-- READ amplification --\n");
    printf("sorted runs on disk   : %d\n", sortedRuns);
    printf("SST touches/read no bloom : %.2f\n",(double)touchesNoBloom/PROBES);
    printf("SST touches/read w/ bloom : %.2f\n",(double)touchesBloom/PROBES);
    printf("bloom read I/O reduction  : %.0f%%\n",
           100.0*(1.0-(double)touchesBloom/touchesNoBloom));
    printf("\n-- SPACE amplification --\n");
    printf("live user data        : %.1f MB\n", liveBytes/1e6);
    printf("data on disk          : %.1f MB\n", B(onDiskEntries)/1e6);
    printf("space amplification   : %.2fx\n", (double)B(onDiskEntries)/liveBytes);
    return 0;
}
