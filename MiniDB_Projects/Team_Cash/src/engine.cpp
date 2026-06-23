#include "engine.h"

#include <stdexcept>

#include "executor.h"
#include "storage.h"

namespace minidb {

Engine::Engine(const std::string& dataDir) : catalog_(dataDir), optimizer_(catalog_) {}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n;");
    return s.substr(a, b - a + 1);
}

Result Engine::execute(const std::string& sql) {
    std::unique_ptr<Statement> stmt = parse(trim(sql));
    switch (stmt->kind) {
        case StmtKind::Create: return doCreate(static_cast<CreateStmt&>(*stmt));
        case StmtKind::Insert: return doInsert(static_cast<InsertStmt&>(*stmt));
        case StmtKind::Select: return doSelect(static_cast<SelectStmt&>(*stmt));
        case StmtKind::Delete: return doDelete(static_cast<DeleteStmt&>(*stmt));
    }
    throw std::runtime_error("unsupported statement");
}

Result Engine::explain(const std::string& sql) {
    std::unique_ptr<Statement> stmt = parse(trim(sql));
    if (stmt->kind != StmtKind::Select) throw std::runtime_error(".explain only works on SELECT");
    auto plan = optimizer_.optimize(static_cast<SelectStmt&>(*stmt));
    return Result{Result::Explain, explainPlan(plan.get()), {}, {}};
}

Result Engine::doCreate(const CreateStmt& s) {
    Schema schema;
    schema.columns = s.columns;
    catalog_.createTable(s.table, schema);
    return Result{Result::Message,
                  "Table '" + s.table + "' created with " + std::to_string(schema.size()) + " columns.",
                  {}, {}};
}

Result Engine::doInsert(const InsertStmt& s) {
    TableInfo* t = catalog_.get(s.table);
    if (!t) throw std::runtime_error("table '" + s.table + "' does not exist");
    if (s.values.size() != t->schema.size())
        throw std::runtime_error("expected " + std::to_string(t->schema.size()) + " values");

    for (size_t i = 0; i < s.values.size(); ++i) {
        const Column& col = t->schema.columns[i];
        if (col.type == Type::INT && !s.values[i].isInt())
            throw std::runtime_error("column '" + col.name + "' expects an INT");
        if (col.type == Type::TEXT && s.values[i].isInt())
            throw std::runtime_error("column '" + col.name + "' expects TEXT");
    }

    const Value& key = s.values[0];  // primary key = first column
    if (t->isIndexed() && key.isInt()) {
        RID existing;
        if (t->index->search(key.i, existing))
            throw std::runtime_error("duplicate primary key " + key.toString());
    }

    // Phase 2: log_manager->logInsert(...) goes here (write-ahead).
    RID rid = t->heap->insert(encodeRow(s.values));
    catalog_.onInsert(t, key, rid);
    return Result{Result::Message, "Inserted 1 row into '" + s.table + "'.", {}, {}};
}

Result Engine::doSelect(const SelectStmt& s) {
    auto plan = optimizer_.optimize(s);
    std::unique_ptr<Operator> op = buildOperator(plan.get(), catalog_);

    Result r;
    r.kind = Result::Rows;
    for (const std::string& q : op->outputColumns)
        r.headers.push_back(q.substr(q.find('.') + 1));  // strip "table." for display

    Row row;
    while (op->next(row)) r.rows.push_back(row);
    return r;
}

Result Engine::doDelete(const DeleteStmt& s) {
    TableInfo* t = catalog_.get(s.table);
    if (!t) throw std::runtime_error("table '" + s.table + "' does not exist");

    std::vector<std::string> cols;
    for (const Column& c : t->schema.columns) cols.push_back(s.table + "." + c.name);

    std::vector<std::pair<RID, Value>> victims;
    int pages = t->disk->numPages();
    for (int pg = 0; pg < pages; ++pg) {
        Page* page = t->pool->fetch(pg);
        for (int slot = 0; slot < page->numSlots(); ++slot) {
            std::string rec;
            if (!page->get(slot, rec)) continue;
            Row row = decodeRow(rec);
            bool match = true;
            for (const Condition& c : s.where) {
                int idx = resolveColumn(cols, c.left);
                if (!valueCompare(row[idx], c.op, c.literal)) { match = false; break; }
            }
            if (match) victims.push_back({RID{pg, slot}, row[0]});
        }
    }

    for (auto& v : victims) {
        // Phase 2: log_manager->logDelete(...) goes here (write-ahead).
        t->heap->erase(v.first);
        catalog_.onDelete(t, v.second);
    }
    return Result{Result::Message,
                  "Deleted " + std::to_string(victims.size()) + " row(s) from '" + s.table + "'.",
                  {}, {}};
}

}  // namespace minidb
