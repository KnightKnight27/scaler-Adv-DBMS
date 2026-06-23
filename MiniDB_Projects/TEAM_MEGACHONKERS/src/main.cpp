#include <iostream>
#include <memory>
#include <string>

#include "catalog/catalog.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "execution/executor_context.h"
#include "parser/parser.h"
#include "parser/ast.h"
#include "planner/planner.h"

using namespace minidb;

// ===========================================================================
// MiniDB REPL
//
// The pipeline for each line is: raw SQL -> Lexer -> Parser (AST) -> Planner
// (physical executor tree) -> Volcano execution. DDL (CREATE TABLE/INDEX) and
// transaction control (BEGIN/COMMIT/ROLLBACK) are handled directly here; DML
// (SELECT/INSERT/DELETE) is planned and executed.
// ===========================================================================

namespace {

// Runs a SELECT plan and prints a formatted result set, using the executor's
// own output schema for headers (so projections show only projected columns).
void PrintSelect(AbstractExecutor* plan) {
    plan->Init();

    const Schema* schema = plan->GetOutputSchema();
    std::string separator;
    if (schema != nullptr) {
        for (const auto& col : schema->GetColumns()) {
            std::cout << col.name_ << "\t| ";
            separator += "--------";
        }
        std::cout << "\n" << separator << "\n";
    }

    Row row;
    int count = 0;
    while (plan->Next(&row)) {
        for (const auto& value : row.columns) std::cout << value << "\t| ";
        std::cout << "\n";
        ++count;
    }
    std::cout << "(" << count << " rows)\n";
}

// Runs an INSERT/DELETE plan whose single result row carries the affected count.
void PrintModification(AbstractExecutor* plan, const std::string& verb) {
    plan->Init();
    Row result;
    if (plan->Next(&result) && !result.columns.empty()) {
        std::cout << "Success: " << result.columns[0] << " row(s) " << verb << ".\n";
    } else {
        std::cout << "Success: 0 row(s) " << verb << ".\n";
    }
}

} // namespace

int main() {
    std::cout << "========================================\n";
    std::cout << "       MiniDB (LSM-Tree Engine)         \n";
    std::cout << "========================================\n";
    std::cout << "Type 'EXIT;' to quit.\n";
    std::cout << "Supported SQL:\n";
    std::cout << "  -> CREATE TABLE <name> (<col> <type>, ...);\n";
    std::cout << "  -> CREATE INDEX <name> ON <table> (<col>);\n";
    std::cout << "  -> INSERT INTO <name> VALUES (..), (..);\n";
    std::cout << "  -> SELECT <cols|*> FROM <name> [JOIN <t> ON a=b] [WHERE <expr>];\n";
    std::cout << "  -> DELETE FROM <name> [WHERE <expr>];\n";
    std::cout << "  -> BEGIN; COMMIT; ROLLBACK;\n";
    std::cout << "----------------------------------------\n";

    Catalog catalog;
    LockManager lock_manager;
    TransactionManager txn_manager(&lock_manager);

    // The explicit, user-controlled transaction (null => auto-commit mode).
    Transaction* active_txn = nullptr;

    std::string input;
    while (true) {
        std::cout << (active_txn ? "minidb*> " : "minidb> ");
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        Parser parser(input);
        StmtPtr stmt = parser.Parse();

        switch (stmt->type) {
            case StatementType::EXIT:
                if (active_txn) txn_manager.Commit(active_txn);
                std::cout << "Shutting down MiniDB cleanly. Bye!\n";
                return 0;

            case StatementType::INVALID:
                std::cout << static_cast<InvalidStatement*>(stmt.get())->error << "\n";
                break;

            case StatementType::BEGIN_TXN:
                if (active_txn) {
                    std::cout << "Error: a transaction is already in progress.\n";
                } else {
                    active_txn = txn_manager.Begin();
                    std::cout << "Transaction started (txn "
                              << active_txn->GetTransactionId() << ").\n";
                }
                break;

            case StatementType::COMMIT_TXN:
                if (!active_txn) {
                    std::cout << "Error: no active transaction to COMMIT.\n";
                } else {
                    txn_manager.Commit(active_txn);
                    active_txn = nullptr;
                    std::cout << "Transaction committed.\n";
                }
                break;

            case StatementType::ROLLBACK_TXN:
                if (!active_txn) {
                    std::cout << "Error: no active transaction to ROLLBACK.\n";
                } else {
                    txn_manager.Abort(active_txn);
                    active_txn = nullptr;
                    // Honest caveat: this LSM build has no undo log, so locks are
                    // released and the transaction ends, but already-applied row
                    // mutations are not reverted.
                    std::cout << "Transaction rolled back (locks released; note: "
                                 "row mutations are not undone in this build).\n";
                }
                break;

            case StatementType::CREATE_TABLE: {
                auto* create = static_cast<CreateTableStatement*>(stmt.get());
                std::vector<Column> columns;
                for (const auto& col : create->columns) {
                    columns.emplace_back(col.name, col.type, col.length);
                }
                Schema schema(columns);
                if (catalog.CreateTable(create->table_name, schema)) {
                    std::cout << "Success: Table '" << create->table_name << "' created with "
                              << columns.size() << " columns.\n";
                } else {
                    std::cout << "Error: could not create table '" << create->table_name
                              << "' (it may already exist).\n";
                }
                break;
            }

            case StatementType::CREATE_INDEX: {
                auto* create = static_cast<CreateIndexStatement*>(stmt.get());
                if (catalog.CreateIndex(create->index_name, create->table_name,
                                        create->column_name)) {
                    std::cout << "Success: Index '" << create->index_name << "' created on "
                              << create->table_name << "(" << create->column_name << ").\n";
                } else {
                    std::cout << "Error: could not create index '" << create->index_name << "'.\n";
                }
                break;
            }

            case StatementType::SELECT:
            case StatementType::INSERT:
            case StatementType::DELETE: {
                bool auto_commit = (active_txn == nullptr);
                Transaction* txn = auto_commit ? txn_manager.Begin() : active_txn;
                ExecutorContext context(&catalog, txn->GetTransactionId());
                Planner planner(&context);
                try {
                    if (stmt->type == StatementType::SELECT) {
                        auto plan = planner.PlanSelect(*static_cast<SelectStatement*>(stmt.get()));
                        PrintSelect(plan.get());
                    } else if (stmt->type == StatementType::INSERT) {
                        auto plan = planner.PlanInsert(*static_cast<InsertStatement*>(stmt.get()));
                        PrintModification(plan.get(), "inserted");
                    } else {
                        auto plan = planner.PlanDelete(*static_cast<DeleteStatement*>(stmt.get()));
                        PrintModification(plan.get(), "deleted");
                    }
                } catch (const std::exception& e) {
                    std::cout << "Error: " << e.what() << "\n";
                }
                if (auto_commit) txn_manager.Commit(txn);
                break;
            }
        }
    }

    if (active_txn) txn_manager.Commit(active_txn);
    std::cout << "Shutting down MiniDB cleanly. Bye!\n";
    return 0;
}
