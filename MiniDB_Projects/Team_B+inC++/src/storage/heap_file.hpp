#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../common/types.hpp"

class BufferPool;
class DiskManager;

// A HeapFile is an unordered collection of tuples spread across slotted pages.
// It is the table's row store: insert returns a stable RowID, get/erase work
// by RowID, and scan walks every live tuple (the SeqScan the executor uses).
// All page access goes through the buffer pool; the heap never touches disk.
class HeapFile {
public:
    HeapFile(BufferPool& pool, DiskManager& disk);

    RowID insert(const std::string& tuple);
    std::optional<std::string> get(RowID rid);
    bool erase(RowID rid);

    // Materialize every live (RowID, tuple) pair. Fine for a teaching-scale DB.
    std::vector<std::pair<RowID, std::string>> scan();

private:
    BufferPool&  pool_;
    DiskManager& disk_;
};
