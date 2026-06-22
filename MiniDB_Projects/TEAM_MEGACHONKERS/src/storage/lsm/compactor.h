#pragma once

#include <string>
#include <vector>

namespace minidb {

class Compactor {
public:
    // Takes a list of SSTable file paths (ordered from OLDEST to NEWEST)
    // Merges them into a single output file, and deletes the old files.
    static bool Compact(const std::vector<std::string>& input_sstables, const std::string& output_sstable);
};

} // namespace minidb