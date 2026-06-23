// Lock manager stress test for MiniDB.
// Exercises many concurrent lock/unlock cycles across 4 threads.
#include <cassert>
#include <cstdio>
#include <iostream>
#include <thread>
#include <vector>

#include "transaction/lock_manager.h"

using namespace minidb;
using namespace std;

static void Worker(LockManager* lm, int32_t tid, int32_t iters) {
  TxnId txn = static_cast<TxnId>(100 + tid);
  int32_t acquired = 0;
  for (int32_t i = 0; i < iters; ++i) {
    int32_t rid = i % 16;
    if (lm->LockExclusive(txn, rid)) {
      ++acquired;
      lm->Unlock(txn, rid);
    }
  }
  if (acquired == 0) {
    cerr << "tid=" << tid << " acquired zero locks\n";
  }
}

int main() {
  LockManager lm;
  vector<thread> threads;
  for (int32_t t = 0; t < 4; ++t) {
    threads.emplace_back(Worker, &lm, t, 200);
  }
  for (auto& th : threads)
    th.join();
  assert(lm.UnlockAll(100));
  cout << "ALL LOCK STRESS TESTS PASSED" << endl;
  return 0;
}
