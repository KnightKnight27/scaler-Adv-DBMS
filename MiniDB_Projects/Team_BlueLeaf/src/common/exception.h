#pragma once

#include <stdexcept>
#include <string>

namespace minidb {

// Single exception type for all recoverable MiniDB errors (bad page checksum,
// parse error, lock conflict, etc.). Mirrors the repo's existing convention of
// throwing std::runtime_error with a descriptive message.
class DBException : public std::runtime_error {
public:
    explicit DBException(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace minidb
