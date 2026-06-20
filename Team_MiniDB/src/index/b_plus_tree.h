#pragma once
#include <optional>
#include <vector>

#include "../storage/buffer_pool.h"
#include "b_plus_tree_page.h"

namespace minidb {

// Disk-backed B+Tree primary index mapping int64 keys -> RID. Each table's
// catalog entry stores the root_page_id; this class is a thin handle over
// the buffer pool that's reconstructed (cheaply) whenever the catalog needs
// to search/insert/delete.
//
// Note (documented limitation): Remove() deletes the key from its leaf but
// does not rebalance/merge underflowed nodes with siblings - acceptable for
// a course project since correctness of search/insert/delete is preserved,
// only space utilization after heavy deletion is suboptimal.
class BPlusTree {
 public:
  BPlusTree(BufferPool *bpm, page_id_t root_page_id) : bpm_(bpm), root_page_id_(root_page_id) {}

  static page_id_t CreateEmpty(BufferPool *bpm) {
    page_id_t root_pid;
    Page *root = bpm->NewPage(&root_pid);
    BTreeLeafPage leaf(root->GetData());
    leaf.Init(INVALID_PAGE_ID);
    bpm->UnpinPage(root_pid, true);
    return root_pid;
  }

  page_id_t RootPageId() const { return root_page_id_; }

  std::optional<RID> Search(Key key) {
    page_id_t leaf_pid = FindLeaf(key);
    Page *page = bpm_->FetchPage(leaf_pid);
    BTreeLeafPage leaf(page->GetData());
    int idx = leaf.LowerBound(key);
    std::optional<RID> result;
    if (idx < leaf.NumKeys() && leaf.Keys()[idx] == key) result = leaf.Values()[idx];
    bpm_->UnpinPage(leaf_pid, false);
    return result;
  }

  void Insert(Key key, RID rid) {
    page_id_t leaf_pid = FindLeaf(key);
    Page *page = bpm_->FetchPage(leaf_pid);
    BTreeLeafPage leaf(page->GetData());
    int idx = leaf.LowerBound(key);
    if (idx < leaf.NumKeys() && leaf.Keys()[idx] == key) {
      leaf.Values()[idx] = rid;  // overwrite existing mapping
      bpm_->UnpinPage(leaf_pid, true);
      return;
    }
    leaf.InsertAt(idx, key, rid);

    bool needs_split = leaf.IsFull();
    bpm_->UnpinPage(leaf_pid, true);
    if (needs_split) SplitLeaf(leaf_pid);
  }

  bool Remove(Key key) {
    page_id_t leaf_pid = FindLeaf(key);
    Page *page = bpm_->FetchPage(leaf_pid);
    BTreeLeafPage leaf(page->GetData());
    int idx = leaf.LowerBound(key);
    bool found = idx < leaf.NumKeys() && leaf.Keys()[idx] == key;
    if (found) leaf.RemoveAt(idx);
    bpm_->UnpinPage(leaf_pid, found);
    return found;
  }

 private:
  page_id_t FindLeaf(Key key) {
    page_id_t pid = root_page_id_;
    while (true) {
      Page *page = bpm_->FetchPage(pid);
      BTreeHeader hdr = *reinterpret_cast<BTreeHeader *>(page->GetData());
      if (hdr.is_leaf) {
        bpm_->UnpinPage(pid, false);
        return pid;
      }
      BTreeInternalPage internal(page->GetData());
      int child_idx = internal.ChildIndexFor(key);
      page_id_t child = internal.Children()[child_idx];
      bpm_->UnpinPage(pid, false);
      pid = child;
    }
  }

