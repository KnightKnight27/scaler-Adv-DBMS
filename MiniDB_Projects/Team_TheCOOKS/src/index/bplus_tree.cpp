#include "index/bplus_tree.h"

#include <cstring>
#include <stdexcept>
#include <utility>

#include "buffer/page.h"
#include "common/serialize.h"

namespace walterdb {
namespace {

// --- node geometry (fixed fanout, bounded key length) ----------------------
constexpr size_t MAX_KEY_LEN = BPlusTree::MAX_KEY_LEN;       // 64
constexpr size_t KEY_SLOT = 2 + MAX_KEY_LEN;                 // u16 len + padded key = 66
constexpr size_t RID_SIZE = 6;                              // i32 page + u16 slot
constexpr size_t NODE_HEADER = 16;                         // type, num_keys, next_leaf
constexpr uint8_t NODE_LEAF = 1;
constexpr uint8_t NODE_INTERNAL = 0;

constexpr size_t LEAF_CAP = (PAGE_SIZE - NODE_HEADER) / (KEY_SLOT + RID_SIZE);   // 56
constexpr size_t LEAF_RIDS_OFF = NODE_HEADER + LEAF_CAP * KEY_SLOT;
constexpr size_t INT_KEY_CAP = (PAGE_SIZE - NODE_HEADER - 4) / (KEY_SLOT + 4);   // 58
constexpr size_t INT_CHILD_OFF = NODE_HEADER + INT_KEY_CAP * KEY_SLOT;

// A typed view over a B+tree node page.  Keys are stored as [u16 len][bytes]
// in a fixed-stride array starting right after the header; leaves keep a
// parallel RID array, internals keep a parallel child-pointer array.
class Node {
 public:
  explicit Node(char* d) : d_(d) {}

  bool is_leaf() const { return static_cast<uint8_t>(d_[0]) == NODE_LEAF; }
  void set_leaf(bool leaf) { d_[0] = static_cast<char>(leaf ? NODE_LEAF : NODE_INTERNAL); }
  uint16_t num_keys() const { return load_u16(d_ + 2); }
  void set_num_keys(uint16_t n) { store_u16(d_ + 2, n); }
  page_id_t next_leaf() const { return static_cast<page_id_t>(load_u32(d_ + 4)); }
  void set_next_leaf(page_id_t p) { store_u32(d_ + 4, static_cast<uint32_t>(p)); }

  void init_leaf() {
    std::memset(d_, 0, PAGE_SIZE);
    set_leaf(true);
    set_num_keys(0);
    set_next_leaf(INVALID_PAGE_ID);
  }
  void init_internal() {
    std::memset(d_, 0, PAGE_SIZE);
    set_leaf(false);
    set_num_keys(0);
  }

  std::string_view key_at(size_t i) const {
    const char* p = d_ + NODE_HEADER + i * KEY_SLOT;
    return std::string_view(p + 2, load_u16(p));
  }
  void set_key_at(size_t i, std::string_view k) {
    char* p = d_ + NODE_HEADER + i * KEY_SLOT;
    store_u16(p, static_cast<uint16_t>(k.size()));
    std::memcpy(p + 2, k.data(), k.size());
  }

  RID rid_at(size_t i) const {
    const char* p = d_ + LEAF_RIDS_OFF + i * RID_SIZE;
    return RID{static_cast<page_id_t>(load_u32(p)), load_u16(p + 4)};
  }
  void set_rid_at(size_t i, RID r) {
    char* p = d_ + LEAF_RIDS_OFF + i * RID_SIZE;
    store_u32(p, static_cast<uint32_t>(r.page_id));
    store_u16(p + 4, r.slot);
  }

  page_id_t child_at(size_t i) const {
    return static_cast<page_id_t>(load_u32(d_ + INT_CHILD_OFF + i * 4));
  }
  void set_child_at(size_t i, page_id_t p) {
    store_u32(d_ + INT_CHILD_OFF + i * 4, static_cast<uint32_t>(p));
  }

