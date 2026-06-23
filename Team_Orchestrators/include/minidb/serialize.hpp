#pragma once
// Fixed, deterministic byte encoding of tuples given a schema.
//   INT     -> 8 bytes little-endian
//   DOUBLE  -> 8 bytes IEEE-754
//   VARCHAR -> 4-byte length prefix + raw bytes
#include "minidb/schema.hpp"
#include "minidb/types.hpp"
#include <cstring>
#include <cstdint>
#include <vector>

namespace minidb {

inline void put_u32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
inline uint32_t get_u32(const uint8_t* p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
  return v;
}

inline std::vector<uint8_t> serialize_tuple(const Schema& schema, const Tuple& t) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < schema.size(); ++i) {
    switch (schema.column(i).type) {
      case TypeId::Int: {
        int64_t v = t[i].as_int();
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), b, b + 8);
        break;
      }
      case TypeId::Double: {
        double v = t[i].as_double();
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), b, b + 8);
        break;
      }
      case TypeId::Varchar: {
        const std::string& s = t[i].as_string();
        put_u32(out, static_cast<uint32_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
        break;
      }
    }
  }
  return out;
}

inline Tuple deserialize_tuple(const Schema& schema, const uint8_t* p, size_t len) {
  Tuple t;
  t.reserve(schema.size());
  size_t off = 0;
  for (size_t i = 0; i < schema.size(); ++i) {
    switch (schema.column(i).type) {
      case TypeId::Int: {
        int64_t v;
        std::memcpy(&v, p + off, 8);
        off += 8;
        t.emplace_back(v);
        break;
      }
      case TypeId::Double: {
        double v;
        std::memcpy(&v, p + off, 8);
        off += 8;
        t.emplace_back(v);
        break;
      }
      case TypeId::Varchar: {
        uint32_t n = get_u32(p + off);
        off += 4;
        std::string s(reinterpret_cast<const char*>(p + off), n);
        off += n;
        t.emplace_back(std::move(s));
        break;
      }
    }
  }
  (void)len;
  return t;
}

}  // namespace minidb
