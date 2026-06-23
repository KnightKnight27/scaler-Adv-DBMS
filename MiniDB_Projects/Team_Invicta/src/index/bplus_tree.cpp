#include "index/bplus_tree.h"
#include <cstring>
#include <stdexcept>

namespace minidb {
namespace {

// --- Node byte layout (within a 4 KB page) --------------------------------
//   [0]      node type        (1 = leaf, 0 = internal)
//   [2..4)   size             (uint16 — number of keys/entries)
//   [4..8)   next_leaf        (int32 — leaves only)
//   [8..)    leaf:  entries   = key(8) + page_id(4) + slot(2)  (14 bytes each)
//            inner: children  = page_id(4) each, then keys = int64(8) each
constexpr int LEAF_ENTRY_BASE = 8;
constexpr int LEAF_ENTRY_SIZE = 14;
constexpr int INT_CHILD_BASE  = 8;

bool IsLeaf(const char *d) { return d[0] == 1; }
void SetType(char *d, bool leaf) { d[0] = leaf ? 1 : 0; }

uint16_t GetSize(const char *d) {
  uint16_t v;
  std::memcpy(&v, d + 2, 2);
  return v;
}
void SetSize(char *d, uint16_t v) { std::memcpy(d + 2, &v, 2); }

page_id_t GetNext(const char *d) {
  int32_t v;
  std::memcpy(&v, d + 4, 4);
  return v;
}
void SetNext(char *d, page_id_t v) { std::memcpy(d + 4, &v, 4); }

// Leaf entry accessors.
int64_t LeafKey(const char *d, int i) {
  int64_t k;
  std::memcpy(&k, d + LEAF_ENTRY_BASE + i * LEAF_ENTRY_SIZE, 8);
  return k;
}
RID LeafRID(const char *d, int i) {
  RID r;
  const char *p = d + LEAF_ENTRY_BASE + i * LEAF_ENTRY_SIZE + 8;
  std::memcpy(&r.page_id, p, 4);
  std::memcpy(&r.slot, p + 4, 2);
  return r;
}
void SetLeafEntry(char *d, int i, int64_t k, const RID &r) {
  char *p = d + LEAF_ENTRY_BASE + i * LEAF_ENTRY_SIZE;
  std::memcpy(p, &k, 8);
  std::memcpy(p + 8, &r.page_id, 4);
  std::memcpy(p + 12, &r.slot, 2);
}

// Internal node: children grow from INT_CHILD_BASE; keys live in the upper part
// of the page. We leave a 2-child margin so a node can transiently overflow by
// one during a split without the regions colliding.
constexpr int INT_KEY_BASE = INT_CHILD_BASE + (BPlusTree::INTERNAL_MAX + 2) * 4;

page_id_t Child(const char *d, int i) {
  page_id_t c;
  std::memcpy(&c, d + INT_CHILD_BASE + i * 4, 4);
  return c;
}
void SetChild(char *d, int i, page_id_t c) {
  std::memcpy(d + INT_CHILD_BASE + i * 4, &c, 4);
}
int64_t IntKey(const char *d, int i) {
  int64_t k;
  std::memcpy(&k, d + INT_KEY_BASE + i * 8, 8);
  return k;
}
void SetIntKey(char *d, int i, int64_t k) {
  std::memcpy(d + INT_KEY_BASE + i * 8, &k, 8);
}

}  // namespace

BPlusTree::BPlusTree(BufferPoolManager *bpm, page_id_t *header_page_id)
    : bpm_(bpm), header_page_id_(*header_page_id) {
  if (header_page_id_ == INVALID_PAGE_ID) {
    Page *h = bpm_->NewPage(&header_page_id_);
    if (!h) throw std::runtime_error("BPlusTree: no frame for header page");
    page_id_t root = INVALID_PAGE_ID;
    std::memcpy(h->data(), &root, sizeof(root));  // empty tree
    bpm_->UnpinPage(header_page_id_, true);
    *header_page_id = header_page_id_;
  }
}

page_id_t BPlusTree::GetRoot() {
  Page *h = bpm_->FetchPage(header_page_id_);
  page_id_t root;
  std::memcpy(&root, h->data(), sizeof(root));
  bpm_->UnpinPage(header_page_id_, false);
  return root;
}

void BPlusTree::SetRoot(page_id_t root) {
  Page *h = bpm_->FetchPage(header_page_id_);
  std::memcpy(h->data(), &root, sizeof(root));
  bpm_->UnpinPage(header_page_id_, true);
}

page_id_t BPlusTree::FindLeaf(int64_t key, std::vector<page_id_t> *path) {
  page_id_t pid = GetRoot();
  while (true) {
    Page *page = bpm_->FetchPage(pid);
    const char *d = page->data();
    if (IsLeaf(d)) {
      bpm_->UnpinPage(pid, false);
      return pid;
    }
    if (path) path->push_back(pid);
    uint16_t size = GetSize(d);
    int idx = 0;  // go right while key >= separator
    while (idx < size && key >= IntKey(d, idx)) ++idx;
    page_id_t child = Child(d, idx);
    bpm_->UnpinPage(pid, false);
    pid = child;
  }
}

bool BPlusTree::GetValue(int64_t key, RID *out) {
  if (GetRoot() == INVALID_PAGE_ID) return false;
  page_id_t leaf = FindLeaf(key, nullptr);
  Page *page = bpm_->FetchPage(leaf);
  const char *d = page->data();
  uint16_t size = GetSize(d);
  bool found = false;
  for (int i = 0; i < size; ++i) {
    if (LeafKey(d, i) == key) {
      *out = LeafRID(d, i);
      found = true;
      break;
    }
  }
  bpm_->UnpinPage(leaf, false);
  return found;
}

bool BPlusTree::Insert(int64_t key, const RID &rid) {
  // Empty tree: create the root leaf.
  if (GetRoot() == INVALID_PAGE_ID) {
    page_id_t pid;
    Page *page = bpm_->NewPage(&pid);
    char *d = page->data();
    SetType(d, true);
    SetSize(d, 1);
    SetNext(d, INVALID_PAGE_ID);
    SetLeafEntry(d, 0, key, rid);
    bpm_->UnpinPage(pid, true);
    SetRoot(pid);
    return true;
  }

  std::vector<page_id_t> path;
  page_id_t leaf_pid = FindLeaf(key, &path);
  Page *page = bpm_->FetchPage(leaf_pid);
  char *d = page->data();
  uint16_t size = GetSize(d);

  // Read entries into a buffer, inserting in sorted order (reject duplicates).
  std::vector<std::pair<int64_t, RID>> ents;
  ents.reserve(size + 1);
  bool inserted = false;
  for (int i = 0; i < size; ++i) {
    int64_t k = LeafKey(d, i);
    if (!inserted && key < k) {
      ents.emplace_back(key, rid);
      inserted = true;
    }
    if (k == key) {  // duplicate primary key
      bpm_->UnpinPage(leaf_pid, false);
      return false;
    }
    ents.emplace_back(k, LeafRID(d, i));
  }
  if (!inserted) ents.emplace_back(key, rid);

  if (static_cast<int>(ents.size()) <= LEAF_MAX) {
    for (size_t i = 0; i < ents.size(); ++i) SetLeafEntry(d, i, ents[i].first, ents[i].second);
    SetSize(d, static_cast<uint16_t>(ents.size()));
    bpm_->UnpinPage(leaf_pid, true);
    return true;
  }

  // Split the leaf: left keeps [0,mid), right gets [mid,end).
  size_t total = ents.size();
  size_t mid = total / 2;
  page_id_t right_pid;
  Page *right = bpm_->NewPage(&right_pid);
  char *rd = right->data();
  SetType(rd, true);
  SetSize(rd, static_cast<uint16_t>(total - mid));
  for (size_t i = mid; i < total; ++i) SetLeafEntry(rd, i - mid, ents[i].first, ents[i].second);
  SetNext(rd, GetNext(d));  // right inherits old next

  SetSize(d, static_cast<uint16_t>(mid));
  for (size_t i = 0; i < mid; ++i) SetLeafEntry(d, i, ents[i].first, ents[i].second);
  SetNext(d, right_pid);  // left now points at right

  int64_t split_key = ents[mid].first;
  bpm_->UnpinPage(right_pid, true);
  bpm_->UnpinPage(leaf_pid, true);

  InsertIntoParent(path, split_key, leaf_pid, right_pid);
  return true;
}

void BPlusTree::InsertIntoParent(std::vector<page_id_t> &path, int64_t key,
                                 page_id_t left_child, page_id_t right_child) {
  if (path.empty()) {
    // The split node was the root: grow a new internal root.
    page_id_t root_pid;
    Page *root = bpm_->NewPage(&root_pid);
    char *d = root->data();
    SetType(d, false);
    SetSize(d, 1);
    SetChild(d, 0, left_child);
    SetIntKey(d, 0, key);
    SetChild(d, 1, right_child);
    bpm_->UnpinPage(root_pid, true);
    SetRoot(root_pid);
    return;
  }

  page_id_t parent_pid = path.back();
  path.pop_back();
  Page *page = bpm_->FetchPage(parent_pid);
  char *d = page->data();
  uint16_t size = GetSize(d);

  // Read keys/children into buffers, then splice in the new separator.
  std::vector<int64_t> keys;
  std::vector<page_id_t> kids;
  for (int i = 0; i < size; ++i) keys.push_back(IntKey(d, i));
  for (int i = 0; i <= size; ++i) kids.push_back(Child(d, i));

  int pos = 0;
  while (pos < static_cast<int>(keys.size()) && keys[pos] < key) ++pos;
  keys.insert(keys.begin() + pos, key);
  kids.insert(kids.begin() + pos + 1, right_child);

  auto write_internal = [](char *dst, const std::vector<int64_t> &ks,
                           const std::vector<page_id_t> &cs) {
    SetType(dst, false);
    SetSize(dst, static_cast<uint16_t>(ks.size()));
    for (size_t i = 0; i < ks.size(); ++i) SetIntKey(dst, i, ks[i]);
    for (size_t i = 0; i < cs.size(); ++i) SetChild(dst, i, cs[i]);
  };

  if (static_cast<int>(keys.size()) <= INTERNAL_MAX) {
    write_internal(d, keys, kids);
    bpm_->UnpinPage(parent_pid, true);
    return;
  }

  // Split internal: promote the middle key, split keys/children around it.
  size_t total = keys.size();
  size_t mid = total / 2;
  int64_t promote = keys[mid];

  std::vector<int64_t> lkeys(keys.begin(), keys.begin() + mid);
  std::vector<page_id_t> lkids(kids.begin(), kids.begin() + mid + 1);
  std::vector<int64_t> rkeys(keys.begin() + mid + 1, keys.end());
  std::vector<page_id_t> rkids(kids.begin() + mid + 1, kids.end());

  page_id_t right_pid;
  Page *right = bpm_->NewPage(&right_pid);
  write_internal(right->data(), rkeys, rkids);
  write_internal(d, lkeys, lkids);
  bpm_->UnpinPage(right_pid, true);
  bpm_->UnpinPage(parent_pid, true);

  InsertIntoParent(path, promote, parent_pid, right_pid);
}

bool BPlusTree::Delete(int64_t key) {
  if (GetRoot() == INVALID_PAGE_ID) return false;
  page_id_t leaf = FindLeaf(key, nullptr);
  Page *page = bpm_->FetchPage(leaf);
  char *d = page->data();
  uint16_t size = GetSize(d);

  std::vector<std::pair<int64_t, RID>> ents;
  bool removed = false;
  for (int i = 0; i < size; ++i) {
    int64_t k = LeafKey(d, i);
    if (k == key) { removed = true; continue; }
    ents.emplace_back(k, LeafRID(d, i));
  }
  if (removed) {
    for (size_t i = 0; i < ents.size(); ++i) SetLeafEntry(d, i, ents[i].first, ents[i].second);
    SetSize(d, static_cast<uint16_t>(ents.size()));
  }
  bpm_->UnpinPage(leaf, removed);
  return removed;
}

std::vector<std::pair<int64_t, RID>> BPlusTree::Range(int64_t low, int64_t high) {
  std::vector<std::pair<int64_t, RID>> out;
  if (GetRoot() == INVALID_PAGE_ID || low > high) return out;
  page_id_t pid = FindLeaf(low, nullptr);
  while (pid != INVALID_PAGE_ID) {
    Page *page = bpm_->FetchPage(pid);
    const char *d = page->data();
    uint16_t size = GetSize(d);
    bool done = false;
    for (int i = 0; i < size; ++i) {
      int64_t k = LeafKey(d, i);
      if (k > high) { done = true; break; }
      if (k >= low) out.emplace_back(k, LeafRID(d, i));
    }
    page_id_t next = GetNext(d);
    bpm_->UnpinPage(pid, false);
    if (done) break;
    pid = next;
  }
  return out;
}

}  // namespace minidb
