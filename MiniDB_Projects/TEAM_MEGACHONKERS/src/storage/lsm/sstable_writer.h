#pragma once

#include <string>
#include <map>
#include <fstream>
#include <cstdint>

namespace minidb {

class SSTableWriter {
public:
    // Writes the sorted map of entries to a permanent SSTable file.
    // Returns true if successful.
    static bool WriteSSTable(const std::string& file_path, const std::map<std::string, std::string>& entries);
};

} // namespace minidb