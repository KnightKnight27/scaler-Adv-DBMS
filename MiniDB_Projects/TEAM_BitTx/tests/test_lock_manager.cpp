#include "transaction/lock_manager.h"

#include <cassert>
#include <cstdio>
#include <iostream>

using namespace minidb;
using namespace std;

int main() {
  LockManager lm;
  TxnId t1 = NextTxnId();
  TxnId t2 = NextTxnId();

  assert(lm.LockShared(t1, 100));
  assert(lm.LockShared(t2, 100));
  assert(!lm.LockExclusive(t1, 100));
  assert(lm.Unlock(t1, 100));
  assert(lm.Unlock(t2, 100));
  assert(lm.LockExclusive(t1, 100));
  assert(!lm.LockShared(t2, 100));
  assert(lm.UnlockAll(t1));
  assert(lm.LockShared(t2, 100));
  assert(lm.UnlockAll(t2));

  cout << "ALL LOCK MANAGER TESTS PASSED" << endl;
  return 0;
}