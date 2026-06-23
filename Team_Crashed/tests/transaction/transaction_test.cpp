#include <cassert>
#include <cstdio>

#include "transaction/lock_manager.h"
#include "transaction/transaction_manager.h"

using namespace minidb;

int main() {
    transaction::LockManager lm;
    assert(lm.acquireShared(1, 42) == Status::OK);
    assert(lm.acquireShared(2, 42) == Status::OK);
    assert(lm.acquireExclusive(3, 42) == Status::DEADLOCK);
    lm.releaseAll(1);
    lm.releaseAll(2);
    assert(lm.acquireExclusive(3, 42) == Status::OK);
    lm.releaseAll(3);

    transaction::TransactionManager tm;
    TransactionId t1 = tm.begin();
    TransactionId t2 = tm.begin();
    tm.recordWrite(t1, 7);
    assert(tm.commit(t1) == Status::OK);
    tm.recordWrite(t2, 7);
    assert(tm.commit(t2) == Status::TXN_CONFLICT);
    assert(tm.abort(t2) == Status::OK);

    std::printf("[OK] transaction locks and write conflict\n");
    return 0;
}
