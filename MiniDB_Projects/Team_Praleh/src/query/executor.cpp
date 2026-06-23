#include "query/executor.h"

namespace minidb {

ExecResult Executor::Execute(const Statement& stmt, const std::string& plan) {
    ExecResult r;

    if (stmt.type == StmtType::INSERT) {
        page_id_t dummy;
        if (tree_->Search(stmt.id, &dummy)) {
            r.message = "ERROR: duplicate key " + std::to_string(stmt.id);
            return r;
        }
        Record rec{stmt.id, stmt.value, 0, {}};
        page_id_t pid = heap_->InsertRecord(rec);
        if (pid == INVALID_PAGE_ID) { r.message = "ERROR: buffer pool full"; return r; }
        tree_->Insert(stmt.id, pid);
        r.success = true;
        r.message = "Inserted (id=" + std::to_string(stmt.id) +
                    ", value=" + std::to_string(stmt.value) + ")";
        return r;
    }

    if (stmt.type == StmtType::SELECT_JOIN) {
        auto records_a = heap_->ScanAll();
        if (plan == "INDEX_JOIN") {
            for (const auto& rec_a : records_a) {
                page_id_t hint;
                if (tree_->Search(rec_a.value, &hint)) {
                    Record rec_b;
                    if (heap_->SelectRecord(rec_a.value, &rec_b)) {
                        r.joined_rows.push_back({rec_a, rec_b});
                    }
                }
            }
        } else { // NESTED_LOOP_JOIN
            auto records_b = heap_->ScanAll();
            for (const auto& rec_a : records_a) {
                for (const auto& rec_b : records_b) {
                    if (rec_a.value == rec_b.id) {
                        r.joined_rows.push_back({rec_a, rec_b});
                    }
                }
            }
        }
        r.success = true;
        r.message = std::to_string(r.joined_rows.size()) + " joined row(s)";
        return r;
    }

    if (stmt.type == StmtType::SELECT) {
        if (stmt.id != -1 && stmt.where_col == ColType::NONE) {
            Record out;
            if (heap_->SelectRecord(stmt.id, &out)) {
                r.success = true; r.rows.push_back(out); r.message = "Found";
            } else {
                r.success = true;
                r.message = "Not found (id=" + std::to_string(stmt.id) + ")";
            }
            return r;
        }

        if (stmt.where_op != OpType::NONE) {
            if (plan == "INDEX_SCAN_FILTER") {
                auto key_hints = tree_->ScanRange(stmt.where_val);
                for (const auto& kh : key_hints) {
                    Record out;
                    if (heap_->SelectRecord(kh.first, &out)) {
                        r.rows.push_back(out);
                    }
                }
            } else { // TABLE_SCAN
                auto all_recs = heap_->ScanAll();
                for (const auto& rec : all_recs) {
                    bool match = false;
                    int32_t val = (stmt.where_col == ColType::ID) ? rec.id : rec.value;
                    if (stmt.where_op == OpType::EQUAL) {
                        match = (val == stmt.where_val);
                    } else if (stmt.where_op == OpType::GREATER) {
                        match = (val > stmt.where_val);
                    }
                    if (match) r.rows.push_back(rec);
                }
            }
            r.success = true;
            r.message = std::to_string(r.rows.size()) + " filtered row(s)";
            return r;
        }
    }

    if (stmt.type == StmtType::SELECT || stmt.type == StmtType::SHOW) {
        r.rows    = heap_->ScanAll();
        r.success = true;
        r.message = std::to_string(r.rows.size()) + " row(s)";
        return r;
    }

    if (stmt.type == StmtType::DELETE) {
        if (heap_->DeleteRecord(stmt.id)) {
            tree_->Delete(stmt.id);
            r.success = true;
            r.message = "Deleted (id=" + std::to_string(stmt.id) + ")";
        } else {
            r.message = "Not found (id=" + std::to_string(stmt.id) + ")";
        }
        return r;
    }

    r.message = "ERROR: unknown statement"; return r;
}

} // namespace minidb
