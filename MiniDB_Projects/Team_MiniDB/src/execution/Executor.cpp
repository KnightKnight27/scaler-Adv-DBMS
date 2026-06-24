#include "execution/Executor.hpp"

#include "performance/BatchExecutor.hpp"

#include <cstdlib>

using namespace std;

namespace minidb {

Executor::Executor(Catalog& catalog, bool use_batch_mode)
    : catalog_(catalog), use_batch_mode_(use_batch_mode) {}

bool Executor::compareValues(const Value& left, CompareOp op, const Value& right) {
    char* end = nullptr;
    long lv = strtol(left.c_str(), &end, 10);
    bool left_num = (*end == '\0');
    long rv = strtol(right.c_str(), &end, 10);
    bool right_num = (*end == '\0');
    if (left_num && right_num) {
        switch (op) {
            case CompareOp::EQ: return lv == rv;
            case CompareOp::NE: return lv != rv;
            case CompareOp::LT: return lv < rv;
            case CompareOp::LE: return lv <= rv;
            case CompareOp::GT: return lv > rv;
            case CompareOp::GE: return lv >= rv;
        }
    }
    switch (op) {
        case CompareOp::EQ: return left == right;
        case CompareOp::NE: return left != right;
        case CompareOp::LT: return left < right;
        case CompareOp::LE: return left <= right;
        case CompareOp::GT: return left > right;
        case CompareOp::GE: return left >= right;
    }
    return false;
}

bool Executor::evaluatePredicate(const Row& row, const WhereClause& where, const TableDef& table) {
    (void)table;
    auto it = row.find(where.column);
    if (it == row.end()) return false;
    return compareValues(it->second, where.op, where.value);
}

RowList Executor::tableScan(const string& table, const WhereClause& where, bool has_where) {
    RowList result;
    HeapFile* hf = catalog_.getHeapFile(table);
    const TableDef* def = catalog_.getTable(table);
    if (!hf || !def) return result;
    RowList all = hf->scanAll();
    if (use_batch_mode_) {
        return BatchExecutor::filterBatches(all, [&](const Row& row) {
            return !has_where || evaluatePredicate(row, where, *def);
        });
    }
    for (const Row& row : all)
        if (!has_where || evaluatePredicate(row, where, *def)) result.push_back(row);
    return result;
}

RowList Executor::indexScan(const string& table, const WhereClause& where) {
    RowList result;
    if (where.op != CompareOp::EQ) return tableScan(table, where, true);
    BPlusTree* idx = catalog_.getPrimaryIndex(table);
    HeapFile* hf = catalog_.getHeapFile(table);
    const TableDef* def = catalog_.getTable(table);
    if (!idx || !hf || !def) return result;
    if (!idx->search(atoi(where.value.c_str())).has_value()) return result;
    for (const Row& row : hf->scanAll()) {
        auto pk = row.find(def->primary_key_column);
        if (pk != row.end() && pk->second == where.value) { result.push_back(row); break; }
    }
    return result;
}

RowList Executor::nestedLoopJoin(const JoinClause& join, const QueryPlan& plan) {
    RowList outer = tableScan(plan.join_outer_table, plan.where, plan.has_where);
    RowList inner_all = tableScan(plan.join_inner_table, plan.where, false);
    RowList joined;
    for (const Row& o : outer) {
        for (const Row& i : inner_all) {
            string lv, rv;
            if (o.count(join.left_column)) lv = o.at(join.left_column);
            if (i.count(join.right_column)) rv = i.at(join.right_column);
            if (lv == rv) {
                Row merged = o;
                for (const auto& kv : i) merged[kv.first] = kv.second;
                joined.push_back(merged);
            }
        }
    }
    return joined;
}

RowList Executor::executeSelect(const QueryPlan& plan) {
    if (plan.has_join) return nestedLoopJoin(plan.join, plan);
    if (plan.scan == ScanType::INDEX_SCAN && plan.has_where) return indexScan(plan.table_name, plan.where);
    return tableScan(plan.table_name, plan.where, plan.has_where);
}

}  // namespace minidb
