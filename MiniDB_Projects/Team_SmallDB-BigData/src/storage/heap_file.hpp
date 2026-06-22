#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../common/types.hpp"

class BufferPool;
class DiskManager;

// unordered tuples across slotted pages; the table row store
class HeapFile {
public:
    HeapFile(BufferPool& pool, DiskManager& disk);

    RowID insert(const std::string& tuple);
    std::optional<std::string> get(RowID rid);
    bool erase(RowID rid);

    // every live (RowID, tuple) pair
    std::vector<std::pair<RowID, std::string>> scan();

private:
    BufferPool&  pool_;
    DiskManager& disk_;
};
