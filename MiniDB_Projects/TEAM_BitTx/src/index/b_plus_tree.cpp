#include "index/b_plus_tree.h"

#include "storage/disk_manager.h"
#include "storage/page.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace minidb {

using namespace std;

BPlusTree::BPlusTree(const string& filename) : filename_(filename), rootPageId_(-1) {
  string meta = filename + ".meta";
  FILE* f = fopen(meta.c_str(), "rb");
  if (f) {
    fread(&rootPageId_, sizeof(int32_t), 1, f);
    fclose(f);
  }
}

BPlusTree::~BPlusTree() {
  string meta = filename_ + ".meta";
  FILE* f = fopen(meta.c_str(), "wb");
  if (f) {
    fwrite(&rootPageId_, sizeof(int32_t), 1, f);
    fclose(f);
  }
}

int32_t BPlusTree::AllocatePage() {
  DiskManager dm(filename_);
  return dm.AllocatePage();
}

bool BPlusTree::WriteNode(int32_t pageId, const Node& node) const {
  DiskManager dm(filename_);
  char buf[PAGE_SIZE];
  memset(buf, 0, PAGE_SIZE);

  char* p = buf;
  memcpy(p, &node.isLeaf, sizeof(bool)); p += sizeof(bool);
  memcpy(p, &node.parentPageId, sizeof(int32_t)); p += sizeof(int32_t);
  memcpy(p, &node.nextLeaf, sizeof(int32_t)); p += sizeof(int32_t);
  int32_t numKeys = static_cast<int32_t>(node.keys.size());
  memcpy(p, &numKeys, sizeof(int32_t)); p += sizeof(int32_t);

  for (const auto& k : node.keys) {
    uint8_t type = static_cast<uint8_t>(k.GetTypeId());
    memcpy(p, &type, sizeof(uint8_t)); p += sizeof(uint8_t);
    int32_t size = static_cast<int32_t>(k.SerializedSize());
    memcpy(p, &size, sizeof(int32_t)); p += sizeof(int32_t);
    k.SerializeTo(p);
    p += size;
  }

  if (node.isLeaf) {
    for (const auto& rid : node.rids) {
      int32_t pid = rid.GetPageId();
      int32_t slot = rid.GetSlotNum();
      memcpy(p, &pid, sizeof(int32_t)); p += sizeof(int32_t);
      memcpy(p, &slot, sizeof(int32_t)); p += sizeof(int32_t);
    }
  } else {
    int32_t numChildren = static_cast<int32_t>(node.childPages.size());
    memcpy(p, &numChildren, sizeof(int32_t)); p += sizeof(int32_t);
    for (int32_t child : node.childPages) {
      memcpy(p, &child, sizeof(int32_t)); p += sizeof(int32_t);
    }
  }

  dm.WritePage(pageId, buf);
  return true;
}

bool BPlusTree::ReadNode(int32_t pageId, Node* node) const {
  DiskManager dm(filename_);
  char buf[PAGE_SIZE];
  dm.ReadPage(pageId, buf);

  const char* p = buf;
  memcpy(&node->isLeaf, p, sizeof(bool)); p += sizeof(bool);
  memcpy(&node->parentPageId, p, sizeof(int32_t)); p += sizeof(int32_t);
  memcpy(&node->nextLeaf, p, sizeof(int32_t)); p += sizeof(int32_t);
  int32_t numKeys;
  memcpy(&numKeys, p, sizeof(int32_t)); p += sizeof(int32_t);

  node->keys.clear();
  node->keys.reserve(numKeys);
  for (int32_t i = 0; i < numKeys; ++i) {
    uint8_t type;
    memcpy(&type, p, sizeof(uint8_t)); p += sizeof(uint8_t);
    int32_t size;
    memcpy(&size, p, sizeof(int32_t)); p += sizeof(int32_t);
    Value k = Value::DeserializeFrom(p, static_cast<TypeId>(type));
    node->keys.push_back(k);
    p += size;
  }

  node->rids.clear();
  node->childPages.clear();
  if (node->isLeaf) {
    node->rids.reserve(numKeys);
    for (int32_t i = 0; i < numKeys; ++i) {
      int32_t pid, slot;
      memcpy(&pid, p, sizeof(int32_t)); p += sizeof(int32_t);
      memcpy(&slot, p, sizeof(int32_t)); p += sizeof(int32_t);
      node->rids.push_back(RecordId(pid, slot));
    }
  } else {
    int32_t numChildren = 0;
    memcpy(&numChildren, p, sizeof(int32_t)); p += sizeof(int32_t);
    node->childPages.reserve(numChildren);
    for (int32_t i = 0; i < numChildren; ++i) {
      int32_t child;
      memcpy(&child, p, sizeof(int32_t)); p += sizeof(int32_t);
      node->childPages.push_back(child);
    }
  }
  return true;
}

int32_t BPlusTree::FindLeafPage(int32_t pageId, const Value& key) const {
  Node node;
  ReadNode(pageId, &node);
  if (node.isLeaf) {
    return pageId;
  }
  int32_t slot = 0;
  while (slot < static_cast<int32_t>(node.keys.size()) && key >= node.keys[slot]) {
    slot++;
  }
  return FindLeafPage(node.childPages[slot], key);
}

