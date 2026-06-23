#pragma once

#include "common/rid.h"
#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace minidb {

using namespace std;

class BPlusTree {
public:
  explicit BPlusTree(const string& filename);
  ~BPlusTree();

  bool Insert(const Value& key, const RecordId& rid);
  bool Remove(const Value& key);
  bool Get(const Value& key, vector<RecordId>* rids);

private:
  int32_t AllocatePage();

  struct Node {
    bool isLeaf = true;
    int32_t parentPageId = -1;
    int32_t nextLeaf = -1;
    vector<Value> keys;
    vector<RecordId> rids;       // leaves only
    vector<int32_t> childPages;  // internal nodes only
  };

  bool ReadNode(int32_t pageId, Node* node) const;
  bool WriteNode(int32_t pageId, const Node& node) const;

  int32_t FindLeafPage(int32_t pageId, const Value& key) const;
  void InsertIntoParent(int32_t leftPageId, const Value& key, int32_t rightPageId);

  string filename_;
  int32_t rootPageId_;
};

} // namespace minidb