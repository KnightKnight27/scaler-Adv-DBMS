#include "executors.h"

#include "../index/bplus_tree.h"
#include "../recovery/log_manager.h"

namespace minidb {

namespace {
// Acquire a Strict-2PL lock for the current transaction (if any). Throws TxnAborted on a
// detected deadlock so Database can abort the transaction and release its locks.
void LockOrAbort(ExecutionContext* ctx, RID rid, LockMode mode) {
    if (!ctx || !ctx->txn || !ctx->lock_mgr) return;  // autocommit / no-txn path
    if (!ctx->lock_mgr->Acquire(ctx->txn->id(), rid.AsKey(), mode))
        throw TxnAborted{"deadlock detected acquiring " +
                         std::string(mode == LockMode::EXCLUSIVE ? "X" : "S") + " lock on " +
                         rid.ToString()};
}

// Write-Ahead Log the row image of a mutation (skipped on the no-log autocommit/recovery path).
void LogMutation(ExecutionContext* ctx, LogType type, const std::string& table,
                 const std::vector<Value>& row) {
    if (!ctx || !ctx->log) return;
    LogRecord rec;
    rec.type = type;
    rec.txn_id = ctx->txn ? ctx->txn->id() : 0;  // 0 == autocommit
    rec.table = table;
    rec.row = row;
    ctx->log->Append(rec);
}
}  // namespace

Value CoerceTo(const Value& v, TypeId target) {
    switch (target) {
        case TypeId::INTEGER: return Value::MakeInt(static_cast<int32_t>(v.AsBigInt()));
        case TypeId::BIGINT:  return Value::MakeBigInt(v.AsBigInt());
        case TypeId::BOOLEAN: return Value::MakeBool(v.AsBool());
        case TypeId::VARCHAR: return v.type() == TypeId::VARCHAR ? v : Value::MakeVarchar(v.ToString());
        default:              return v;
    }
}

// ---- SeqScan ----
void SeqScanExecutor::Init() {
    it_.emplace(table_->heap->begin());
    end_.emplace(table_->heap->end());
}
bool SeqScanExecutor::Next(Tuple* out) {
    if (!it_ || !(*it_ != *end_)) return false;
    RID rid = it_->GetRID();
    LockOrAbort(ctx_, rid, read_mode_);  // Strict 2PL: S for reads, X for SELECT ... FOR UPDATE
    *out = Tuple::Deserialize(it_->GetRecord(), table_->schema);
    out->SetRid(rid);
    ++(*it_);
    return true;
}

// ---- IndexScan ----
void IndexScanExecutor::Init() {
    rids_ = index_->tree->Search(key_);
    pos_ = 0;
}
bool IndexScanExecutor::Next(Tuple* out) {
    std::string bytes;
    while (pos_ < rids_.size()) {
        RID rid = rids_[pos_++];
        if (!table_->heap->Get(rid, &bytes)) continue;  // tombstoned; skip
        LockOrAbort(ctx_, rid, read_mode_);
        *out = Tuple::Deserialize(bytes, table_->schema);
        out->SetRid(rid);
        return true;
    }
    return false;
}

// ---- Filter ----
bool FilterExecutor::Next(Tuple* out) {
    Tuple t;
    while (child_->Next(&t)) {
        if (Evaluator::Matches(pred_, t, child_->OutSchema())) {
            *out = std::move(t);
            return true;
        }
    }
    return false;
}

// ---- Project ----
bool ProjectExecutor::Next(Tuple* out) {
    Tuple t;
    if (!child_->Next(&t)) return false;
    std::vector<Value> vals;
    vals.reserve(cols_.size());
    for (int c : cols_) vals.push_back(t.GetValue(c));
    *out = Tuple(std::move(vals));
    out->SetRid(t.rid());
    return true;
}

// ---- NestedLoopJoin ----
void NestedLoopJoinExecutor::Init() {
    left_->Init();
    right_->Init();
    right_rows_.clear();
    Tuple t;
    while (right_->Next(&t)) right_rows_.push_back(t);
    cur_left_.reset();
    right_pos_ = 0;
}
bool NestedLoopJoinExecutor::Next(Tuple* out) {
    while (true) {
        if (!cur_left_) {
            Tuple l;
            if (!left_->Next(&l)) return false;
            cur_left_ = std::move(l);
            right_pos_ = 0;
        }
        while (right_pos_ < right_rows_.size()) {
            const Tuple& r = right_rows_[right_pos_++];
            if (cur_left_->GetValue(left_key_) == r.GetValue(right_key_)) {
                std::vector<Value> vals = cur_left_->Values();
                vals.insert(vals.end(), r.Values().begin(), r.Values().end());
                *out = Tuple(std::move(vals));
                return true;
            }
        }
        cur_left_.reset();  // exhausted right for this left row; advance left
    }
}

// ---- Insert ----
void InsertExecutor::Init() {
    const Schema& schema = table_->schema;
    for (auto& raw : rows_) {
        std::vector<Value> vals;
        vals.reserve(schema.Count());
        for (size_t i = 0; i < schema.Count(); ++i)
            vals.push_back(CoerceTo(raw[i], schema.GetColumn(i).type));
        Tuple row(std::move(vals));
        RID rid = table_->heap->Insert(row.Serialize(schema));
        LockOrAbort(ctx_, rid, LockMode::EXCLUSIVE);  // X lock on the newly inserted row
        if (ctx_ && ctx_->txn) ctx_->txn->RecordWrite(table_->name, rid);
        LogMutation(ctx_, LogType::Insert, table_->name, row.Values());
        for (auto& ix : table_->indexes) ix.tree->Insert(row.GetValue(ix.col_idx), rid);
        table_->num_rows++;
        affected_++;
    }
}

// ---- Delete ----
void DeleteExecutor::Init() {
    child_->Init();
    Tuple t;
    while (child_->Next(&t)) {
        RID rid = t.rid();
        if (!rid.IsValid()) continue;
        LockOrAbort(ctx_, rid, LockMode::EXCLUSIVE);  // X lock before removing the row
        if (ctx_ && ctx_->txn) ctx_->txn->RecordWrite(table_->name, rid);
        for (auto& ix : table_->indexes) ix.tree->Remove(t.GetValue(ix.col_idx), rid);
        if (table_->heap->Delete(rid)) {
            LogMutation(ctx_, LogType::Delete, table_->name, t.Values());
            if (table_->num_rows > 0) table_->num_rows--;
            affected_++;
        }
    }
}

}  // namespace minidb
