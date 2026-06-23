#pragma once

#include "common/types.h"

namespace minidb {

class BufferPoolManager;
class Catalog;
class Executor;
class LockManager;
class TransactionManager;
class WALManager;

Schema StudentSchema();
Schema CourseSchema();
void RunDemo(Executor& executor, Catalog& catalog, TransactionManager& txns,
             LockManager& locks, WALManager& wal);
void RunTerminal(Executor& executor, Catalog& catalog);

} // namespace minidb
