#include "engine/rowstore_engine.h"

#include "catalog/record.h"
#include "common/exception.h"
#include "index/bplus_tree.h"
#include "storage/heap_file.h"

namespace minidb {

TableInfo& RowStoreEngine::require(const std::string& table) {
    TableInfo* t = catalog_->get_table(table);
    if (!t) throw DBException("RowStoreEngine: no such table: " + table);
    return *t;
}

void RowStoreEngine::create_table(const std::string& table, const Schema& schema, int pk_col) {
    if (!catalog_->has_table(table)) catalog_->create_table(table, schema, pk_col);
}

bool RowStoreEngine::put(const std::string& table, std::int64_t key, const std::string& row) {
    TableInfo& t = require(table);
    const IndexInfo* pk = t.primary_index();
    if (!pk) throw DBException("RowStoreEngine: table has no primary index: " + table);

    BPlusTree idx(bp_, pk->root);
    RID existing;
    if (idx.search(key, existing)) return false;  // duplicate primary key

    HeapFile heap(bp_, t.heap_first);
    auto it = heap_tail_.find(table);
    PageId tail = (it == heap_tail_.end()) ? INVALID_PAGE_ID : it->second;
    RID rid = heap.append(row, tail);   // O(1) append using the tail hint
    heap_tail_[table] = tail;

    PageId old_root = idx.root_page();
    idx.insert(key, rid);
    if (idx.root_page() != old_root)
        catalog_->update_index_root(table, pk->name, idx.root_page());
    return true;
}

bool RowStoreEngine::get(const std::string& table, std::int64_t key, std::string& out) {
    TableInfo& t = require(table);
    const IndexInfo* pk = t.primary_index();
    BPlusTree idx(bp_, pk->root);
    RID rid;
    if (!idx.search(key, rid)) return false;
    HeapFile heap(bp_, t.heap_first);
    return heap.get(rid, out);
}

bool RowStoreEngine::erase(const std::string& table, std::int64_t key) {
    TableInfo& t = require(table);
    const IndexInfo* pk = t.primary_index();
    BPlusTree idx(bp_, pk->root);
    RID rid;
    if (!idx.search(key, rid)) return false;
    HeapFile heap(bp_, t.heap_first);
    heap.erase(rid);
    idx.erase(key);
    return true;
}

// --- cursors ---
namespace {

// Full table scan over the heap (physical order). Decodes each row's key.
class HeapScanCursor : public StorageEngine::Cursor {
public:
    HeapScanCursor(BufferPool* bp, PageId first, Schema schema, int pk_col)
        : iter_(bp, first), schema_(std::move(schema)), pk_col_(pk_col) {}
    bool next(std::int64_t& key, std::string& row) override {
        RID rid;
        if (!iter_.next(rid, row)) return false;
        key = std::get<std::int64_t>(
            Record::deserialize(schema_, row)[static_cast<std::size_t>(pk_col_)]);
        return true;
    }
private:
    HeapFile::Iterator iter_;
    Schema             schema_;
    int                pk_col_;
};

// Bounded key range scan via the B+Tree, fetching each row from the heap.
class IndexRangeCursor : public StorageEngine::Cursor {
public:
    IndexRangeCursor(BufferPool* bp, PageId root, PageId heap_first, std::int64_t lo, std::int64_t hi)
        : bp_(bp), heap_first_(heap_first), iter_(BPlusTree(bp, root).range(lo, hi)) {}
    bool next(std::int64_t& key, std::string& row) override {
        BTKey k; RID rid;
        if (!iter_.next(k, rid)) return false;
        key = k;
        HeapFile(bp_, heap_first_).get(rid, row);
        return true;
    }
private:
    BufferPool*               bp_;
    PageId                    heap_first_;
    BPlusTree::RangeIterator  iter_;
};

} // namespace

std::unique_ptr<StorageEngine::Cursor> RowStoreEngine::scan(const std::string& table) {
    TableInfo& t = require(table);
    return std::make_unique<HeapScanCursor>(bp_, t.heap_first, t.schema, t.pk_col);
}

std::unique_ptr<StorageEngine::Cursor> RowStoreEngine::range(const std::string& table,
                                                             std::int64_t lo, std::int64_t hi) {
    TableInfo& t = require(table);
    const IndexInfo* pk = t.primary_index();
    return std::make_unique<IndexRangeCursor>(bp_, pk->root, t.heap_first, lo, hi);
}

void RowStoreEngine::flush() {
    bp_->flush_all();
    catalog_->save();
}

EngineStats RowStoreEngine::stats(const std::string& table) {
    EngineStats s;
    s.bytes_on_disk = static_cast<std::uint64_t>(disk_->num_pages()) * PAGE_SIZE;
    auto cur = scan(table);
    std::int64_t key; std::string row;
    while (cur->next(key, row)) ++s.live_rows;
    return s;
}

} // namespace minidb
