#pragma once
#include <string>

namespace minidb {

class Compaction {
public:
    // Compiles two SSTables into a new one
    static void compact(const std::string& input1, const std::string& input2, const std::string& output);
};

} // namespace minidb
