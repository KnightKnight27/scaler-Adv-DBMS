#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// executor.h  –  executes a parsed statement against storage + index
// ─────────────────────────────────────────────────────────────────────────────
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include "query/parser.h"
#include <vector>
#include <string>

namespace minidb {

struct ExecResult {
    bool                success = false;
    std::string         message;
    std::vector<Record> rows;    // populated for SELECT / SHOW
    std::vector<std::pair<Record, Record>> joined_rows; // populated for SELECT_JOIN
};

class Executor {
public:
    Executor(HeapFile* heap, BPlusTree* tree) : heap_(heap), tree_(tree) {}

    ExecResult Execute(const Statement& stmt, const std::string& plan);

private:
    HeapFile*  heap_;
    BPlusTree* tree_;
};

} // namespace minidb