 private:
  char* d_;
};

// Index of the child to descend into for `key`: the first separator strictly
// greater than key (children left-to-right cover increasing key ranges).
size_t child_index_for(const Node& n, std::string_view key) {
  uint16_t nk = n.num_keys();
  for (size_t i = 0; i < nk; ++i) {
    if (key.compare(n.key_at(i)) < 0) return i;
  }
  return nk;
}

// RAII guard that unpins a pinned page on scope exit -- so if a deeper split
// throws (e.g. the buffer pool is exhausted), every ancestor frame still on the
// recursive stack is unpinned during stack unwinding instead of leaking its pin.
struct PinGuard {
  BufferPool* bpm;
  page_id_t pid;
  bool dirty = false;
  PinGuard(BufferPool* b, page_id_t p) : bpm(b), pid(p) {}
  ~PinGuard() { bpm->unpin_page(pid, dirty); }
  PinGuard(const PinGuard&) = delete;
  PinGuard& operator=(const PinGuard&) = delete;
  void mark_dirty() { dirty = true; }
};

}  // namespace

// ---------------------------------------------------------------------------

BPlusTree::BPlusTree(BufferPool* bpm, page_id_t meta_page_id)
    : bpm_(bpm), meta_page_id_(meta_page_id) {}

BPlusTree BPlusTree::create(BufferPool* bpm) {
  page_id_t meta_pid, root_pid;
  Page* mp = bpm->new_page(&meta_pid);
  Page* rp = bpm->new_page(&root_pid);
  if (!mp || !rp) throw std::runtime_error("BPlusTree::create: no frame available");
  Node root(rp->data());
  root.init_leaf();
  store_u32(mp->data(), static_cast<uint32_t>(root_pid));  // meta page: root id at offset 0
  bpm->unpin_page(root_pid, true);
  bpm->unpin_page(meta_pid, true);
  return BPlusTree(bpm, meta_pid);
}

page_id_t BPlusTree::root_page_id() const {
  Page* mp = bpm_->fetch_page(meta_page_id_);
  page_id_t root = static_cast<page_id_t>(load_u32(mp->data()));
  bpm_->unpin_page(meta_page_id_, false);
  return root;
}

void BPlusTree::set_root_page_id(page_id_t pid) {
  Page* mp = bpm_->fetch_page(meta_page_id_);
  store_u32(mp->data(), static_cast<uint32_t>(pid));
  bpm_->unpin_page(meta_page_id_, true);
}

page_id_t BPlusTree::find_leaf(std::string_view key) const {
  page_id_t pid = root_page_id();
  while (true) {
    Page* p = bpm_->fetch_page(pid);
    if (!p) throw std::runtime_error("BPlusTree::find_leaf: fetch failed");
    Node n(p->data());
    if (n.is_leaf()) {
      bpm_->unpin_page(pid, false);
      return pid;
    }
    page_id_t child = n.child_at(child_index_for(n, key));
    bpm_->unpin_page(pid, false);
    pid = child;
  }
}

std::optional<RID> BPlusTree::search(std::string_view key) const {
  page_id_t leaf = find_leaf(key);
  Page* p = bpm_->fetch_page(leaf);
  Node n(p->data());
  std::optional<RID> out;
  uint16_t nk = n.num_keys();
  for (size_t i = 0; i < nk; ++i) {
    if (key.compare(n.key_at(i)) == 0) {
      out = n.rid_at(i);
      break;
    }
  }
  bpm_->unpin_page(leaf, false);
  return out;
}

bool BPlusTree::insert(std::string_view key, RID rid) {
  if (key.size() > MAX_KEY_LEN) {
    throw std::invalid_argument("BPlusTree::insert: key exceeds MAX_KEY_LEN");
  }
  bool inserted_new = false;
  page_id_t old_root = root_page_id();
  auto split = insert_rec(old_root, key, rid, &inserted_new);
  if (split) {
    // The root split: grow the tree one level with a fresh internal root.
    page_id_t nr_pid;
    Page* nrp = bpm_->new_page(&nr_pid);
    if (!nrp) throw std::runtime_error("BPlusTree::insert: no frame for new root");
    Node nr(nrp->data());
    nr.init_internal();
    nr.set_num_keys(1);
    nr.set_key_at(0, split->sep_key);
    nr.set_child_at(0, old_root);
    nr.set_child_at(1, split->right_page);
    bpm_->unpin_page(nr_pid, true);
    set_root_page_id(nr_pid);
  }
  return inserted_new;
}

std::optional<BPlusTree::SplitResult> BPlusTree::insert_rec(page_id_t node_pid,
                                                           std::string_view key, RID rid,
                                                           bool* inserted_new) {
  Page* p = bpm_->fetch_page(node_pid);
  if (!p) throw std::runtime_error("BPlusTree::insert_rec: fetch failed");
  PinGuard guard(bpm_, node_pid);  // unpins node_pid on every exit, incl. exceptions
  Node n(p->data());

  if (n.is_leaf()) {
    uint16_t nk = n.num_keys();
    size_t pos = nk;
    for (size_t i = 0; i < nk; ++i) {
      int c = key.compare(n.key_at(i));
      if (c == 0) {  // upsert: overwrite existing key's RID
        n.set_rid_at(i, rid);
        *inserted_new = false;
        guard.mark_dirty();
        return std::nullopt;
      }
      if (c < 0) { pos = i; break; }
    }
    *inserted_new = true;

    if (nk < LEAF_CAP) {  // room: shift and insert in place
      for (size_t i = nk; i > pos; --i) {
        n.set_key_at(i, n.key_at(i - 1));
        n.set_rid_at(i, n.rid_at(i - 1));
      }
      n.set_key_at(pos, key);
      n.set_rid_at(pos, rid);
      n.set_num_keys(nk + 1);
      guard.mark_dirty();
      return std::nullopt;
    }

    // Full leaf: gather all entries + the new one, split in half.
    std::vector<std::pair<std::string, RID>> e;
    e.reserve(nk + 1);
    for (size_t i = 0; i < pos; ++i) e.emplace_back(std::string(n.key_at(i)), n.rid_at(i));
    e.emplace_back(std::string(key), rid);
    for (size_t i = pos; i < nk; ++i) e.emplace_back(std::string(n.key_at(i)), n.rid_at(i));

    size_t mid = e.size() / 2;
    page_id_t right_pid;
    Page* rp = bpm_->new_page(&right_pid);
    if (!rp) throw std::runtime_error("BPlusTree: no frame for leaf split");
    Node rn(rp->data());
    rn.init_leaf();

    n.set_num_keys(static_cast<uint16_t>(mid));
    for (size_t i = 0; i < mid; ++i) { n.set_key_at(i, e[i].first); n.set_rid_at(i, e[i].second); }
    size_t rcount = e.size() - mid;
    rn.set_num_keys(static_cast<uint16_t>(rcount));
    for (size_t i = 0; i < rcount; ++i) { rn.set_key_at(i, e[mid + i].first); rn.set_rid_at(i, e[mid + i].second); }

    rn.set_next_leaf(n.next_leaf());
    n.set_next_leaf(right_pid);
    std::string sep = e[mid].first;

    bpm_->unpin_page(right_pid, true);
    guard.mark_dirty();
    return SplitResult{std::move(sep), right_pid};
  }

  // Internal node: descend, then absorb any split coming back up.  The guard
  // keeps node_pid pinned across the recursion and unpins it on return OR if the
  // recursive call throws.
  size_t ci = child_index_for(n, key);
  page_id_t child = n.child_at(ci);
  auto res = insert_rec(child, key, rid, inserted_new);
  if (!res) {
    return std::nullopt;
  }

  uint16_t nk = n.num_keys();
  if (nk < INT_KEY_CAP) {  // room for the new separator
    for (size_t i = nk; i > ci; --i) n.set_key_at(i, n.key_at(i - 1));
    for (size_t i = nk + 1; i > ci + 1; --i) n.set_child_at(i, n.child_at(i - 1));
    n.set_key_at(ci, res->sep_key);
    n.set_child_at(ci + 1, res->right_page);
    n.set_num_keys(nk + 1);
    guard.mark_dirty();
    return std::nullopt;
  }

  // Full internal: rebuild keys/children with the insertion, split, promote mid.
  std::vector<std::string> keys;
  std::vector<page_id_t> children;
  for (size_t i = 0; i < nk; ++i) keys.emplace_back(n.key_at(i));
  for (size_t i = 0; i <= nk; ++i) children.push_back(n.child_at(i));
  keys.insert(keys.begin() + ci, res->sep_key);
  children.insert(children.begin() + ci + 1, res->right_page);

  size_t mid = keys.size() / 2;
  std::string promote = keys[mid];

  n.set_num_keys(static_cast<uint16_t>(mid));
  for (size_t i = 0; i < mid; ++i) n.set_key_at(i, keys[i]);
  for (size_t i = 0; i <= mid; ++i) n.set_child_at(i, children[i]);

  page_id_t right_pid;
  Page* rp = bpm_->new_page(&right_pid);
  if (!rp) throw std::runtime_error("BPlusTree: no frame for internal split");
  Node rn(rp->data());
  rn.init_internal();
  size_t rkeys = keys.size() - (mid + 1);
  rn.set_num_keys(static_cast<uint16_t>(rkeys));
  for (size_t i = 0; i < rkeys; ++i) rn.set_key_at(i, keys[mid + 1 + i]);
  for (size_t i = 0; i <= rkeys; ++i) rn.set_child_at(i, children[mid + 1 + i]);

  bpm_->unpin_page(right_pid, true);
  guard.mark_dirty();
  return SplitResult{std::move(promote), right_pid};
}

bool BPlusTree::erase(std::string_view key) {
  page_id_t leaf = find_leaf(key);
  Page* p = bpm_->fetch_page(leaf);
  Node n(p->data());
  uint16_t nk = n.num_keys();
  for (size_t i = 0; i < nk; ++i) {
    if (key.compare(n.key_at(i)) == 0) {
      // Lazy delete: shift the tail down by one, no underflow merge.
      for (size_t j = i; j + 1 < nk; ++j) {
        n.set_key_at(j, n.key_at(j + 1));
        n.set_rid_at(j, n.rid_at(j + 1));
      }
      n.set_num_keys(nk - 1);
      bpm_->unpin_page(leaf, true);
      return true;
    }
  }
  bpm_->unpin_page(leaf, false);
  return false;
}

int BPlusTree::height() const {
  int h = 1;
  page_id_t pid = root_page_id();
  while (true) {
    Page* p = bpm_->fetch_page(pid);
    Node n(p->data());
    bool leaf = n.is_leaf();
    page_id_t child = leaf ? INVALID_PAGE_ID : n.child_at(0);
    bpm_->unpin_page(pid, false);
    if (leaf) break;
    ++h;
    pid = child;
  }
  return h;
}

// ----- range scan -----------------------------------------------------------

BPlusTree::Iterator BPlusTree::range(std::string_view lo, std::string_view hi) const {
  page_id_t leaf;
  int idx = 0;
  if (lo.empty()) {
    // Leftmost leaf: follow child 0 down to the bottom.
    page_id_t pid = root_page_id();
    while (true) {
      Page* p = bpm_->fetch_page(pid);
      Node n(p->data());
      bool is_leaf = n.is_leaf();
      page_id_t child = is_leaf ? INVALID_PAGE_ID : n.child_at(0);
      bpm_->unpin_page(pid, false);
      if (is_leaf) break;
      pid = child;
    }
    leaf = pid;
    idx = 0;
  } else {
    leaf = find_leaf(lo);
    Page* p = bpm_->fetch_page(leaf);
    Node n(p->data());
    uint16_t nk = n.num_keys();
    size_t i = 0;
    while (i < nk && n.key_at(i).compare(lo) < 0) ++i;
    idx = static_cast<int>(i);
    bpm_->unpin_page(leaf, false);
  }
  return Iterator(bpm_, leaf, idx, std::string(hi));
}

BPlusTree::Iterator::Iterator(BufferPool* bpm, page_id_t leaf, int idx, std::string hi)
    : bpm_(bpm), page_id_(leaf), idx_(idx), hi_(std::move(hi)) {
  if (leaf == INVALID_PAGE_ID) return;
  page_ = bpm_->fetch_page(leaf);
  load_current();
}

BPlusTree::Iterator::~Iterator() { release(); }

void BPlusTree::Iterator::release() {
  if (page_) {
    bpm_->unpin_page(page_id_, false);
    page_ = nullptr;
  }
  page_id_ = INVALID_PAGE_ID;
}

BPlusTree::Iterator::Iterator(Iterator&& o) noexcept
    : bpm_(o.bpm_), page_id_(o.page_id_), page_(o.page_), idx_(o.idx_),
      hi_(std::move(o.hi_)), key_(std::move(o.key_)), rid_(o.rid_) {
  o.page_ = nullptr;
  o.page_id_ = INVALID_PAGE_ID;
}

BPlusTree::Iterator& BPlusTree::Iterator::operator=(Iterator&& o) noexcept {
  if (this != &o) {
    release();
    bpm_ = o.bpm_;
    page_id_ = o.page_id_;
    page_ = o.page_;
    idx_ = o.idx_;
    hi_ = std::move(o.hi_);
    key_ = std::move(o.key_);
    rid_ = o.rid_;
    o.page_ = nullptr;
    o.page_id_ = INVALID_PAGE_ID;
  }
  return *this;
}

void BPlusTree::Iterator::load_current() {
  while (page_) {
    Node n(page_->data());
    if (idx_ < n.num_keys()) {
      std::string_view k = n.key_at(idx_);
      if (!hi_.empty() && k.compare(hi_) >= 0) {  // hit exclusive upper bound
        release();
        return;
      }
      key_.assign(k.data(), k.size());
      rid_ = n.rid_at(idx_);
      return;
    }
    // Exhausted this leaf: hop to the next via the leaf chain.
    page_id_t nxt = n.next_leaf();
    bpm_->unpin_page(page_id_, false);
    page_ = nullptr;
    if (nxt == INVALID_PAGE_ID) {
      page_id_ = INVALID_PAGE_ID;
      return;
    }
    page_id_ = nxt;
    page_ = bpm_->fetch_page(nxt);
    idx_ = 0;
  }
}

void BPlusTree::Iterator::next() {
  if (!page_) return;
  ++idx_;
  load_current();
}

}  // namespace walterdb
