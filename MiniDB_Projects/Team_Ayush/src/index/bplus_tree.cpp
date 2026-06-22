#include "index/bplus_tree.h"

#include <cstring>

namespace minidb {

// ---- Node page layout ------------------------------------------------------
//   off 0  : int8  is_leaf (1 = leaf, 0 = internal)
//   off 2  : int16 count   (number of keys held)
//   off 4  : int32 next    (leaf: next leaf page id; unused for internal)
//   off 8  : keys[]   (int32 each), key i at 8 + 4*i
//   leaf     : rids[] after the key region; rid i = (page:int32, slot:int16)
//   internal : children[] (int32 each) after the key region; child i
//
// Capacities are generous relative to reserved space; we split when a node
// exceeds CAP keys.
namespace {
const int kHdr      = 8;
const int kLeafCap  = 200;   // max keys in a leaf before splitting
const int kIntCap   = 200;   // max keys in an internal node before splitting
const int kKeyOff   = kHdr;  // keys start here in every node
// Region after the (reserved) key array. Reserve CAP+1 keys so an overflowing
// node still fits before we split it.
const int kLeafValOff = kHdr + 4 * (kLeafCap + 1);   // rids region
const int kIntChildOff = kHdr + 4 * (kIntCap + 1);   // children region

int8_t  IsLeaf(const char* p)        { return p[0]; }
void    SetLeaf(char* p, int8_t v)   { p[0] = static_cast<char>(v); }
int16_t Count(const char* p)         { int16_t v; std::memcpy(&v, p + 2, 2); return v; }
void    SetCount(char* p, int16_t v) { std::memcpy(p + 2, &v, 2); }
int32_t Next(const char* p)          { int32_t v; std::memcpy(&v, p + 4, 4); return v; }
void    SetNext(char* p, int32_t v)  { std::memcpy(p + 4, &v, 4); }

int32_t KeyAt(const char* p, int i)  { int32_t v; std::memcpy(&v, p + kKeyOff + 4 * i, 4); return v; }
void    SetKey(char* p, int i, int32_t v) { std::memcpy(p + kKeyOff + 4 * i, &v, 4); }

RID  RidAt(const char* p, int i) {
  RID r;
  std::memcpy(&r.page, p + kLeafValOff + 6 * i, 4);
  std::memcpy(&r.slot, p + kLeafValOff + 6 * i + 4, 2);
  return r;
}
void SetRid(char* p, int i, RID r) {
  std::memcpy(p + kLeafValOff + 6 * i, &r.page, 4);
  std::memcpy(p + kLeafValOff + 6 * i + 4, &r.slot, 2);
}

PageId ChildAt(const char* p, int i) { int32_t v; std::memcpy(&v, p + kIntChildOff + 4 * i, 4); return v; }
void   SetChild(char* p, int i, PageId v) { std::memcpy(p + kIntChildOff + 4 * i, &v, 4); }
}  // namespace

PageId BPlusTree::CreateNew(BufferPool* bp) {
  // Empty leaf root.
  PageId root;
  Frame* rf = bp->NewPage(&root);
  std::memset(rf->data, 0, PAGE_SIZE);
  SetLeaf(rf->data, 1);
  SetCount(rf->data, 0);
  SetNext(rf->data, INVALID_PAGE_ID);
  bp->UnpinPage(root, true);

  // Header page records the root id.
  PageId header;
  Frame* hf = bp->NewPage(&header);
  std::memset(hf->data, 0, PAGE_SIZE);
  std::memcpy(hf->data, &root, 4);
  bp->UnpinPage(header, true);
  return header;
}

PageId BPlusTree::Root() {
  Frame* f = bp_->FetchPage(header_page_);
  PageId r; std::memcpy(&r, f->data, 4);
  bp_->UnpinPage(header_page_, false);
  return r;
}

void BPlusTree::SetRoot(PageId r) {
  Frame* f = bp_->FetchPage(header_page_);
  std::memcpy(f->data, &r, 4);
  bp_->UnpinPage(header_page_, true);
}

bool BPlusTree::Search(int32_t key, RID* out) {
  PageId pid = Root();
  while (true) {
    Frame* f = bp_->FetchPage(pid);
    char* p = f->data;
    if (IsLeaf(p)) {
      int n = Count(p);
      bool found = false;
      for (int i = 0; i < n; ++i) {
        if (KeyAt(p, i) == key) {
          if (out) *out = RidAt(p, i);
          found = true;
          break;
        }
        if (KeyAt(p, i) > key) break;  // keys sorted
      }
      bp_->UnpinPage(pid, false);
      return found;
    }
    // internal: choose child
    int n = Count(p);
    int i = 0;
    while (i < n && key >= KeyAt(p, i)) ++i;
    PageId child = ChildAt(p, i);
    bp_->UnpinPage(pid, false);
    pid = child;
  }
}

BPlusTree::InsertResult BPlusTree::InsertRec(PageId pid, int32_t key, RID value) {
  Frame* f = bp_->FetchPage(pid);
  char* p = f->data;

  if (IsLeaf(p)) {
    int n = Count(p);
    // Locate insertion position; overwrite if key already present.
    int pos = 0;
    while (pos < n && KeyAt(p, pos) < key) ++pos;
    if (pos < n && KeyAt(p, pos) == key) {
      SetRid(p, pos, value);
      bp_->UnpinPage(pid, true);
      return {false, 0, INVALID_PAGE_ID};
    }
    // Shift right to make room.
    for (int j = n; j > pos; --j) {
      SetKey(p, j, KeyAt(p, j - 1));
      SetRid(p, j, RidAt(p, j - 1));
    }
    SetKey(p, pos, key);
    SetRid(p, pos, value);
    SetCount(p, static_cast<int16_t>(n + 1));
    n += 1;

    if (n <= kLeafCap) {
      bp_->UnpinPage(pid, true);
      return {false, 0, INVALID_PAGE_ID};
    }

    // Split leaf: right gets [mid, n).
    int mid = n / 2;
    PageId rpid;
    Frame* rf = bp_->NewPage(&rpid);
    char* rp = rf->data;
    std::memset(rp, 0, PAGE_SIZE);
    SetLeaf(rp, 1);
    int rcount = n - mid;
    for (int j = 0; j < rcount; ++j) {
      SetKey(rp, j, KeyAt(p, mid + j));
      SetRid(rp, j, RidAt(p, mid + j));
    }
    SetCount(rp, static_cast<int16_t>(rcount));
    SetCount(p, static_cast<int16_t>(mid));
    SetNext(rp, Next(p));
    SetNext(p, rpid);
    int32_t up = KeyAt(rp, 0);
    bp_->UnpinPage(rpid, true);
    bp_->UnpinPage(pid, true);
    return {true, up, rpid};
  }

  // Internal node: find child, recurse, then maybe absorb a promoted key.
  int n = Count(p);
  int ci = 0;
  while (ci < n && key >= KeyAt(p, ci)) ++ci;
  PageId child = ChildAt(p, ci);
  bp_->UnpinPage(pid, false);  // release during recursion

  InsertResult r = InsertRec(child, key, value);
  if (!r.split) return {false, 0, INVALID_PAGE_ID};

  // Re-fetch and insert separator r.up_key with child r.right at position ci.
  f = bp_->FetchPage(pid);
  p = f->data;
  n = Count(p);
  for (int j = n; j > ci; --j) SetKey(p, j, KeyAt(p, j - 1));
  for (int j = n + 1; j > ci + 1; --j) SetChild(p, j, ChildAt(p, j - 1));
  SetKey(p, ci, r.up_key);
  SetChild(p, ci + 1, r.right);
  SetCount(p, static_cast<int16_t>(n + 1));
  n += 1;

  if (n <= kIntCap) {
    bp_->UnpinPage(pid, true);
    return {false, 0, INVALID_PAGE_ID};
  }

  // Split internal: promote middle key (moved up, not copied).
  int mid = n / 2;
  int32_t up = KeyAt(p, mid);
  PageId rpid;
  Frame* rf = bp_->NewPage(&rpid);
  char* rp = rf->data;
  std::memset(rp, 0, PAGE_SIZE);
  SetLeaf(rp, 0);
  int rcount = n - mid - 1;
  for (int j = 0; j < rcount; ++j) SetKey(rp, j, KeyAt(p, mid + 1 + j));
  for (int j = 0; j <= rcount; ++j) SetChild(rp, j, ChildAt(p, mid + 1 + j));
  SetCount(rp, static_cast<int16_t>(rcount));
  SetCount(p, static_cast<int16_t>(mid));
  bp_->UnpinPage(rpid, true);
  bp_->UnpinPage(pid, true);
  return {true, up, rpid};
}

void BPlusTree::Insert(int32_t key, RID value) {
  PageId root = Root();
  InsertResult r = InsertRec(root, key, value);
  if (!r.split) return;

  // Root split: build a new internal root.
  PageId nr;
  Frame* f = bp_->NewPage(&nr);
  char* p = f->data;
  std::memset(p, 0, PAGE_SIZE);
  SetLeaf(p, 0);
  SetCount(p, 1);
  SetKey(p, 0, r.up_key);
  SetChild(p, 0, root);
  SetChild(p, 1, r.right);
  bp_->UnpinPage(nr, true);
  SetRoot(nr);
}

bool BPlusTree::Delete(int32_t key) {
  // Descend to the leaf holding key, then remove in place (lazy, no merge).
  PageId pid = Root();
  while (true) {
    Frame* f = bp_->FetchPage(pid);
    char* p = f->data;
    if (IsLeaf(p)) {
      int n = Count(p);
      int pos = -1;
      for (int i = 0; i < n; ++i) {
        if (KeyAt(p, i) == key) { pos = i; break; }
        if (KeyAt(p, i) > key) break;
      }
      if (pos < 0) { bp_->UnpinPage(pid, false); return false; }
      for (int j = pos; j < n - 1; ++j) {
        SetKey(p, j, KeyAt(p, j + 1));
        SetRid(p, j, RidAt(p, j + 1));
      }
      SetCount(p, static_cast<int16_t>(n - 1));
      bp_->UnpinPage(pid, true);
      return true;
    }
    int n = Count(p);
    int i = 0;
    while (i < n && key >= KeyAt(p, i)) ++i;
    PageId child = ChildAt(p, i);
    bp_->UnpinPage(pid, false);
    pid = child;
  }
}

PageId BPlusTree::LeftmostLeaf() {
  PageId pid = Root();
  while (true) {
    Frame* f = bp_->FetchPage(pid);
    char* p = f->data;
    if (IsLeaf(p)) { bp_->UnpinPage(pid, false); return pid; }
    PageId child = ChildAt(p, 0);
    bp_->UnpinPage(pid, false);
    pid = child;
  }
}

void BPlusTree::Range(int32_t low, int32_t high,
                      const std::function<void(int32_t, RID)>& visitor) {
  PageId pid = LeftmostLeaf();
  while (pid != INVALID_PAGE_ID) {
    Frame* f = bp_->FetchPage(pid);
    char* p = f->data;
    int n = Count(p);
    PageId next = Next(p);
    for (int i = 0; i < n; ++i) {
      int32_t k = KeyAt(p, i);
      if (k < low) continue;
      if (k > high) { bp_->UnpinPage(pid, false); return; }
      visitor(k, RidAt(p, i));
    }
    bp_->UnpinPage(pid, false);
    pid = next;
  }
}

void BPlusTree::ScanAll(const std::function<void(int32_t, RID)>& visitor) {
  Range(INT32_MIN, INT32_MAX, visitor);
}

}  // namespace minidb
