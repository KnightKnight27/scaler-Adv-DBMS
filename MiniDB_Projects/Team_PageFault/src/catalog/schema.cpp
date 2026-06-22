#include "schema.hpp"
#include <cstring>
#include <cstdint>
#include <stdexcept>

namespace minidb {

std::string Schema::serialize(const std::vector<Value>& row) const {
    if (row.size() != cols_.size()) throw std::runtime_error("row arity mismatch");
    std::string out;
    for (size_t i = 0; i < cols_.size(); i++) {
        if (cols_[i].type == Type::INT) {
            int64_t v = row[i].i;
            out.append(reinterpret_cast<const char*>(&v), sizeof(v));
        } else {
            if (row[i].s.size() > UINT16_MAX)
                throw std::runtime_error("TEXT value too long (max 65535 bytes)");
            uint16_t len = static_cast<uint16_t>(row[i].s.size());
            out.append(reinterpret_cast<const char*>(&len), sizeof(len));
            out.append(row[i].s);
        }
    }
    return out;
}

std::vector<Value> Schema::deserialize(const std::string& bytes) const {
    std::vector<Value> row;
    row.reserve(cols_.size());
    size_t off = 0;
    for (size_t i = 0; i < cols_.size(); i++) {
        if (cols_[i].type == Type::INT) {
            int64_t v;
            std::memcpy(&v, bytes.data() + off, sizeof(v));
            off += sizeof(v);
            row.push_back(Value::makeInt(v));
        } else {
            uint16_t len;
            std::memcpy(&len, bytes.data() + off, sizeof(len));
            off += sizeof(len);
            row.push_back(Value::makeText(bytes.substr(off, len)));
            off += len;
        }
    }
    return row;
}

} // namespace minidb
