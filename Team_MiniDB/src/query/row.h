#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../catalog/value.h"
#include "../storage/rid.h"

namespace minidb {

// A row flowing through the executor: values plus the RID it came from
// (needed by DELETE; empty/invalid for rows produced by a join or
// projection that no longer map to a single physical tuple).
struct Row {
  std::vector<Value> values;
  RID rid;
};

// Maps qualified ("alias.col") and unqualified ("col", when unambiguous)
// names to a position in a Row's values, built once per plan node.
class RowSchema {
 public:
  void AddColumn(const std::string &alias, const std::string &col) {
    qualified_.push_back(alias + "." + col);
    unqualified_.push_back(col);
  }

  size_t Size() const { return qualified_.size(); }
  const std::string &QualifiedName(size_t i) const { return qualified_[i]; }
  const std::string &UnqualifiedName(size_t i) const { return unqualified_[i]; }

  // Resolves a possibly-qualified column reference to an index, or -1.
  int Resolve(const std::string &table, const std::string &col) const {
    if (!table.empty()) {
      for (size_t i = 0; i < qualified_.size(); i++)
        if (qualified_[i] == table + "." + col) return static_cast<int>(i);
      return -1;
    }
    int found = -1;
    for (size_t i = 0; i < unqualified_.size(); i++) {
      if (unqualified_[i] == col) {
        if (found != -1) return -1;  // ambiguous
        found = static_cast<int>(i);
      }
    }
    return found;
  }

  void Append(const RowSchema &other) {
    qualified_.insert(qualified_.end(), other.qualified_.begin(), other.qualified_.end());
    unqualified_.insert(unqualified_.end(), other.unqualified_.begin(), other.unqualified_.end());
  }

 private:
  std::vector<std::string> qualified_;
  std::vector<std::string> unqualified_;
};

}  // namespace minidb
