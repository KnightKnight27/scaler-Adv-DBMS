// MiniDB - a Tuple is one row: an ordered list of Values plus the RID it lives at.
// Serialization is schema-driven and variable-length so VARCHAR works without padding:
//   INTEGER -> 4 bytes, BIGINT -> 8, BOOLEAN -> 1, VARCHAR -> 2-byte length + bytes.
#pragma once

#include <string>
#include <vector>
#include "rid.h"
#include "schema.h"
#include "types.h"

namespace minidb {

class Tuple {
public:
    Tuple() = default;
    explicit Tuple(std::vector<Value> values) : values_(std::move(values)) {}

    size_t Count() const { return values_.size(); }
    const Value& GetValue(size_t i) const { return values_[i]; }
    const std::vector<Value>& Values() const { return values_; }

    RID rid() const { return rid_; }
    void SetRid(RID r) { rid_ = r; }

    // Encode this tuple into a byte string according to `schema`.
    std::string Serialize(const Schema& schema) const;
    // Decode a tuple from `bytes` according to `schema`.
    static Tuple Deserialize(const std::string& bytes, const Schema& schema);

    std::string ToString() const;

private:
    std::vector<Value> values_;
    RID rid_;
};

}  // namespace minidb
