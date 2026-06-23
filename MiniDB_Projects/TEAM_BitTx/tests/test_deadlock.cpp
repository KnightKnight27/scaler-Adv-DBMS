#include "transaction/lock_manager.h"

#include <cassert>
#include <iostream>

using namespace minidb;

int main() {
  LockManager lm;
  TxnId t1 = NextTxnId();
  TxnId t2 = NextTxnId();

  // t1 holds 100, t2 holds 200
  assert(lm.LockExclusive(t1, 100));
  assert(lm.LockExclusive(t2, 200));

  // t1 wants 200 (held by t2) — should block and register wait-for edge
  assert(!lm.LockExclusive(t1, 200));
  // t2 wants 100 (held by t1) — should block, completing cycle
  assert(!lm.LockExclusive(t2, 100));
  assert(lm.HasCycle(t1));
  assert(lm.HasCycle(t2));

  // break cycle by releasing t2's hold on 100
  lm.Unlock(t2, 100);
  lm.Unlock(t2, 200);
  assert(lm.LockExclusive(t1, 200));
  assert(!lm.HasCycle(t1));

  lm.UnlockAll(t1);
  lm.UnlockAll(t2);

  std::cout << "ALL DEADLOCK DETECTION TESTS PASSED" << std::endl;
  return 0;
}