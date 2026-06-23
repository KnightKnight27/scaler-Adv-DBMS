#pragma once
#include <cstdint>
#include <cstring>

#include "../storage/page.h"
#include "../storage/rid.h"

namespace minidb {

// Small fanout on purpose (rather than packing a full 4KB page) so that
// inserting a handful of rows already triggers splits and produces a
// multi-level tree - useful for demonstrating B+Tree behavior in the viva.
constexpr int BTREE_LEAF_MAX = 4;
constexpr int BTREE_INTERNAL_MAX = 4;

using Key = int64_t;

// Shared header for both leaf and internal nodes, laid out at the start of
// the raw page buffer.
struct BTreeHeader {
  int32_t is_leaf;
  int32_t num_keys;
  page_id_t parent_page_id;
  page_id_t next_leaf_page_id;  // leaf-chain pointer; unused by internal nodes
};

// View over a leaf page: keys[] each paired with an RID pointing into the
// table's heap file.
class BTreeLeafPage {
 public:
  explicit BTreeLeafPage(char *raw) : raw_(raw) {}

  BTreeHeader &Header() { return *reinterpret_cast<BTreeHeader *>(raw_); }
  void Init(page_id_t parent) {
    Header() = {1, 0, parent, INVALID_PAGE_ID};
  }

  Key *Keys() { return reinterpret_cast<Key *>(raw_ + sizeof(BTreeHeader)); }
  RID *Values() {
    return reinterpret_cast<RID *>(raw_ + sizeof(BTreeHeader) + sizeof(Key) * BTREE_LEAF_MAX);
  }

  int NumKeys() { return Header().num_keys; }
  bool IsFull() { return NumKeys() >= BTREE_LEAF_MAX; }

  // Returns index of first key >= target (lower bound).
  int LowerBound(Key target) {
    int n = NumKeys();
    int lo = 0, hi = n;
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if (Keys()[mid] < target) lo = mid + 1;
      else hi = mid;
    }
    return lo;
  }

  void InsertAt(int idx, Key key, RID rid) {
    int n = NumKeys();
    for (int i = n; i > idx; i--) {
      Keys()[i] = Keys()[i - 1];
      Values()[i] = Values()[i - 1];
    }
    Keys()[idx] = key;
    Values()[idx] = rid;
    Header().num_keys++;
  }

  void RemoveAt(int idx) {
    int n = NumKeys();
    for (int i = idx; i < n - 1; i++) {
      Keys()[i] = Keys()[i + 1];
      Values()[i] = Values()[i + 1];
    }
    Header().num_keys--;
  }

 private:
  char *raw_;
};

// View over an internal page: child[0], (key[0], child[1]), (key[1], child[2]), ...
class BTreeInternalPage {
 public:
  explicit BTreeInternalPage(char *raw) : raw_(raw) {}

  BTreeHeader &Header() { return *reinterpret_cast<BTreeHeader *>(raw_); }
  void Init(page_id_t parent) {
    Header() = {0, 0, parent, INVALID_PAGE_ID};
  }

  Key *Keys() { return reinterpret_cast<Key *>(raw_ + sizeof(BTreeHeader)); }
  page_id_t *Children() {
    return reinterpret_cast<page_id_t *>(raw_ + sizeof(BTreeHeader) + sizeof(Key) * BTREE_INTERNAL_MAX);
  }

  int NumKeys() { return Header().num_keys; }
  bool IsFull() { return NumKeys() >= BTREE_INTERNAL_MAX; }

  // Index of the child to descend into for `target`.
  int ChildIndexFor(Key target) {
    int n = NumKeys();
    int i = 0;
    while (i < n && target >= Keys()[i]) i++;
    return i;
  }

  void InsertAt(int idx, Key key, page_id_t right_child) {
    int n = NumKeys();
    for (int i = n; i > idx; i--) Keys()[i] = Keys()[i - 1];
    for (int i = n + 1; i > idx + 1; i--) Children()[i] = Children()[i - 1];
    Keys()[idx] = key;
    Children()[idx + 1] = right_child;
    Header().num_keys++;
  }

  void SetFirstChild(page_id_t pid) { Children()[0] = pid; }

 private:
  char *raw_;
};

}  // namespace minidb
