#include "buffer/buffer_pool.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace minidb;
using namespace std;

// Forces eviction by exceeding the pool size, then verifies the
// pinned page still has its dirty content flushed and re-readable.
int main() {
 remove("/tmp/minidb_test_bp_evict.db");
 DiskManager dm("/tmp/minidb_test_bp_evict.db");
 BufferPool bp(&dm);

 constexpr int32_t kPool = 16;
 constexpr int32_t kExtra = 8;
 // Pin kPool pages.
 int32_t pinned[kPool];
 for (int32_t i = 0; i < kPool; ++i) {
 pinned[i] = dm.AllocatePage();
 Page* p = bp.FetchPage(pinned[i]);
 assert(p != nullptr);
 memset(p->GetData(), 0, PAGE_SIZE);
 p->GetData()[0] = static_cast<char>('A' + i);
 assert(bp.UnpinPage(pinned[i], true));
 }

 // Touch kExtra more pages to force eviction of the originals.
 for (int32_t i = 0; i < kExtra; ++i) {
 int32_t pid = dm.AllocatePage();
 Page* p = bp.FetchPage(pid);
 assert(p != nullptr);
 memset(p->GetData(), 0, PAGE_SIZE);
 assert(bp.UnpinPage(pid, false));
 }

 bp.FlushAll();

 // Re-fetch the original pages; their data must survive the round-trip.
 for (int32_t i = 0; i < kPool; ++i) {
 Page* p = bp.FetchPage(pinned[i]);
 assert(p != nullptr);
 assert(p->GetData()[0] == static_cast<char>('A' + i));
 bp.UnpinPage(pinned[i], false);
 }

 bp.FlushAll();
 remove("/tmp/minidb_test_bp_evict.db");
 cout << "ALL BUFFER POOL EVICTION TESTS PASSED" << endl;
 return 0;
}
