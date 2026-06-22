#include "optimizer.h"

#include <cmath>
#include <stdexcept>

void Optimizer::RegisterTable(const std::string &name, TableHeap *heap, BPlusTree *index) {
    catalog_[name] = {heap, index};
}

// ─── selectivity ─────────────────────────────────────────────────────────────
// Fraction of rows expected to pass an equality filter:
//   ID  → 1/100  (high cardinality primary key, very selective)
//   VAL1 / VAL2 → 1/10  (lower cardinality, less selective)
//   NONE → 1.0  (full scan, no filter)

static double selectivity(FilterField f) {
    switch (f) {
        case FilterField::ID:   return 1.0 / 100.0;
        case FilterField::VAL1: return 1.0 / 10.0;
        case FilterField::VAL2: return 1.0 / 10.0;
        default:                return 1.0;
    }
}

std::unique_ptr<AbstractExecutor> Optimizer::Optimize(const DummyAST &ast) const {
    switch (ast.type) {

    // ── SCAN ─────────────────────────────────────────────────────────────────
    // Cost model:
    //   seq_cost   = num_tuples                            (scan every slot)
    //   index_cost = log2(num_tuples+1) + sel * num_tuples (tree + result output)
    // IndexScan is chosen when it is cheaper AND an index on the filter key exists.
    case DummyAST::Type::SCAN: {
        auto it = catalog_.find(ast.table_name);
        if (it == catalog_.end())
            throw std::runtime_error("Optimizer: unknown table '" + ast.table_name + "'");

        TableHeap *heap  = it->second.heap;
        BPlusTree *index = it->second.index;
        double     n     = static_cast<double>(heap->GetNumTuples());
        double     sel   = selectivity(ast.filter_field);

        // IndexScan is only possible for an ID equality filter (the index is on id).
        if (ast.filter_field == FilterField::ID && index != nullptr) {
            double seq_cost   = n;
            double index_cost = std::log2(n + 1.0) + sel * n;
            if (index_cost <= seq_cost)
                return std::make_unique<IndexScanExecutor>(heap, index, ast.filter_value);
        }

        // Build the SeqScan predicate for whichever filter field was specified.
        std::function<bool(const Tuple &)> pred;
        if (ast.filter_field == FilterField::VAL1) {
            int64_t fv = ast.filter_value;
            pred = [fv](const Tuple &t) { return t.val1 == fv; };
        } else if (ast.filter_field == FilterField::VAL2) {
            int64_t fv = ast.filter_value;
            pred = [fv](const Tuple &t) { return t.val2 == fv; };
        } else if (ast.filter_field == FilterField::ID) {
            // ID filter but index unavailable — fall back to SeqScan with predicate.
            int64_t fv = ast.filter_value;
            pred = [fv](const Tuple &t) { return t.id == fv; };
        }
        // FilterField::NONE → pred stays empty → SeqScan returns every live tuple.

        return std::make_unique<SeqScanExecutor>(heap, pred);
    }

    // ── NESTED_LOOP_JOIN ─────────────────────────────────────────────────────
    // Join-order optimisation: when both children are table scans, put the
    // smaller table on the left (outer loop) to minimise the number of inner
    // loop re-initialisations.
    //
    // Cost(NLJ) ≈ |outer| × cost(inner scan)
    // Putting the smaller table on the left means fewer Init() calls on the
    // (potentially larger) inner side.
    case DummyAST::Type::NESTED_LOOP_JOIN: {
        if (!ast.left || !ast.right)
            throw std::runtime_error("Optimizer: NLJ requires both left and right children");

        const DummyAST *left_ast  = ast.left.get();
        const DummyAST *right_ast = ast.right.get();

        // Only reorder when both sides are plain table scans (base case).
        if (left_ast->type  == DummyAST::Type::SCAN &&
            right_ast->type == DummyAST::Type::SCAN) {
            auto lit = catalog_.find(left_ast->table_name);
            auto rit = catalog_.find(right_ast->table_name);
            if (lit != catalog_.end() && rit != catalog_.end()) {
                int32_t left_n  = lit->second.heap->GetNumTuples();
                int32_t right_n = rit->second.heap->GetNumTuples();
                if (right_n < left_n)
                    std::swap(left_ast, right_ast);  // smaller table → outer loop
            }
        }

        auto left_exec  = Optimize(*left_ast);
        auto right_exec = Optimize(*right_ast);

        // Default join predicate: equi-join on the primary key.
        auto pred = [](const Tuple &l, const Tuple &r) { return l.id == r.id; };

        return std::make_unique<NestedLoopJoinExecutor>(
            std::move(left_exec), std::move(right_exec), pred);
    }

    // ── DELETE ───────────────────────────────────────────────────────────────
    // Build a scan child that identifies the rows to delete, then wrap it in
    // DeleteExecutor which soft-deletes each row in the heap and removes it
    // from the B+ Tree index.
    case DummyAST::Type::DELETE: {
        auto it = catalog_.find(ast.table_name);
        if (it == catalog_.end())
            throw std::runtime_error("Optimizer: unknown table '" + ast.table_name + "'");

        TableHeap *heap  = it->second.heap;
        BPlusTree *index = it->second.index;

        // Reuse the SCAN path to build the child executor.
        DummyAST scan_ast;
        scan_ast.type         = DummyAST::Type::SCAN;
        scan_ast.table_name   = ast.table_name;
        scan_ast.filter_field = ast.filter_field;
        scan_ast.filter_value = ast.filter_value;

        auto child = Optimize(scan_ast);
        return std::make_unique<DeleteExecutor>(heap, index, std::move(child));
    }
    }

    throw std::runtime_error("Optimizer: unhandled AST node type");
}
