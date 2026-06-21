#include "catalog/catalog.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "catalog/table.h"
#include "common/serialize.h"
#include "index/bplus_tree.h"
#include "storage/heap_file.h"

namespace walterdb {

namespace {
constexpr uint32_t kCatalogMagic = 0x57414C43;  // 'WALC'
constexpr uint32_t kCatalogVersion = 1;
}  // namespace

Catalog::Catalog(BufferPool* bpm, std::string catalog_path)
    : bpm_(bpm), path_(std::move(catalog_path)) {
  load();
}

Catalog::~Catalog() {
  // Persist any stat updates (row counts) accumulated during the session.
  save();
}

std::string Catalog::lower(const std::string& s) const {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

Status Catalog::create_table(const std::string& name, const Schema& schema, TableInfo** out) {
  std::string key = lower(name);
  if (tables_.count(key)) return Status::already_exists("table '" + name + "' already exists");
  if (schema.num_columns() == 0) return Status::invalid_argument("table needs at least one column");

  auto info = std::make_unique<TableInfo>();
  info->table_id = next_table_id_++;
  info->name = name;
  info->schema = schema;

  // Backing heap for the rows.
  auto heap = HeapFile::create(bpm_);
  info->heap_first_page = heap->first_page_id();

  // Primary-key index, if a PK column was declared.
  if (auto pk = schema.primary_key_index()) {
    info->pk_column = static_cast<int>(*pk);
    BPlusTree index = BPlusTree::create(bpm_);
    info->index_meta_page = index.meta_page_id();
  }

  // Make the freshly allocated heap/index pages durable BEFORE recording the
  // metadata that points at them, so the catalog never references a page that
  // isn't on disk.
  bpm_->flush_all();
  bpm_->disk()->sync();

  TableInfo* raw = info.get();
  tables_[key] = std::move(info);
  save();

  if (out) *out = raw;
  return {};
}

TableInfo* Catalog::get_table(const std::string& name) {
  auto it = tables_.find(lower(name));
  return it == tables_.end() ? nullptr : it->second.get();
}

Table* Catalog::open_table(const std::string& name) {
  std::lock_guard<std::mutex> guard(open_latch_);
  std::string key = lower(name);
  auto cached = open_.find(key);
  if (cached != open_.end()) return cached->second.get();

  auto it = tables_.find(key);
  if (it == tables_.end()) return nullptr;
  auto table = std::make_unique<Table>(bpm_, it->second.get());
  Table* raw = table.get();
  open_[key] = std::move(table);
  return raw;
}

Table* Catalog::open_table_by_id(uint32_t table_id) {
  std::string name;
  for (const auto& [k, info] : tables_) {
    if (info->table_id == table_id) { name = info->name; break; }
  }
  return name.empty() ? nullptr : open_table(name);
}

std::vector<std::string> Catalog::table_names() const {
  std::vector<std::string> names;
  names.reserve(tables_.size());
  for (const auto& [k, info] : tables_) names.push_back(info->name);
  return names;
}

void Catalog::save() const {
  ByteWriter w;
  w.put_u32(kCatalogMagic);
  w.put_u32(kCatalogVersion);
  w.put_u32(next_table_id_);
  w.put_u32(static_cast<uint32_t>(tables_.size()));
  for (const auto& [k, info] : tables_) {
    w.put_string(info->name);
    w.put_u32(info->table_id);
    w.put_i32(info->heap_first_page);
    w.put_i32(info->index_meta_page);
    w.put_i32(info->pk_column);
    w.put_u64(info->row_count);
    w.put_u32(static_cast<uint32_t>(info->schema.num_columns()));
    for (const Column& col : info->schema.columns()) {
      w.put_string(col.name);
      w.put_u8(static_cast<uint8_t>(col.type));
      w.put_u8(col.primary_key ? 1 : 0);
    }
  }

  // Write to a temp file then atomically rename, so a crash mid-write never
  // leaves a half-written catalog.
  std::string tmp = path_ + ".tmp";
  {
    std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
    os.write(w.str().data(), static_cast<std::streamsize>(w.str().size()));
  }
  std::rename(tmp.c_str(), path_.c_str());
}

void Catalog::load() {
  std::ifstream is(path_, std::ios::binary);
  if (!is) return;  // no catalog yet -> empty database
  std::stringstream ss;
  ss << is.rdbuf();
  std::string bytes = ss.str();
  if (bytes.empty()) return;

  ByteReader r(bytes);
  if (r.get_u32() != kCatalogMagic) return;  // not our file -> ignore
  r.get_u32();                               // version (only one so far)
  next_table_id_ = r.get_u32();
  uint32_t num_tables = r.get_u32();

  for (uint32_t t = 0; t < num_tables; ++t) {
    auto info = std::make_unique<TableInfo>();
    info->name = std::string(r.get_string());
    info->table_id = r.get_u32();
    info->heap_first_page = r.get_i32();
    info->index_meta_page = r.get_i32();
    info->pk_column = r.get_i32();
    info->row_count = r.get_u64();
    uint32_t ncols = r.get_u32();
    std::vector<Column> cols;
    cols.reserve(ncols);
    for (uint32_t c = 0; c < ncols; ++c) {
      Column col;
      col.name = std::string(r.get_string());
      col.type = static_cast<TypeId>(r.get_u8());
      col.primary_key = r.get_u8() != 0;
      cols.push_back(std::move(col));
    }
    info->schema = Schema(std::move(cols));
    tables_[lower(info->name)] = std::move(info);
  }
}

}  // namespace walterdb
