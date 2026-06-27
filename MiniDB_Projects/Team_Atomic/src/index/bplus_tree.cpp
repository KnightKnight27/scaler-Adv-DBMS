#include "index/bplus_tree.h"
#include <algorithm>
#include <cstring>

namespace minidb {

// ---- low-level byte helpers ----
static int32_t Rd32(const char* p) { int32_t v; std::memcpy(&v, p, 4); return v; }
static int64_t Rd64(const char* p) { int64_t v; std::memcpy(&v, p, 8); return v; }
static void Wr32(char* p, int32_t v) { std::memcpy(p, &v, 4); }
static void Wr64(char* p, int64_t v) { std::memcpy(p, &v, 8); }

page_id_t BPlusTree::Create(BufferPoolManager* bpm) {
  page_id_t header_pid;
  Page* h = bpm->NewPage(&header_pid);
  if (!h) throw DBError("BPlusTree::Create: buffer pool full");
  Wr32(h->Data(), INVALID_PAGE_ID);  // root = empty
  bpm->UnpinPage(header_pid, true);
  return header_pid;
}

page_id_t BPlusTree::GetRoot() {
  Page* h = bpm_->FetchPage(header_page_id_);
  page_id_t root = Rd32(h->Data());
  bpm_->UnpinPage(header_page_id_, false);
  return root;
}

void BPlusTree::SetRoot(page_id_t root) {
  Page* h = bpm_->FetchPage(header_page_id_);
  Wr32(h->Data(), root);
  bpm_->UnpinPage(header_page_id_, true);
}

BPlusTree::NodeType BPlusTree::TypeOf(page_id_t pid) {
  Page* p = bpm_->FetchPage(pid);
  NodeType t = static_cast<NodeType>(Rd32(p->Data()));
  bpm_->UnpinPage(pid, false);
  return t;
}

BPlusTree::LeafNode BPlusTree::ReadLeaf(page_id_t pid) {
  Page* p = bpm_->FetchPage(pid);
  const char* d = p->Data();
  LeafNode n;
  n.self = pid;
  int32_t size = Rd32(d + 4);
  n.next = Rd32(d + 8);
  const char* e = d + 12;
  for (int i = 0; i < size; i++) {
    int64_t key = Rd64(e);
    RID rid{Rd32(e + 8), Rd32(e + 12)};
    n.entries.emplace_back(key, rid);
    e += 16;
  }
  bpm_->UnpinPage(pid, false);
  return n;
}

BPlusTree::InternalNode BPlusTree::ReadInternal(page_id_t pid) {
  Page* p = bpm_->FetchPage(pid);
  const char* d = p->Data();
  InternalNode n;
  n.self = pid;
  int32_t size = Rd32(d + 4);
  const char* childp = d + 8;
  for (int i = 0; i < size + 1; i++) n.children.push_back(Rd32(childp + i * 4));
  const char* keyp = childp + (size + 1) * 4;
  for (int i = 0; i < size; i++) n.keys.push_back(Rd64(keyp + i * 8));
  bpm_->UnpinPage(pid, false);
  return n;
}

void BPlusTree::WriteLeaf(const LeafNode& n) {
  Page* p = bpm_->FetchPage(n.self);
  char* d = p->Data();
  Wr32(d, kLeaf);
  Wr32(d + 4, static_cast<int32_t>(n.entries.size()));
  Wr32(d + 8, n.next);
  char* e = d + 12;
  for (auto& [key, rid] : n.entries) {
    Wr64(e, key);
    Wr32(e + 8, rid.page_id);
    Wr32(e + 12, rid.slot_id);
    e += 16;
  }
  bpm_->UnpinPage(n.self, true);
}

void BPlusTree::WriteInternal(const InternalNode& n) {
  Page* p = bpm_->FetchPage(n.self);
  char* d = p->Data();
  Wr32(d, kInternal);
  int32_t size = static_cast<int32_t>(n.keys.size());
  Wr32(d + 4, size);
  char* childp = d + 8;
  for (int i = 0; i < size + 1; i++) Wr32(childp + i * 4, n.children[i]);
  char* keyp = childp + (size + 1) * 4;
  for (int i = 0; i < size; i++) Wr64(keyp + i * 8, n.keys[i]);
  bpm_->UnpinPage(n.self, true);
}

page_id_t BPlusTree::NewNode(NodeType type, LeafNode* leaf, InternalNode* internal) {
  page_id_t pid;
  Page* p = bpm_->NewPage(&pid);
  if (!p) throw DBError("BPlusTree: buffer pool full");
  Wr32(p->Data(), type);
  Wr32(p->Data() + 4, 0);
  bpm_->UnpinPage(pid, true);
  if (type == kLeaf && leaf) leaf->self = pid;
  if (type == kInternal && internal) internal->self = pid;
  return pid;
}

page_id_t BPlusTree::FindLeaf(int64_t key, std::vector<page_id_t>* path) {
  page_id_t pid = GetRoot();
  while (TypeOf(pid) == kInternal) {
    if (path) path->push_back(pid);
    InternalNode in = ReadInternal(pid);
    // Find first key strictly greater than `key`; descend into that child.
    int idx = static_cast<int>(std::upper_bound(in.keys.begin(), in.keys.end(), key) -
                               in.keys.begin());
    pid = in.children[idx];
  }
  return pid;
}

bool BPlusTree::Search(int64_t key, RID* out) {
  if (IsEmpty()) return false;
  LeafNode leaf = ReadLeaf(FindLeaf(key, nullptr));
  for (auto& [k, rid] : leaf.entries) {
    if (k == key) { *out = rid; return true; }
  }
  return false;
}

void BPlusTree::Insert(int64_t key, const RID& rid) {
  // Empty tree: create the first leaf as root.
  if (IsEmpty()) {
    LeafNode leaf;
    NewNode(kLeaf, &leaf, nullptr);
    leaf.entries.emplace_back(key, rid);
    WriteLeaf(leaf);
    SetRoot(leaf.self);
    return;
  }

  std::vector<page_id_t> path;
  page_id_t leaf_pid = FindLeaf(key, &path);
  LeafNode leaf = ReadLeaf(leaf_pid);

  // Overwrite existing key, or insert in sorted position.
  auto it = std::lower_bound(leaf.entries.begin(), leaf.entries.end(), key,
                             [](const std::pair<int64_t, RID>& p, int64_t k) {
                               return p.first < k;
                             });
  if (it != leaf.entries.end() && it->first == key) {
    it->second = rid;
    WriteLeaf(leaf);
    return;
  }
  leaf.entries.insert(it, {key, rid});

  if (static_cast<int>(leaf.entries.size()) <= LEAF_MAX) {
    WriteLeaf(leaf);
    return;
  }

  // Split the leaf into [0,mid) and [mid,end).
  int mid = static_cast<int>(leaf.entries.size()) / 2;
  LeafNode right;
  NewNode(kLeaf, &right, nullptr);
  right.entries.assign(leaf.entries.begin() + mid, leaf.entries.end());
  leaf.entries.erase(leaf.entries.begin() + mid, leaf.entries.end());
  right.next = leaf.next;
  leaf.next = right.self;
  WriteLeaf(leaf);
  WriteLeaf(right);

  int64_t sep = right.entries.front().first;  // first key of right goes up
  InsertIntoParent(path, sep, leaf.self, right.self);
}

void BPlusTree::InsertIntoParent(std::vector<page_id_t>& path, int64_t sep_key,
                                 page_id_t left, page_id_t right) {
  // No parent: we split the root -> create a new internal root.
  if (path.empty()) {
    InternalNode root;
    NewNode(kInternal, nullptr, &root);
    root.keys.push_back(sep_key);
    root.children.push_back(left);
    root.children.push_back(right);
    WriteInternal(root);
    SetRoot(root.self);
    return;
  }

  page_id_t parent_pid = path.back();
  path.pop_back();
  InternalNode parent = ReadInternal(parent_pid);

  // Insert sep_key with `right` to the right of `left`.
  int pos = static_cast<int>(std::upper_bound(parent.keys.begin(), parent.keys.end(),
                                              sep_key) - parent.keys.begin());
  parent.keys.insert(parent.keys.begin() + pos, sep_key);
  parent.children.insert(parent.children.begin() + pos + 1, right);

  if (static_cast<int>(parent.keys.size()) <= INTERNAL_MAX) {
    WriteInternal(parent);
    return;
  }

  // Split internal node. Middle key moves up (not copied).
  int mid = static_cast<int>(parent.keys.size()) / 2;
  int64_t up_key = parent.keys[mid];
  InternalNode rnode;
  NewNode(kInternal, nullptr, &rnode);
  rnode.keys.assign(parent.keys.begin() + mid + 1, parent.keys.end());
  rnode.children.assign(parent.children.begin() + mid + 1, parent.children.end());
  parent.keys.erase(parent.keys.begin() + mid, parent.keys.end());
  parent.children.erase(parent.children.begin() + mid + 1, parent.children.end());
  WriteInternal(parent);
  WriteInternal(rnode);

  InsertIntoParent(path, up_key, parent.self, rnode.self);
}

bool BPlusTree::Delete(int64_t key) {
  if (IsEmpty()) return false;
  LeafNode leaf = ReadLeaf(FindLeaf(key, nullptr));
  auto it = std::lower_bound(leaf.entries.begin(), leaf.entries.end(), key,
                             [](const std::pair<int64_t, RID>& p, int64_t k) {
                               return p.first < k;
                             });
  if (it == leaf.entries.end() || it->first != key) return false;
  leaf.entries.erase(it);
  WriteLeaf(leaf);  // no merge/redistribute; tree stays correct for lookups
  return true;
}

std::vector<std::pair<int64_t, RID>> BPlusTree::Range(int64_t low, int64_t high) {
  std::vector<std::pair<int64_t, RID>> result;
  if (IsEmpty()) return result;
  page_id_t pid = FindLeaf(low, nullptr);
  while (pid != INVALID_PAGE_ID) {
    LeafNode leaf = ReadLeaf(pid);
    for (auto& [k, rid] : leaf.entries) {
      if (k < low) continue;
      if (k > high) return result;
      result.emplace_back(k, rid);
    }
    pid = leaf.next;
  }
  return result;
}

}  // namespace minidb
