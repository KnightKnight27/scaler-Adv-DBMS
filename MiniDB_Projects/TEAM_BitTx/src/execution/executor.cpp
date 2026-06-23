#include "execution/executor.h"

namespace minidb {

using namespace std;

void SeqScanExecutor::Init() {
  cursor_ = 0;
  rids_.clear();
  HeapFile* hf = table_->GetHeap();
  for (auto it = hf->Begin(); it != hf->End(); ++it) {
    rids_.push_back(it.GetRid());
  }
}

bool SeqScanExecutor::Next(Tuple* tuple) {
  while (cursor_ < rids_.size()) {
    RecordId rid = rids_[cursor_++];
    if (table_->GetTuple(rid, tuple)) {
      return true;
    }
  }
  return false;
}

const Schema& SeqScanExecutor::GetOutputSchema() const {
  return table_->GetSchema();
}

void FilterExecutor::Init() {
  child_->Init();
}

bool FilterExecutor::Next(Tuple* tuple) {
  while (child_->Next(tuple)) {
    if (pred_(*tuple)) {
      return true;
    }
  }
  return false;
}

const Schema& FilterExecutor::GetOutputSchema() const {
  return child_->GetOutputSchema();
}

void ProjectExecutor::Init() {
  child_->Init();
}

bool ProjectExecutor::Next(Tuple* tuple) {
  Tuple in;
  if (!child_->Next(&in))
    return false;
  vector<Value> out;
  out.reserve(indices_.size());
  for (size_t i : indices_) {
    out.push_back(in.GetValue(i));
  }
  *tuple = Tuple(out);
  return true;
}

const Schema& ProjectExecutor::GetOutputSchema() const {
  (void)indices_;
  return child_->GetOutputSchema();
}

const Schema& InsertExecutor::GetOutputSchema() const {
  static const Schema s;
  return s;
}

bool InsertExecutor::Next(Tuple* tuple) {
  if (produced_)
    return false;
  RecordId rid;
  bool ok = table_->InsertTuple(Tuple(values_), &rid);
  if (ok && catalog_) {
    const auto& schema = table_->GetSchema();
    for (size_t i = 0; i < schema.GetColumnCount(); ++i) {
      if (auto index = catalog_->GetIndex(tableName_, schema.GetColumn(i).GetName())) {
        index->Insert(values_[i], rid);
      }
    }
  }
  produced_ = true;
  if (tuple) {
    vector<Value> out = {Value(ok ? 1 : 0)};
    *tuple = Tuple(out);
  }
  return ok;
}


void IndexScanExecutor::Init() {
  rids_.clear();
  cursor_ = 0;
  if (!table_)
    return;

  BPlusTree* index = nullptr;
  if (catalog_ && !tableName_.empty()) {
    string colName = table_->GetSchema().GetColumn(keyIdx_).GetName();
    index = catalog_->GetIndex(tableName_, colName);
  }

  if (index) {
    index->Get(key_, &rids_);
  } else {
    // Linear probe fallback
    HeapFile* hf = table_->GetHeap();
    for (auto it = hf->Begin(); it != hf->End(); ++it) {
      Tuple t;
      table_->GetTuple(it.GetRid(), &t);
      if (keyIdx_ >= t.GetSize())
        continue;
      if (t.GetValue(keyIdx_) == key_) {
        rids_.push_back(it.GetRid());
      }
    }
  }
}

bool IndexScanExecutor::Next(Tuple* tuple) {
  if (cursor_ >= rids_.size())
    return false;
  if (tuple) {
    table_->GetTuple(rids_[cursor_], tuple);
  }
  cursor_++;
  return true;
}

const Schema& IndexScanExecutor::GetOutputSchema() const {
  return table_->GetSchema();
}

} // namespace minidb