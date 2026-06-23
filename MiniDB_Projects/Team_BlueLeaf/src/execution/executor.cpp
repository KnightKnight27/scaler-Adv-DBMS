#include "execution/executor.h"

#include <iostream>
#include <string>
#include <vector>

#include "catalog/record.h"
#include "common/exception.h"
#include "execution/expr_eval.h"
#include "execution/operators.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "planner/optimizer.h"

namespace minidb {

namespace {

std::string value_str(const Value& v) {
    if (std::holds_alternative<std::int64_t>(v)) return std::to_string(std::get<std::int64_t>(v));
    if (std::holds_alternative<double>(v)) return std::to_string(std::get<double>(v));
    return std::get<std::string>(v);
}

// Coerce a parsed literal to the column's declared type where it is safe
// (integer literal into a DOUBLE column). Other mismatches are left for the
// record codec to reject.
Value coerce(Value v, ValueType t) {
    if (t == ValueType::DOUBLE && std::holds_alternative<std::int64_t>(v))
        return Value{static_cast<double>(std::get<std::int64_t>(v))};
    return v;
}

} // namespace

void Executor::execute_script(const std::string& sql) {
    Lexer lex(sql);
    Parser parser(lex.tokenize());
    while (!parser.at_end()) {
        StmtPtr stmt = parser.parse();
        exec_statement(*stmt);
    }
}

void Executor::exec_statement(Statement& stmt) {
    switch (stmt.kind()) {
        case StmtKind::CreateTable: exec_create(static_cast<CreateTableStmt&>(stmt)); break;
        case StmtKind::Insert:      exec_insert(static_cast<InsertStmt&>(stmt)); break;
        case StmtKind::Delete:      exec_delete(static_cast<DeleteStmt&>(stmt)); break;
        case StmtKind::Select:      exec_select(static_cast<SelectStmt&>(stmt)); break;
    }
}

void Executor::exec_create(CreateTableStmt& s) {
    if (s.primary_key < 0)
        throw DBException("CREATE TABLE " + s.table + ": a PRIMARY KEY is required");
    if (s.columns[static_cast<std::size_t>(s.primary_key)].type != ValueType::INT)
        throw DBException("CREATE TABLE " + s.table + ": primary key must be INT");
    Schema schema(s.columns);
    engine_->create_table(s.table, schema, s.primary_key);
    // Checkpoint DDL immediately: flush the new (empty) structures to disk and
    // clear the WAL, so recovery always replays row ops onto a consistent file.
    if (log_) { engine_->flush(); log_->truncate(); }
    std::cout << "created table " << s.table << " (" << s.columns.size() << " columns)\n";
}

void Executor::exec_insert(InsertStmt& s) {
    TableInfo* t = cat_->get_table(s.table);
    if (!t) throw DBException("INSERT: no such table: " + s.table);
    const Schema& schema = t->schema;

    TxId tx = next_tx_++;
    int inserted = 0, dups = 0;
    for (auto& row : s.rows) {
        if (row.size() != schema.num_columns())
            throw DBException("INSERT into " + s.table + ": wrong number of values");
        for (std::size_t i = 0; i < row.size(); ++i)
            row[i] = coerce(row[i], schema.column(static_cast<int>(i)).type);
        std::int64_t key = std::get<std::int64_t>(row[static_cast<std::size_t>(t->pk_col)]);
        std::string bytes = Record::serialize(schema, row);
        if (engine_->put(s.table, key, bytes)) {
            ++inserted;
            if (log_) log_->append(LogRecord{LogType::PUT, tx, s.table, key, bytes});  // log-before-data
        } else {
            ++dups;
        }
    }
    if (log_ && inserted > 0) {  // force-log-at-commit
        log_->append(LogRecord{LogType::COMMIT, tx, "", 0, ""});
        log_->flush();
    }
    std::cout << "inserted " << inserted << " row(s)";
    if (dups) std::cout << " (" << dups << " duplicate key(s) skipped)";
    std::cout << "\n";
}

void Executor::exec_delete(DeleteStmt& s) {
    TableInfo* t = cat_->get_table(s.table);
    if (!t) throw DBException("DELETE: no such table: " + s.table);

    // Scan + filter to find matching primary keys, then erase (collect first to
    // avoid mutating the heap while scanning it).
    SeqScan scan(engine_, s.table, t->schema, s.table);
    std::vector<std::int64_t> keys;
    scan.open();
    Tuple tup;
    while (scan.next(tup)) {
        if (s.where && !eval_predicate(s.where.get(), tup, scan.out_schema())) continue;
        keys.push_back(std::get<std::int64_t>(tup.values[static_cast<std::size_t>(t->pk_col)]));
    }
    scan.close();

    TxId tx = next_tx_++;
    int deleted = 0;
    for (std::int64_t k : keys) {
        if (engine_->erase(s.table, k)) {
            ++deleted;
            if (log_) log_->append(LogRecord{LogType::ERASE, tx, s.table, k, ""});
        }
    }
    if (log_ && deleted > 0) {
        log_->append(LogRecord{LogType::COMMIT, tx, "", 0, ""});
        log_->flush();
    }
    std::cout << "deleted " << deleted << " row(s)\n";
}

void Executor::exec_select(SelectStmt& s) {
    Optimizer opt(cat_, engine_);
    std::unique_ptr<Operator> plan = opt.plan(s);
    std::cout << "-- plan: " << opt.explanation() << "\n";

    const OutSchema& os = plan->out_schema();
    for (std::size_t i = 0; i < os.size(); ++i) std::cout << (i ? " | " : "") << os[i].name;
    std::cout << "\n";

    plan->open();
    Tuple row;
    int n = 0;
    while (plan->next(row)) {
        for (std::size_t i = 0; i < row.values.size(); ++i)
            std::cout << (i ? " | " : "") << value_str(row.values[i]);
        std::cout << "\n";
        ++n;
    }
    plan->close();
    std::cout << "(" << n << " row(s))\n";
}

} // namespace minidb
