#pragma once

#include "catalog/Catalog.hpp"
#include "recovery/WAL.hpp"

namespace minidb {

class RecoveryManager {
public:
    RecoveryManager(WAL& wal, Catalog& catalog);
    void recover();

private:
    WAL& wal_;
    Catalog& catalog_;
    void redo(const LogRecord& rec);
    void undo(const LogRecord& rec);
};

}  // namespace minidb
