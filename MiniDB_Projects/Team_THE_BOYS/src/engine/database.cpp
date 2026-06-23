#include "engine/database.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace minidb {

namespace {

void ApplyUndo(Executor& executor, const TransactionManager* txn_manager) {
    const auto& log = txn_manager->undo_log();
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        if (it->op == UndoEntry::Op::INSERT) {
            executor.UndoInsert(it->table, it->row, it->rid);
        } else {
            executor.UndoDelete(it->table, it->row);
        }
    }
}

}  // namespace

Database::Database(std::string db_path) : db_path_(std::move(db_path)) {
    std::filesystem::create_directories(db_path_);
    page_manager_ = std::make_unique<PageManager>(db_path_ + "/minidb.dat");
    buffer_pool_ = std::make_unique<BufferPool>(page_manager_.get());
    wal_ = std::make_unique<WriteAheadLog>(db_path_ + "/minidb.wal");
    lock_manager_ = std::make_unique<LockManager>();
    txn_manager_ = std::make_unique<TransactionManager>(lock_manager_.get(), wal_.get());
    recovery_manager_ = std::make_unique<RecoveryManager>(&catalog_, wal_.get());
    catalog_.Load(db_path_ + "/catalog.meta", page_manager_.get(), buffer_pool_.get());
}

Database::~Database() {
    if (buffer_pool_) {
        buffer_pool_->FlushAllPages();
    }
    catalog_.Save(db_path_ + "/catalog.meta");
}

void Database::Flush() {
    buffer_pool_->FlushAllPages();
}

void Database::Recover() { recovery_manager_->Recover(); }

void Database::Crash() {
    buffer_pool_->FlushAllPages();
    catalog_.Save(db_path_ + "/catalog.meta");
    std::exit(1);
}

std::string Database::FormatResult(const std::vector<std::vector<std::string>>& rows) {
    if (rows.empty()) return "(0 rows)\n";
    std::ostringstream oss;
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i > 0) oss << " | ";
            oss << row[i];
        }
        oss << '\n';
    }
    oss << '(' << rows.size() << " rows)\n";
    return oss.str();
}

std::string Database::HandleCreateTable(const CreateTableStmt& stmt) {
    TableSchema schema;
    schema.name = stmt.table;
    schema.columns = stmt.columns;

    auto initLeafPage = [&](int page_id) {
        Page* p = buffer_pool_->FetchPage(page_id);
        std::vector<char> blank(PAGE_SIZE, '\0');
        blank[0] = 1;
        blank[1] = 0;
        int no_next = INVALID_PAGE_ID;
        std::memcpy(blank.data() + 8, &no_next, sizeof(no_next));
        std::memcpy(p->MutableData(), blank.data(), PAGE_SIZE);
        buffer_pool_->UnpinPage(page_id, true);
    };

    int heap_page = page_manager_->AllocatePage();
    Page* page = buffer_pool_->FetchPage(heap_page);
    page->Initialize();
    buffer_pool_->UnpinPage(heap_page, true);
    schema.heap_first_page = heap_page;

    int index_root = page_manager_->AllocatePage();
    initLeafPage(index_root);
    schema.pk_index_root = index_root;

    for (const auto& col : schema.columns) {
        if (col.indexed && !col.primary_key) {
            int sec_root = page_manager_->AllocatePage();
            initLeafPage(sec_root);
            schema.secondary_indexes[col.name] = sec_root;
        }
    }

    if (!catalog_.CreateTable(schema)) {
        return "ERROR: table already exists\n";
    }

    auto heap = std::make_unique<HeapFile>(page_manager_.get(), buffer_pool_.get(), heap_page);
    catalog_.RegisterHeapFile(stmt.table, std::move(heap));

    auto pk_index = std::make_unique<BPlusTree>(page_manager_.get(), buffer_pool_.get(), index_root);
    catalog_.RegisterIndex(stmt.table, "", std::move(pk_index));

    for (const auto& [col_name, sec_root] : schema.secondary_indexes) {
        auto sec = std::make_unique<BPlusTree>(page_manager_.get(), buffer_pool_.get(), sec_root);
        catalog_.RegisterIndex(stmt.table, col_name, std::move(sec));
    }

    catalog_.Save(db_path_ + "/catalog.meta");
    return "CREATE TABLE OK\n";
}

std::string Database::ExecuteSql(const std::string& sql) {
    ParsedStatement stmt = parser_.Parse(sql);
    Executor executor(&catalog_, txn_manager_.get(), use_batch_);

    try {
        switch (stmt.type) {
            case StmtType::CREATE_TABLE:
                return HandleCreateTable(stmt.create_table);
            case StmtType::INSERT: {
                int n = executor.ExecuteInsert(stmt.insert);
                bool explicit_txn = txn_manager_->IsExplicitTransaction();
                txn_manager_->CommitIfAuto();
                if (!explicit_txn) Flush();
                return "INSERT " + std::to_string(n) + " row(s)\n";
            }
            case StmtType::DELETE_STMT: {
                int n = executor.ExecuteDelete(stmt.delete_stmt);
                bool explicit_txn = txn_manager_->IsExplicitTransaction();
                txn_manager_->CommitIfAuto();
                if (!explicit_txn) Flush();
                return "DELETE " + std::to_string(n) + " row(s)\n";
            }
            case StmtType::SELECT: {
                auto plan = optimizer_.Optimize(stmt.select, &catalog_);
                auto rows = executor.ExecuteSelect(plan);
                txn_manager_->CommitIfAuto();
                std::ostringstream oss;
                oss << "Plan: ";
                switch (plan->type) {
                    case PlanType::SEQ_SCAN: oss << "SeqScan"; break;
                    case PlanType::INDEX_SCAN: oss << "IndexScan"; break;
                    case PlanType::NESTED_LOOP_JOIN:
                        oss << "NestedLoopJoin(" << plan->join.left_table << ","
                            << plan->join.right_table << ")";
                        break;
                    case PlanType::AGGREGATE: oss << "Aggregate"; break;
                    default: oss << "Other"; break;
                }
                oss << " (cost=" << plan->estimated_cost << ")\n";
                oss << FormatResult(rows);
                return oss.str();
            }
            case StmtType::BEGIN_TXN:
                txn_manager_->Begin();
                return "BEGIN OK\n";
            case StmtType::COMMIT:
                txn_manager_->Commit();
                Flush();
                return "COMMIT OK\n";
            case StmtType::ROLLBACK:
                ApplyUndo(executor, txn_manager_.get());
                txn_manager_->Rollback();
                txn_manager_->ClearUndoLog();
                return "ROLLBACK OK\n";
            case StmtType::CHECKPOINT:
                recovery_manager_->Checkpoint();
                buffer_pool_->FlushAllPages();
                catalog_.Save(db_path_ + "/catalog.meta");
                return "CHECKPOINT OK\n";
            case StmtType::CRASH:
                Crash();
                return "Simulating crash...\n";
            case StmtType::USE_BATCH:
                use_batch_ = true;
                return "Execution mode: BATCH (Track A)\n";
            case StmtType::USE_ROW:
                use_batch_ = false;
                return "Execution mode: ROW\n";
        }
    } catch (...) {
        if (txn_manager_->InTransaction()) {
            ApplyUndo(executor, txn_manager_.get());
            txn_manager_->Rollback();
            txn_manager_->ClearUndoLog();
        }
        throw;
    }
    return "OK\n";
}

}  // namespace minidb