bool BPlusTree::Get(const Value& key, vector<RecordId>* rids) {
  if (rootPageId_ == -1) {
    return false;
  }
  int32_t leafPageId = FindLeafPage(rootPageId_, key);
  Node leaf;
  ReadNode(leafPageId, &leaf);
  bool found = false;
  for (size_t i = 0; i < leaf.keys.size(); ++i) {
    if (leaf.keys[i] == key) {
      rids->push_back(leaf.rids[i]);
      found = true;
    }
  }
  return found;
}

bool BPlusTree::Insert(const Value& key, const RecordId& rid) {
  if (rootPageId_ == -1) {
    rootPageId_ = AllocatePage();
    Node root;
    root.isLeaf = true;
    root.keys.push_back(key);
    root.rids.push_back(rid);
    WriteNode(rootPageId_, root);
    return true;
  }

  int32_t leafPageId = FindLeafPage(rootPageId_, key);
  Node leaf;
  ReadNode(leafPageId, &leaf);

  auto it = lower_bound(leaf.keys.begin(), leaf.keys.end(), key);
  size_t idx = distance(leaf.keys.begin(), it);
  leaf.keys.insert(it, key);
  leaf.rids.insert(leaf.rids.begin() + idx, rid);

  constexpr size_t MAX_KEYS = 4;
  if (leaf.keys.size() > MAX_KEYS) {
    int32_t newLeafPageId = AllocatePage();
    Node newLeaf;
    newLeaf.isLeaf = true;
    newLeaf.parentPageId = leaf.parentPageId;
    newLeaf.nextLeaf = leaf.nextLeaf;
    leaf.nextLeaf = newLeafPageId;

    size_t splitIdx = leaf.keys.size() / 2;
    newLeaf.keys.assign(leaf.keys.begin() + splitIdx, leaf.keys.end());
    newLeaf.rids.assign(leaf.rids.begin() + splitIdx, leaf.rids.end());

    leaf.keys.erase(leaf.keys.begin() + splitIdx, leaf.keys.end());
    leaf.rids.erase(leaf.rids.begin() + splitIdx, leaf.rids.end());

    WriteNode(leafPageId, leaf);
    WriteNode(newLeafPageId, newLeaf);

    InsertIntoParent(leafPageId, newLeaf.keys[0], newLeafPageId);
  } else {
    WriteNode(leafPageId, leaf);
  }
  return true;
}

void BPlusTree::InsertIntoParent(int32_t leftPageId, const Value& key, int32_t rightPageId) {
  Node left;
  ReadNode(leftPageId, &left);

  if (leftPageId == rootPageId_) {
    int32_t newRootPageId = AllocatePage();
    Node newRoot;
    newRoot.isLeaf = false;
    newRoot.keys.push_back(key);
    newRoot.childPages.push_back(leftPageId);
    newRoot.childPages.push_back(rightPageId);

    left.parentPageId = newRootPageId;
    Node right;
    ReadNode(rightPageId, &right);
    right.parentPageId = newRootPageId;

    WriteNode(leftPageId, left);
    WriteNode(rightPageId, right);
    WriteNode(newRootPageId, newRoot);
    rootPageId_ = newRootPageId;
    return;
  }

  int32_t parentPageId = left.parentPageId;
  Node parent;
  ReadNode(parentPageId, &parent);

  auto it = lower_bound(parent.keys.begin(), parent.keys.end(), key);
  size_t idx = distance(parent.keys.begin(), it);
  parent.keys.insert(it, key);
  parent.childPages.insert(parent.childPages.begin() + idx + 1, rightPageId);

  Node right;
  ReadNode(rightPageId, &right);
  right.parentPageId = parentPageId;
  WriteNode(rightPageId, right);

  constexpr size_t MAX_KEYS = 4;
  if (parent.keys.size() > MAX_KEYS) {
    int32_t newParentPageId = AllocatePage();
    Node newParent;
    newParent.isLeaf = false;
    newParent.parentPageId = parent.parentPageId;

    size_t splitIdx = parent.keys.size() / 2;
    Value pushUpKey = parent.keys[splitIdx];

    newParent.keys.assign(parent.keys.begin() + splitIdx + 1, parent.keys.end());
    newParent.childPages.assign(parent.childPages.begin() + splitIdx + 1, parent.childPages.end());

    for (int32_t childId : newParent.childPages) {
      Node childNode;
      ReadNode(childId, &childNode);
      childNode.parentPageId = newParentPageId;
      WriteNode(childId, childNode);
    }

    parent.keys.erase(parent.keys.begin() + splitIdx, parent.keys.end());
    parent.childPages.erase(parent.childPages.begin() + splitIdx + 1, parent.childPages.end());

    WriteNode(parentPageId, parent);
    WriteNode(newParentPageId, newParent);

    InsertIntoParent(parentPageId, pushUpKey, newParentPageId);
  } else {
    WriteNode(parentPageId, parent);
  }
}

bool BPlusTree::Remove(const Value& key) {
  if (rootPageId_ == -1) {
    return false;
  }
  int32_t leafPageId = FindLeafPage(rootPageId_, key);
  Node leaf;
  ReadNode(leafPageId, &leaf);

  bool deleted = false;
  for (auto it = leaf.keys.begin(); it != leaf.keys.end(); ) {
    if (*it == key) {
      size_t idx = distance(leaf.keys.begin(), it);
      it = leaf.keys.erase(it);
      leaf.rids.erase(leaf.rids.begin() + idx);
      deleted = true;
    } else {
      ++it;
    }
  }

  if (deleted) {
    WriteNode(leafPageId, leaf);
  }
  return deleted;
}

} // namespace minidb