// ============================================================================
//  database.hpp — The engine façade. One method, execute(sql), drives the
//  whole pipeline: parse -> (optimize for SELECT) -> run -> result.
//
//  This is the seam every other layer plugs into: the REPL, the test harness,
//  and (later) the transaction + recovery layers all call execute(). Keeping
//  DML (INSERT/DELETE) here — rather than as executors — lets us keep the heap
//  and the index in lockstep in one obvious place, which matters when WAL and
//  MVCC hook in.
// ============================================================================
#pragma once

#include "catalog/catalog.hpp"
#include "optimizer/optimizer.hpp"
#include "sql/parser.hpp"

#include <string>
#include <vector>

namespace minidb {

struct QueryResult {
    bool        is_select = false;
    Schema      schema;          // output columns (SELECT)
    std::vector<Tuple> rows;     // result rows (SELECT)
    std::string message;         // status line (DDL/DML or error)
    std::string plan;            // optimizer explain text (SELECT)
    bool        ok = true;
};

class Database {
public:
    Database(BufferPoolManager* bpm) : cat_(bpm), opt_(&cat_) {}

    Catalog& catalog() { return cat_; }

    QueryResult execute(const std::string& sql) {
        try {
            Statement st = Parser(sql).parse();
            switch (st.kind) {
                case Statement::Kind::Create: return do_create(st.create);
                case Statement::Kind::Insert: return do_insert(st.insert);
                case Statement::Kind::Select: return do_select(st.select);
                case Statement::Kind::Delete: return do_delete(st.del);
            }
        } catch (const std::exception& e) {
            QueryResult r; r.ok = false; r.message = std::string("Error: ") + e.what();
            return r;
        }
        return {};
    }

private:
    QueryResult do_create(const CreateStmt& c) {
        Schema s; for (auto& col : c.columns) s.columns.push_back(col);
        cat_.create_table(c.table, s);
        QueryResult r; r.message = "Table '" + c.table + "' created (" +
            std::to_string(c.columns.size()) + " columns)";
        return r;
    }

    QueryResult do_insert(const InsertStmt& ins) {
        TableInfo* t = cat_.get_table(ins.table);
        const Schema& sc = t->schema;

        // Map the provided values onto schema-ordered columns.
        Tuple row;
        row.values.resize(sc.size());
        std::vector<bool> set(sc.size(), false);
        if (ins.columns.empty()) {
            if (ins.values.size() != sc.size())
                throw std::runtime_error("INSERT value count != column count");
            for (size_t i = 0; i < sc.size(); ++i) { row.values[i] = ins.values[i]; set[i] = true; }
        } else {
            if (ins.columns.size() != ins.values.size())
                throw std::runtime_error("INSERT column/value count mismatch");
            for (size_t i = 0; i < ins.columns.size(); ++i) {
                int c = sc.index_of(ins.columns[i]);
                if (c < 0) throw std::runtime_error("no such column: " + ins.columns[i]);
                row.values[c] = ins.values[i]; set[c] = true;
            }
            for (size_t i = 0; i < sc.size(); ++i)
                if (!set[i]) row.values[i] = (sc.columns[i].type == ColType::INT)
                                 ? Value::Int(0) : Value::Text("");
        }

        // Enforce primary-key uniqueness via the index BEFORE touching the heap.
        int64_t pk = t->pk_of(row);
        RID probe;
        if (t->index->search(pk, &probe))
            throw std::runtime_error("duplicate primary key: " + std::to_string(pk));

        RID rid = t->heap->insert(row.serialize(sc));
        t->index->insert(pk, rid);
        t->num_rows++;
        QueryResult r; r.message = "1 row inserted";
        return r;
    }

    QueryResult do_select(const SelectStmt& q) {
        QueryResult r; r.is_select = true;
        auto plan = opt_.plan(q, &r.plan);
        plan->open();
        Tuple t;
        while (plan->next(&t)) r.rows.push_back(t);
        r.schema = plan->out_schema();
        r.message = std::to_string(r.rows.size()) + " row(s)";
        return r;
    }

    // DELETE walks the heap directly so we have each matching RID to remove
    // from BOTH the heap and the primary index (keeping them consistent).
    QueryResult do_delete(const DeleteStmt& d) {
        TableInfo* t = cat_.get_table(d.table);
        Schema qs = qualify(t->name, t->schema);
        std::vector<std::pair<RID, int64_t>> doomed;
        t->heap->scan([&](RID rid, const std::string& bytes) {
            Tuple tup = Tuple::deserialize(bytes.data(), t->schema);
            if (eval_predicate(d.where.get(), tup, qs))
                doomed.emplace_back(rid, t->pk_of(tup));
        });
        for (auto& [rid, pk] : doomed) {
            t->heap->erase(rid);
            t->index->erase(pk);
            t->num_rows--;
        }
        QueryResult r; r.message = std::to_string(doomed.size()) + " row(s) deleted";
        return r;
    }

    Catalog   cat_;
    Optimizer opt_;
};

}  // namespace minidb
