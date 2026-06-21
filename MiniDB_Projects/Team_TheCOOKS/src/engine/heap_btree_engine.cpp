#include "engine/heap_btree_engine.h"

#include <sys/stat.h>

#include <utility>
#include <vector>

#include "buffer/page.h"
#include "common/serialize.h"

namespace walterdb {

namespace {
// Cursor over an already-materialised, ordered list of key/value pairs.
class VectorIterator : public KVIterator {
 public:
  explicit VectorIterator(std::vector<std::pair<std::string, std::string>> items)
      : items_(std::move(items)) {}
  bool valid() const override { return pos_ < items_.size(); }
  void next() override { ++pos_; }
  std::string_view key() const override { return items_[pos_].first; }
  std::string_view value() const override { return items_[pos_].second; }

 private:
  std::vector<std::pair<std::string, std::string>> items_;
  size_t pos_ = 0;
};
}  // namespace

HeapBTreeEngine::HeapBTreeEngine(const std::string& path, size_t buffer_frames)
    : disk_(path), pool_(&disk_, buffer_frames, /*k=*/2) {
  if (disk_.num_pages() == 0) {
    // Fresh store: page 0 is the header recording where the heap and index live.
    page_id_t header;
    Page* hp = pool_.new_page(&header);  // page 0
    auto heap = HeapFile::create(&pool_);
    BPlusTree index = BPlusTree::create(&pool_);
    store_u32(hp->data() + 0, static_cast<uint32_t>(heap->first_page_id()));
    store_u32(hp->data() + 4, static_cast<uint32_t>(index.meta_page_id()));
    pool_.unpin_page(header, true);
    pool_.flush_all();
    disk_.sync();
    heap_ = std::move(heap);
    index_ = std::make_unique<BPlusTree>(&pool_, index.meta_page_id());
  } else {
    Page* hp = pool_.fetch_page(0);
    page_id_t heap_first = static_cast<page_id_t>(load_u32(hp->data() + 0));
    page_id_t index_meta = static_cast<page_id_t>(load_u32(hp->data() + 4));
    pool_.unpin_page(0, false);
    heap_ = std::make_unique<HeapFile>(&pool_, heap_first);
    index_ = std::make_unique<BPlusTree>(&pool_, index_meta);
  }
}

HeapBTreeEngine::~HeapBTreeEngine() {
  pool_.flush_all();
  disk_.sync();
}

Status HeapBTreeEngine::put(std::string_view key, std::string_view value) {
  if (auto old = index_->search(key)) heap_->erase(*old);  // reclaim the old slot (tombstone)
  RID rid = heap_->insert(value);
  index_->insert(key, rid);  // upsert: overwrites the RID if the key already existed
  return {};
}

std::optional<std::string> HeapBTreeEngine::get(std::string_view key) {
  auto rid = index_->search(key);
  if (!rid) return std::nullopt;
  return heap_->get(*rid);
}

Status HeapBTreeEngine::remove(std::string_view key) {
  auto rid = index_->search(key);
  if (!rid) return {};
  index_->erase(key);
  heap_->erase(*rid);
  return {};
}

std::unique_ptr<KVIterator> HeapBTreeEngine::scan(std::string_view lo, std::string_view hi) {
  std::vector<std::pair<std::string, std::string>> out;
  for (auto it = index_->range(lo, hi); it.valid(); it.next()) {
    if (auto v = heap_->get(it.rid())) out.emplace_back(std::string(it.key()), std::move(*v));
  }
  return std::make_unique<VectorIterator>(std::move(out));
}

void HeapBTreeEngine::flush() {
  pool_.flush_all();
  disk_.sync();
}

uint64_t HeapBTreeEngine::disk_size() const {
  struct stat st {};
  return ::stat(disk_.path().c_str(), &st) == 0 ? static_cast<uint64_t>(st.st_size) : 0;
}

}  // namespace walterdb
