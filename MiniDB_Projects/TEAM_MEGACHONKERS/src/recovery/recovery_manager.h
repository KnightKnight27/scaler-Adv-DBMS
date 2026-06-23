#pragma once

#include "catalog/catalog.h"
#include <string>

namespace minidb {

class RecoveryManager {
public:
    // Reads the WAL file and replays all committed PUT and DELETE operations 
    // back into the memory structures to simulate crash recovery.
    static void ReplayWAL(const std::string& wal_file_path, TableMetadata* table);
};

} // namespace minidb