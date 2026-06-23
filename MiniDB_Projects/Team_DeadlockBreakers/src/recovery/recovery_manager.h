#pragma once

#include <cstddef>

namespace minidb {

class Catalog;
class WALManager;

struct RecoveryResult {
    size_t redone = 0;
    size_t ignored = 0;
};

// REDO committed WAL records. Operations are idempotent by primary key.
class RecoveryManager {
public:
    RecoveryManager(Catalog* catalog, WALManager* wal)
        : catalog_(catalog), wal_(wal) {}

    RecoveryResult Recover();

private:
    Catalog* catalog_;
    WALManager* wal_;
};

} // namespace minidb
