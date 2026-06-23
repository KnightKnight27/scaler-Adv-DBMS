#pragma once

#include "catalog/catalog.h"
#include "recovery/wal.h"

namespace minidb {

class RecoveryManager {
public:
    RecoveryManager(Catalog* catalog, WriteAheadLog* wal);

    void Recover();
    void Checkpoint();

private:
    Catalog* catalog_;
    WriteAheadLog* wal_;
};

}  // namespace minidb
