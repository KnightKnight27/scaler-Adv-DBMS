#include "catalog/catalog.h"

#include <cstring>

namespace minidb {

namespace {
// Simple cursor-based writer/reader over a byte buffer.
struct Writer {
  char* p;
  void I32(int32_t v) { std::memcpy(p, &v, 4); p += 4; }
  void I8(int8_t v)   { *p = static_cast<char>(v); p += 1; }
  void Str(const std::string& s) {
    I32(static_cast<int32_t>(s.size()));
    std::memcpy(p, s.data(), s.size());
    p += s.size();
  }
};
struct Reader {
  const char* p;
  int32_t I32() { int32_t v; std::memcpy(&v, p, 4); p += 4; return v; }
  int8_t  I8()  { int8_t v = static_cast<int8_t>(*p); p += 1; return v; }
  std::string Str() {
    int32_t n = I32();
    std::string s(p, p + n);
    p += n;
    return s;
  }
};
const int32_t kMagic = 0x4D444231;  // "MDB1"
}  // namespace

void Catalog::SerializeTo(char* page) const {
  std::memset(page, 0, PAGE_SIZE);
  Writer w{page};
  w.I32(kMagic);
  w.I32(static_cast<int32_t>(tables.size()));
  for (const TableInfo& t : tables) {
    w.Str(t.name);
    w.I32(t.heap_first);
    w.I32(t.pk_index_header);
    w.I32(static_cast<int32_t>(t.row_count));
    w.I32(t.schema.pk_index);
    w.I32(static_cast<int32_t>(t.schema.columns.size()));
    for (const Column& c : t.schema.columns) {
      w.Str(c.name);
      w.I8(static_cast<int8_t>(c.type == ValueType::INT ? 0 : 1));
      w.I32(c.length);
    }
  }
}

void Catalog::DeserializeFrom(const char* page) {
  tables.clear();
  Reader r{page};
  if (r.I32() != kMagic) return;  // fresh/empty meta page
  int32_t n = r.I32();
  for (int32_t i = 0; i < n; ++i) {
    TableInfo t;
    t.name = r.Str();
    t.heap_first = r.I32();
    t.pk_index_header = r.I32();
    t.row_count = r.I32();
    t.schema.pk_index = r.I32();
    int32_t ncols = r.I32();
    for (int32_t c = 0; c < ncols; ++c) {
      Column col;
      col.name = r.Str();
      col.type = (r.I8() == 0) ? ValueType::INT : ValueType::VARCHAR;
      col.length = r.I32();
      t.schema.columns.push_back(col);
    }
    tables.push_back(t);
  }
}

}  // namespace minidb
