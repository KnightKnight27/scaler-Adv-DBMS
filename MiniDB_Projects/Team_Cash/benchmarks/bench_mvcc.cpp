// Benchmark / demonstration: MVCC vs 2PL under write contention.
//
// A writer is in the middle of an uncommitted update to every key. We then let
// many readers try to read every key.
//   * 2PL : a reader needs a shared lock, which conflicts with the writer's
//           exclusive lock, so the read blocks.
//   * MVCC: a reader reads against its snapshot and sees the last committed
//           version, so it never blocks.
#include <iostream>

#include "mvcc.h"
#include "txn.h"

using namespace minidb;

int main() {
    const int K = 10;    // keys
    const int R = 100;   // readers
    const int total = K * R;

    // ---- 2PL ----
    LockManager lm;
    int writer = 999;
    for (int k = 1; k <= K; ++k) lm.acquire(writer, k, LockMode::Exclusive);  // uncommitted writer

    int served2pl = 0, blocked2pl = 0;
    for (int r = 0; r < R; ++r) {
        int reader = 1000 + r;
        for (int k = 1; k <= K; ++k) {
            if (lm.acquire(reader, k, LockMode::Shared) == LockManager::Granted) ++served2pl;
            else ++blocked2pl;
            lm.release(reader);  // the blocked reader gives up its wait
        }
    }

    // ---- MVCC ----
    MVCCStore mv;
    for (int k = 1; k <= K; ++k) mv.put(k, "v1", 1);
    mv.commit(1);                                   // committed baseline version
    for (int k = 1; k <= K; ++k) mv.put(k, "v2", 100);  // writer's new versions, NOT committed

    int servedMvcc = 0, blockedMvcc = 0;
    for (int r = 0; r < R; ++r) {
        int snapshot = 1;  // readers started before the writer committed
        for (int k = 1; k <= K; ++k) {
            std::string val;
            if (mv.read(k, snapshot, val)) ++servedMvcc;
            else ++blockedMvcc;
        }
    }

    std::cout << "Write contention on " << K << " keys, " << R << " readers (" << total << " reads):\n";
    std::cout << "  2PL : served " << served2pl << ", blocked " << blocked2pl << "\n";
    std::cout << "  MVCC: served " << servedMvcc << ", blocked " << blockedMvcc << "\n";
    std::cout << "  -> 2PL blocks " << (100 * blocked2pl / total) << "% of reads; MVCC blocks "
              << (100 * blockedMvcc / total) << "%.\n";
    return 0;
}
