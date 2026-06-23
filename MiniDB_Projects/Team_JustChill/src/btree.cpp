// btree.cpp — Phase B: page-backed B+ Tree
#include "btree.h"

#include <cstring>

#include "buffer_pool.h"
#include "heap_file.h"
#include "page.h"

namespace minidb {
namespace {

// ---- On-page node layout (offsets within a 4 KB page) ----
//
//   0:  is_leaf  (uint8)
//   1:  num_keys (uint16)
//   3:  next     (int32)   leaf: next-leaf page id (-1 if last); internal: unused
//   8:  keys[]   (int64 × (kOrder+1))
//   KV: leaf -> rids[] (page_id u32 + slot_id u16, 6B each)
//       internal -> children[] (int32 page id each)
constexpr int OFF_IS_LEAF = 0;
constexpr int OFF_NUM_KEYS = 1;
constexpr int OFF_NEXT = 3;
constexpr int OFF_KEYS = 8;
constexpr int KEY_CAP = BPlusTree::kOrder + 1;          // 65
constexpr int OFF_VALS = OFF_KEYS + KEY_CAP * 8;        // rids / children region

constexpr uint32_t kTomb = 0xFFFFFFFFu;  // RID page_id sentinel == tombstone

uint8_t getLeaf(const char* d) { return static_cast<uint8_t>(d[OFF_IS_LEAF]); }
void setLeaf(char* d, uint8_t v) { d[OFF_IS_LEAF] = static_cast<char>(v); }

uint16_t getNumKeys(const char* d) {
  uint16_t v;
  std::memcpy(&v, d + OFF_NUM_KEYS, 2);
  return v;
}
void setNumKeys(char* d, uint16_t v) { std::memcpy(d + OFF_NUM_KEYS, &v, 2); }

int32_t getNext(const char* d) {
  int32_t v;
  std::memcpy(&v, d + OFF_NEXT, 4);
  return v;
}
void setNext(char* d, int32_t v) { std::memcpy(d + OFF_NEXT, &v, 4); }

int64_t getKey(const char* d, int i) {
  int64_t v;
  std::memcpy(&v, d + OFF_KEYS + i * 8, 8);
  return v;
}
void setKey(char* d, int i, int64_t v) { std::memcpy(d + OFF_KEYS + i * 8, &v, 8); }

RID getRid(const char* d, int i) {
  RID r;
  std::memcpy(&r.page_id, d + OFF_VALS + i * 6, 4);
  std::memcpy(&r.slot_id, d + OFF_VALS + i * 6 + 4, 2);
  return r;
}
void setRid(char* d, int i, RID r) {
  std::memcpy(d + OFF_VALS + i * 6, &r.page_id, 4);
  std::memcpy(d + OFF_VALS + i * 6 + 4, &r.slot_id, 2);
}

int32_t getChild(const char* d, int i) {
  int32_t v;
  std::memcpy(&v, d + OFF_VALS + i * 4, 4);
  return v;
}
void setChild(char* d, int i, int32_t v) { std::memcpy(d + OFF_VALS + i * 4, &v, 4); }

}  // namespace

// ---- open / meta page ----

void BPlusTree::open(::BufferPool* pool, ::HeapFile* heap, bool fresh) {
  pool_ = pool;
  heap_ = heap;
  if (fresh) {
    heap_->allocatePage();             // page 0: meta
    int root = heap_->allocatePage();  // page 1: empty root leaf
    Page* rp = pool_->getPage(root);
    setLeaf(rp->data, 1);
    setNumKeys(rp->data, 0);
    setNext(rp->data, -1);
    pool_->unpinPage(root, true);
    root_page_id_ = root;
    writeRootToMeta();
  } else {
    readRootFromMeta();
  }
}

void BPlusTree::readRootFromMeta() {
  Page* p = pool_->getPage(0);
  std::memcpy(&root_page_id_, p->data, 4);
  pool_->unpinPage(0, false);
}

void BPlusTree::writeRootToMeta() {
  Page* p = pool_->getPage(0);
  std::memcpy(p->data, &root_page_id_, 4);
  pool_->unpinPage(0, true);
}

// ---- traversal ----

int BPlusTree::findLeafPage(Key key) const {
  int pid = root_page_id_;
  while (true) {
    Page* p = pool_->getPage(pid);
    if (getLeaf(p->data)) {
      pool_->unpinPage(pid, false);
      return pid;
    }
    uint16_t nk = getNumKeys(p->data);
    int idx = 0;  // first separator > key (upper_bound) -> descend that child
    while (idx < nk && key >= getKey(p->data, idx)) ++idx;
    int child = getChild(p->data, idx);
    pool_->unpinPage(pid, false);
    pid = child;
  }
}

std::optional<RID> BPlusTree::search(Key key) const {
  int leaf = findLeafPage(key);
  Page* p = pool_->getPage(leaf);
  uint16_t nk = getNumKeys(p->data);
  std::optional<RID> out;
  for (int i = 0; i < nk; ++i) {
    int64_t k = getKey(p->data, i);
    if (k < key) continue;
    if (k > key) break;
    RID r = getRid(p->data, i);
    if (r.page_id != kTomb) {
      out = r;
      break;
    }
  }
  pool_->unpinPage(leaf, false);
  return out;
}

bool BPlusTree::remove(Key key) {
  int leaf = findLeafPage(key);
  Page* p = pool_->getPage(leaf);
  uint16_t nk = getNumKeys(p->data);
  bool removed = false;
  for (int i = 0; i < nk; ++i) {
    int64_t k = getKey(p->data, i);
    if (k < key) continue;
    if (k > key) break;
    if (getRid(p->data, i).page_id != kTomb) {
      setRid(p->data, i, RID{kTomb, 0});  // tombstone, no rebalancing
      removed = true;
      break;
    }
  }
  pool_->unpinPage(leaf, removed);
  return removed;
}

// ---- insert (with split) ----

BPlusTree::SplitResult BPlusTree::insertRec(int page_id, Key key, RID rid) {
  Page* p = pool_->getPage(page_id);
  char* d = p->data;

  if (getLeaf(d)) {
    uint16_t nk = getNumKeys(d);
    int pos = 0;
    while (pos < nk && getKey(d, pos) < key) ++pos;
    for (int i = nk; i > pos; --i) {  // shift to make room
      setKey(d, i, getKey(d, i - 1));
      setRid(d, i, getRid(d, i - 1));
    }
    setKey(d, pos, key);
    setRid(d, pos, rid);
    setNumKeys(d, ++nk);

    if (nk <= kOrder) {
      pool_->unpinPage(page_id, true);
      return {};
    }
    // Split: right half moves to a new leaf; its first key copies up.
    int mid = nk / 2;
    int right = heap_->allocatePage();
    Page* rp = pool_->getPage(right);
    char* rd = rp->data;
    setLeaf(rd, 1);
    int rcount = nk - mid;
    for (int i = 0; i < rcount; ++i) {
      setKey(rd, i, getKey(d, mid + i));
      setRid(rd, i, getRid(d, mid + i));
    }
    setNumKeys(rd, static_cast<uint16_t>(rcount));
    setNext(rd, getNext(d));
    setNext(d, right);
    setNumKeys(d, static_cast<uint16_t>(mid));
    Key sep = getKey(rd, 0);
    pool_->unpinPage(right, true);
    pool_->unpinPage(page_id, true);
    return {true, sep, right};
  }

  // Internal: descend (keeping this node pinned to absorb a child split).
  uint16_t nk = getNumKeys(d);
  int idx = 0;
  while (idx < nk && key >= getKey(d, idx)) ++idx;
  int child = getChild(d, idx);
  SplitResult cs = insertRec(child, key, rid);
  if (!cs.did_split) {
    pool_->unpinPage(page_id, false);
    return {};
  }

  nk = getNumKeys(d);
  for (int i = nk; i > idx; --i) setKey(d, i, getKey(d, i - 1));
  setKey(d, idx, cs.sep_key);
  for (int i = nk + 1; i > idx + 1; --i) setChild(d, i, getChild(d, i - 1));
  setChild(d, idx + 1, cs.new_right);
  setNumKeys(d, ++nk);

  if (nk <= kOrder) {
    pool_->unpinPage(page_id, true);
    return {};
  }
  // Split internal: the middle key moves up (kept in neither child).
  int mid = nk / 2;
  Key sep = getKey(d, mid);
  int right = heap_->allocatePage();
  Page* rp = pool_->getPage(right);
  char* rd = rp->data;
  setLeaf(rd, 0);
  int rkeys = nk - mid - 1;
  for (int i = 0; i < rkeys; ++i) setKey(rd, i, getKey(d, mid + 1 + i));
  for (int i = 0; i < rkeys + 1; ++i) setChild(rd, i, getChild(d, mid + 1 + i));
  setNumKeys(rd, static_cast<uint16_t>(rkeys));
  setNumKeys(d, static_cast<uint16_t>(mid));
  pool_->unpinPage(right, true);
  pool_->unpinPage(page_id, true);
  return {true, sep, right};
}

void BPlusTree::insert(Key key, RID rid) {
  SplitResult s = insertRec(root_page_id_, key, rid);
  if (!s.did_split) return;
  // Root split: grow a new internal root pointing at the two halves.
  int new_root = heap_->allocatePage();
  Page* p = pool_->getPage(new_root);
  char* d = p->data;
  setLeaf(d, 0);
  setNumKeys(d, 1);
  setNext(d, -1);
  setKey(d, 0, s.sep_key);
  setChild(d, 0, root_page_id_);
  setChild(d, 1, s.new_right);
  pool_->unpinPage(new_root, true);
  root_page_id_ = new_root;
  writeRootToMeta();
}

// ---- Iterator ----

void BPlusTree::Iterator::loadLeaf(int page_id) {
  keys_.clear();
  rids_.clear();
  if (page_id < 0) {
    next_leaf_ = -1;
    return;
  }
  Page* p = tree_->pool_->getPage(page_id);
  const char* d = p->data;
  uint16_t nk = getNumKeys(d);
  for (int i = 0; i < nk; ++i) {
    keys_.push_back(getKey(d, i));
    rids_.push_back(getRid(d, i));
  }
  next_leaf_ = getNext(d);
  tree_->pool_->unpinPage(page_id, false);
}

void BPlusTree::Iterator::skipToValid() {
  while (true) {
    if (idx_ >= static_cast<int>(keys_.size())) {
      if (next_leaf_ < 0) {
        valid_ = false;
        return;
      }
      loadLeaf(next_leaf_);
      idx_ = 0;
      continue;
    }
    if (keys_[idx_] > high_) {  // past the requested range
      valid_ = false;
      return;
    }
    if (rids_[idx_].page_id == kTomb) {  // skip tombstones
      ++idx_;
      continue;
    }
    valid_ = true;
    return;
  }
}

void BPlusTree::Iterator::next() {
  ++idx_;
  skipToValid();
}

BPlusTree::Iterator BPlusTree::range(Key low, Key high) const {
  Iterator it;
  it.tree_ = this;
  it.high_ = high;
  int leaf = findLeafPage(low);
  it.loadLeaf(leaf);
  it.idx_ = 0;
  while (it.idx_ < static_cast<int>(it.keys_.size()) && it.keys_[it.idx_] < low)
    ++it.idx_;
  it.skipToValid();
  return it;
}

}  // namespace minidb
