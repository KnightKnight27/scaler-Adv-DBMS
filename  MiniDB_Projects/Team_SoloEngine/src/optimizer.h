#pragma once

#include "execution.h"

#include <memory>
#include <string>
#include <unordered_map>

// ─── Query representation ─────────────────────────────────────────────────────

enum class FilterField { NONE, ID, VAL1, VAL2 };

struct DummyAST {
    enum class Type { SCAN, NESTED_LOOP_JOIN, DELETE };

    Type        type{Type::SCAN};
    std::string table_name;                    // for SCAN nodes
    FilterField filter_field{FilterField::NONE};
    int64_t     filter_value{0};

    std::unique_ptr<DummyAST> left;            // for NESTED_LOOP_JOIN
    std::unique_ptr<DummyAST> right;
};

// ─── Table catalog entry ──────────────────────────────────────────────────────

struct TableInfo {
    TableHeap *heap;
    BPlusTree *index;   // may be nullptr if no index
};

// ─── Rule-based optimizer ─────────────────────────────────────────────────────
// RULE: SCAN with FilterField::ID  → IndexScanExecutor
//       SCAN with any other filter → SeqScanExecutor

class Optimizer {
public:
    void RegisterTable(const std::string &name, TableHeap *heap, BPlusTree *index);
    std::unique_ptr<AbstractExecutor> Optimize(const DummyAST &ast) const;

private:
    std::unordered_map<std::string, TableInfo> catalog_;
};
