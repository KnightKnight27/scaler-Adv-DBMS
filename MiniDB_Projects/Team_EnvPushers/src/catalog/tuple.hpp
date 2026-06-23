// Tuple <-> bytes serialization, driven by a Schema.
//
// Layout per column: [1 null-flag byte][payload].
//   INTEGER -> 8 bytes, little-endian-ish (memcpy of int64).
//   TEXT    -> [4-byte length][bytes].
// A null flag of 1 means the value is SQL NULL and no payload follows.
#pragma once

#include <cstring>
#include <stdexcept>
#include <vector>

#include "catalog/schema.hpp"
#include "common/types.hpp"

namespace minidb {

inline std::vector<uint8_t> serialize_tuple(const Schema& schema, const Tuple& tuple) {
    if (tuple.size() != schema.size())
        throw std::runtime_error("serialize_tuple: arity mismatch");
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t n) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    };
    for (size_t i = 0; i < schema.size(); ++i) {
        const Value& v = tuple[i];
        uint8_t null_flag = v.is_null() ? 1 : 0;
        out.push_back(null_flag);
        if (null_flag) continue;
        if (schema.column(i).type == TypeId::INTEGER) {
            int64_t x = v.as_int();
            put(&x, sizeof(x));
        } else {
            const std::string& s = v.as_text();
            uint32_t len = static_cast<uint32_t>(s.size());
            put(&len, sizeof(len));
            put(s.data(), s.size());
        }
    }
    return out;
}

inline Tuple deserialize_tuple(const Schema& schema, const std::vector<uint8_t>& bytes) {
    Tuple tuple;
    tuple.reserve(schema.size());
    size_t off = 0;
    auto need = [&](size_t n) {
        if (off + n > bytes.size())
            throw std::runtime_error("deserialize_tuple: truncated record");
    };
    for (size_t i = 0; i < schema.size(); ++i) {
        need(1);
        uint8_t null_flag = bytes[off++];
        if (null_flag) { tuple.push_back(Value::Null()); continue; }
        if (schema.column(i).type == TypeId::INTEGER) {
            need(sizeof(int64_t));
            int64_t x;
            std::memcpy(&x, bytes.data() + off, sizeof(x));
            off += sizeof(x);
            tuple.push_back(Value::Int(x));
        } else {
            need(sizeof(uint32_t));
            uint32_t len;
            std::memcpy(&len, bytes.data() + off, sizeof(len));
            off += sizeof(len);
            need(len);
            std::string s(reinterpret_cast<const char*>(bytes.data() + off), len);
            off += len;
            tuple.push_back(Value::Text(std::move(s)));
        }
    }
    return tuple;
}

}  // namespace minidb