  void SplitLeaf(page_id_t leaf_pid) {
    Page *page = bpm_->FetchPage(leaf_pid);
    BTreeLeafPage leaf(page->GetData());

    int n = leaf.NumKeys();
    int mid = n / 2;

    page_id_t new_pid;
    Page *new_page = bpm_->NewPage(&new_pid);
    BTreeLeafPage new_leaf(new_page->GetData());
    new_leaf.Init(leaf.Header().parent_page_id);

    for (int i = mid; i < n; i++) new_leaf.InsertAt(i - mid, leaf.Keys()[i], leaf.Values()[i]);
    leaf.Header().num_keys = mid;

    new_leaf.Header().next_leaf_page_id = leaf.Header().next_leaf_page_id;
    leaf.Header().next_leaf_page_id = new_pid;

    Key separator = new_leaf.Keys()[0];
    page_id_t parent_pid = leaf.Header().parent_page_id;

    bpm_->UnpinPage(new_pid, true);
    bpm_->UnpinPage(leaf_pid, true);

    InsertIntoParent(parent_pid, leaf_pid, separator, new_pid);
  }

  void InsertIntoParent(page_id_t parent_pid, page_id_t left, Key key, page_id_t right) {
    if (parent_pid == INVALID_PAGE_ID) {
      // left was the root - create a new root above it.
      page_id_t new_root_pid;
      Page *new_root_page = bpm_->NewPage(&new_root_pid);
      BTreeInternalPage new_root(new_root_page->GetData());
      new_root.Init(INVALID_PAGE_ID);
      new_root.SetFirstChild(left);
      new_root.InsertAt(0, key, right);
      bpm_->UnpinPage(new_root_pid, true);

      SetParent(left, new_root_pid);
      SetParent(right, new_root_pid);
      root_page_id_ = new_root_pid;
      return;
    }

    Page *parent_page = bpm_->FetchPage(parent_pid);
    BTreeInternalPage parent(parent_page->GetData());
    int idx = parent.ChildIndexFor(key);
    // ChildIndexFor walks past keys < target; the new entry goes right
    // after `left`'s position among existing keys.
    int insert_pos = 0;
    for (int i = 0; i <= parent.NumKeys(); i++) {
      if (parent.Children()[i] == left) {
        insert_pos = i;
        break;
      }
    }
    parent.InsertAt(insert_pos, key, right);
    SetParent(right, parent_pid);

    if (!parent.IsFull()) {
      bpm_->UnpinPage(parent_pid, true);
      return;
    }
    bpm_->UnpinPage(parent_pid, true);
    SplitInternal(parent_pid);
  }

  void SplitInternal(page_id_t pid) {
    Page *page = bpm_->FetchPage(pid);
    BTreeInternalPage node(page->GetData());

    int n = node.NumKeys();
    int mid = n / 2;
    Key up_key = node.Keys()[mid];

    page_id_t new_pid;
    Page *new_page = bpm_->NewPage(&new_pid);
    BTreeInternalPage new_node(new_page->GetData());
    new_node.Init(node.Header().parent_page_id);
    new_node.SetFirstChild(node.Children()[mid + 1]);
    SetParent(node.Children()[mid + 1], new_pid);

    for (int i = mid + 1; i < n; i++) {
      new_node.InsertAt(i - mid - 1, node.Keys()[i], node.Children()[i + 1]);
      SetParent(node.Children()[i + 1], new_pid);
    }
    node.Header().num_keys = mid;

    page_id_t parent_pid = node.Header().parent_page_id;
    bpm_->UnpinPage(new_pid, true);
    bpm_->UnpinPage(pid, true);

    InsertIntoParent(parent_pid, pid, up_key, new_pid);
  }

  void SetParent(page_id_t child_pid, page_id_t parent_pid) {
    Page *page = bpm_->FetchPage(child_pid);
    BTreeHeader *hdr = reinterpret_cast<BTreeHeader *>(page->GetData());
    hdr->parent_page_id = parent_pid;
    bpm_->UnpinPage(child_pid, true);
  }

  BufferPool *bpm_;
  page_id_t root_page_id_;
};

}  // namespace minidb
