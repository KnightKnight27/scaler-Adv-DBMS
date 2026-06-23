#pragma once

#include <string>
#include <vector>

#include "catalog/schema.h"
#include "common/types.h"

namespace minidb {

// Record encodes a row (a vector of Values) to/from the opaque byte string that
// the storage layer stores in a slot. The on-page format is simple and
// self-describing given the schema:
//
//   INT     -> 8 bytes (int64, host order)
//   DOUBLE  -> 8 bytes (double)
//   VARCHAR -> 2-byte length prefix + that many bytes
//
// Columns are laid out in schema order, so decoding only needs the schema.
class Record {
public:
    static std::string serialize(const Schema& schema, const std::vector<Value>& values);
    static std::vector<Value> deserialize(const Schema& schema, const char* buf, std::size_t len);
    static std::vector<Value> deserialize(const Schema& schema, const std::string& bytes) {
        return deserialize(schema, bytes.data(), bytes.size());
    }
};

} // namespace minidb
