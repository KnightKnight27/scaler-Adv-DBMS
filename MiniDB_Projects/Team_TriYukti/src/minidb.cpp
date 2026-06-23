#include "sql/parser.h"
#include "sql/executor.h"
#include "optimizer/optimizer.h"
#include "transaction/transaction.h"
#include "transaction/lock_manager.h"
#include "recovery/log_manager.h"
#include "recovery/recovery.h"
#include "mvcc/mvcc_manager.h"
#include <iostream>
#include <atomic>
#include <string>

using namespace minidb;

int main() {
    std::cout << "Welcome to MiniDB REPL" << std::endl;
    std::cout << "Type 'exit' to quit." << std::endl;

    PageManager pm("minidb.db");
    BufferPool bp(10, &pm);
    LockManager lm;
    LogManager logm("wal.log");
    MVCCManager mvcc_manager(&bp);
    RecoveryManager rm(&logm, &bp, &mvcc_manager);
    std::atomic<int32_t> global_ts(0);
    
    ExecutorContext ctx;
    ctx.buffer_pool_ = &bp;
    ctx.mvcc_manager_ = &mvcc_manager;
    
    std::cout << "Loading catalog..." << std::endl;
    ctx.LoadCatalog("catalog.bin");
    
    std::cout << "Running recovery..." << std::endl;
    rm.Recover();

    Optimizer optimizer(&ctx);

    Transaction *current_txn = nullptr;
    txn_id_t next_txn_id = 1;

    std::string command_buffer;
    std::string input;
    while (true) {
        if (command_buffer.empty()) {
            std::cout << "minidb> ";
        } else {
            std::cout << "      > ";
        }
        
        if (!std::getline(std::cin, input)) break;
        
        // Trim input for checking exit
        std::string trimmed = input;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
        
        if (command_buffer.empty() && (trimmed == "exit" || trimmed == "quit")) break;
        if (command_buffer.empty() && trimmed.empty()) continue;
        
        command_buffer += input + " ";
        
        // Wait for semicolon
        size_t semi_pos = command_buffer.find(';');
        if (semi_pos == std::string::npos) {
            continue;
        }
        
        std::string stmt_str = command_buffer.substr(0, semi_pos);
        command_buffer.clear(); // Reset buffer
        
        ParsedStatement stmt = Parser::Parse(stmt_str);
        
        if (stmt.type == StatementType::UNKNOWN) {
            std::cout << "Error: Unknown or unsupported command." << std::endl;
            continue;
        }
        if (stmt.type == StatementType::EMPTY) continue;

        if (stmt.type == StatementType::BEGIN) {
            if (current_txn != nullptr) {
                std::cout << "Transaction already active." << std::endl;
            } else {
                int32_t ts = ++global_ts;
                current_txn = new Transaction(ts);
                current_txn->SetSnapshotTimestamp(ts);
                ctx.txn_ = current_txn;
                lm.AddTransaction(current_txn);
                
                LogRecord r;
                r.type = LogRecordType::BEGIN;
                r.txn_id = current_txn->GetTransactionId();
                logm.AppendLogRecord(r);
                
                std::cout << "Transaction " << current_txn->GetTransactionId() << " started." << std::endl;
            }
        } else if (stmt.type == StatementType::COMMIT) {
            if (current_txn) {
                current_txn->SetState(TransactionState::COMMITTED);
                
                for (const auto &res : current_txn->GetSharedLockSet()) lm.Unlock(current_txn, res);
                for (const auto &res : current_txn->GetExclusiveLockSet()) lm.Unlock(current_txn, res);
                
                LogRecord r;
                r.type = LogRecordType::COMMIT;
                r.txn_id = current_txn->GetTransactionId();
                int32_t commit_ts = ++global_ts;
                r.commit_ts = commit_ts;
                r.lsn = logm.AppendLogRecord(r);
                logm.Flush();
                
                mvcc_manager.RecordCommit(current_txn->GetTransactionId(), commit_ts);
                
                std::cout << "Transaction " << current_txn->GetTransactionId() << " committed." << std::endl;
                ctx.write_sets_.erase(current_txn->GetTransactionId());
                lm.RemoveTransaction(current_txn->GetTransactionId());
                delete current_txn;
                current_txn = nullptr;
                ctx.txn_ = nullptr;
            } else {
                std::cout << "No active transaction." << std::endl;
            }
        } else if (stmt.type == StatementType::ROLLBACK) {
            if (current_txn) {
                current_txn->SetState(TransactionState::ABORTED);
                
                for (const auto &res : current_txn->GetSharedLockSet()) lm.Unlock(current_txn, res);
                for (const auto &res : current_txn->GetExclusiveLockSet()) lm.Unlock(current_txn, res);
                
                LogRecord r;
                r.type = LogRecordType::ABORT;
                r.txn_id = current_txn->GetTransactionId();
                logm.AppendLogRecord(r);
                
                auto &wset = ctx.write_sets_[current_txn->GetTransactionId()];
                for (auto it = wset.rbegin(); it != wset.rend(); ++it) {
                    Page *p = bp.FetchPage(it->rid.page_id);
                    if (p) {
                        if (it->before_image.data_.empty()) {
                            p->DeleteTuple(it->rid.slot_id);
                        } else {
                            p->UpdateTuple(it->rid.slot_id, it->before_image);
                        }
                        bp.UnpinPage(it->rid.page_id, true);
                    }
                }
                ctx.write_sets_.erase(current_txn->GetTransactionId());
                
                std::cout << "Transaction " << current_txn->GetTransactionId() << " aborted and rolled back." << std::endl;
                lm.RemoveTransaction(current_txn->GetTransactionId());
                delete current_txn;
                current_txn = nullptr;
                ctx.txn_ = nullptr;
            } else {
                std::cout << "No active transaction." << std::endl;
            }
        } else if (stmt.type == StatementType::CREATE_TABLE) {
            page_id_t new_page_id;
            Page *p = bp.NewPage(&new_page_id);
            bp.UnpinPage(new_page_id, true);
            
            TableInfo tinfo;
            tinfo.name = stmt.table_name;
            tinfo.first_page_id = new_page_id;
            tinfo.last_page_id = new_page_id;
            tinfo.index = new BPlusTree(&bp);
            
            for (const auto& col : stmt.columns_with_type) {
                Column c;
                c.name = col.first;
                if (col.second == "int" || col.second == "INT") {
                    c.type = ColumnType::INT;
                    c.size = 4;
                } else {
                    c.type = ColumnType::VARCHAR;
                    c.size = 255;
                }
                tinfo.schema.columns.push_back(c);
            }
            
            ctx.catalog_[stmt.table_name] = tinfo;
            ctx.SaveCatalog("catalog.bin");
            std::cout << "Table '" << stmt.table_name << "' created." << std::endl;
        } else if (stmt.type == StatementType::INSERT) {
            if (ctx.catalog_.find(stmt.table_name) == ctx.catalog_.end()) {
                std::cout << "Table not found." << std::endl;
                continue;
            }
            if (current_txn == nullptr) {
                std::cout << "Error: INSERT must be inside a transaction." << std::endl;
                continue;
            }
            
            TableInfo& tinfo = ctx.catalog_[stmt.table_name];
            Tuple tuple = TupleSerializer::Serialize(stmt.values, tinfo.schema, current_txn->GetTransactionId(), TXN_NULL);
            
            page_id_t page_id = tinfo.last_page_id;
            Page* page = bp.FetchPage(page_id);
            RecordId rid;
            if (!page->InsertTuple(tuple, &rid)) {
                bp.UnpinPage(page_id, false);
                page_id_t new_page_id;
                Page* new_page = bp.NewPage(&new_page_id, page_id);
                if (new_page) {
                    new_page->InsertTuple(tuple, &rid);
                    bp.UnpinPage(new_page_id, true);
                    tinfo.last_page_id = new_page_id;
                    page_id = new_page_id;
                } else {
                    std::cout << "Failed to allocate new page." << std::endl;
                    continue;
                }
            } else {
                bp.UnpinPage(page_id, true);
            }
            
            mvcc_manager.InsertVersion(rid, tuple, current_txn);
            
            if (tinfo.index && !tinfo.schema.columns.empty() && tinfo.schema.columns[0].name == "id" && tinfo.schema.columns[0].type == ColumnType::INT) {
                if (!stmt.values.empty()) {
                    int32_t key = std::stoi(stmt.values[0]);
                    tinfo.index->Insert(key, rid);
                }
            }
            
            LogRecord log_rec;
            log_rec.type = LogRecordType::UPDATE;
            log_rec.txn_id = current_txn->GetTransactionId();
            log_rec.rid = rid;
            log_rec.after_image = tuple;
            log_rec.lsn = logm.AppendLogRecord(log_rec);
            
            ctx.write_sets_[current_txn->GetTransactionId()].push_back(log_rec);
            std::cout << "Inserted 1 row into " << stmt.table_name << "." << std::endl;
        } else if (stmt.type == StatementType::DELETE) {
            if (ctx.catalog_.find(stmt.table_name) == ctx.catalog_.end()) {
                std::cout << "Table not found." << std::endl;
                continue;
            }
            if (current_txn == nullptr) {
                std::cout << "Error: DELETE must be inside a transaction." << std::endl;
                continue;
            }
            
            TableInfo& tinfo = ctx.catalog_[stmt.table_name];
            SeqScan scan(&ctx, stmt.table_name, stmt.where_column, stmt.where_op, stmt.where_value);
            scan.Open();
            Tuple t;
            std::vector<std::pair<RecordId, Tuple>> to_delete;
            while (scan.Next(&t)) {
                to_delete.push_back({t.rid_, t});
            }
            scan.Close();
            
            int count = 0;
            for (const auto& item : to_delete) {
                RecordId rid = item.first;
                Tuple tuple = item.second;
                
                if (mvcc_manager.DeleteVersion(rid, current_txn)) {
                    LogRecord log_rec;
                    log_rec.type = LogRecordType::UPDATE;
                    log_rec.txn_id = current_txn->GetTransactionId();
                    log_rec.rid = rid;
                    log_rec.before_image = tuple;
                    
                    Tuple after_tuple = tuple;
                    after_tuple.SetDeletedBy(current_txn->GetTransactionId());
                    log_rec.after_image = after_tuple;
                    
                    log_rec.lsn = logm.AppendLogRecord(log_rec);
                    ctx.write_sets_[current_txn->GetTransactionId()].push_back(log_rec);
                    count++;
                } else {
                    std::cout << "Write-write conflict detected on row (" << rid.page_id << ", " << rid.slot_id << "). Aborting." << std::endl;
                    current_txn->SetState(TransactionState::ABORTED);
                    break;
                }
            }
            std::cout << "Deleted " << count << " rows from " << stmt.table_name << "." << std::endl;
        } else if (stmt.type == StatementType::SELECT) {
            auto plan = optimizer.Optimize(stmt);
            if (plan) {
                plan->Open();
                Tuple t;
                int count = 0;
                TableInfo& tinfo = ctx.catalog_[stmt.table_name];
                
                Schema out_schema = tinfo.schema;
                if (stmt.has_join && ctx.catalog_.find(stmt.join_table) != ctx.catalog_.end()) {
                    for (const auto& col : ctx.catalog_[stmt.join_table].schema.columns) {
                        out_schema.columns.push_back(col);
                    }
                }
                
                while (plan->Next(&t)) {
                    count++;
                    auto values = TupleSerializer::Deserialize(t, out_schema);
                    for (size_t i = 0; i < values.size(); ++i) {
                        std::cout << values[i] << (i + 1 == values.size() ? "" : " | ");
                    }
                    std::cout << std::endl;
                }
                plan->Close();
                std::cout << count << " rows returned." << std::endl;
            } else {
                std::cout << "Could not generate execution plan." << std::endl;
            }
        } else if (stmt.type == StatementType::EXPLAIN) {
            std::cout << "Execution Plan:" << std::endl;
            std::cout << "  SeqScan on " << stmt.table_name << std::endl;
        } else if (stmt.type == StatementType::SHOW_LOCKS) {
            std::cout << "Lock Table:" << std::endl;
            if (current_txn) {
                for (auto &l : current_txn->GetSharedLockSet()) std::cout << "  S-Lock: " << l << std::endl;
                for (auto &l : current_txn->GetExclusiveLockSet()) std::cout << "  X-Lock: " << l << std::endl;
            }
        } else if (stmt.type == StatementType::SHOW_TRANSACTIONS) {
            if (current_txn) std::cout << "Active Txn: " << current_txn->GetTransactionId() << std::endl;
            else std::cout << "No active transactions." << std::endl;
        } else if (stmt.type == StatementType::SHOW_TABLES) {
            std::cout << "Tables in MiniDB:" << std::endl;
            for (const auto& [name, info] : ctx.catalog_) {
                std::cout << "  - " << name << " (" << info.schema.columns.size() << " columns)" << std::endl;
            }
        } else {
            if (stmt_str.find("VACUUM ") == 0) {
                std::string table_name = stmt_str.substr(7);
                if (ctx.catalog_.find(table_name) != ctx.catalog_.end()) {
                    int32_t min_active_ts = global_ts;
                    for (auto txn_id : lm.GetActiveTransactions()) {
                        if (txn_id < min_active_ts) min_active_ts = txn_id;
                    }
                    int count = mvcc_manager.Vacuum(min_active_ts, ctx.catalog_[table_name]);
                    std::cout << "Vacuumed " << count << " dead versions from " << table_name << "." << std::endl;
                } else {
                    std::cout << "Table not found." << std::endl;
                }
            } else if (stmt_str.find("SHOW VERSIONS ") == 0) {
                std::string table_name = stmt_str.substr(14);
                if (ctx.catalog_.find(table_name) != ctx.catalog_.end()) {
                    ctx.mvcc_manager_ = nullptr; // disable MVCC for physical scan
                    SeqScan scan(&ctx, table_name);
                    scan.Open();
                    Tuple t;
                    while (scan.Next(&t)) {
                        std::cout << "RID(" << t.rid_.page_id << "," << t.rid_.slot_id << "): ";
                        std::cout << "[v created_by=" << t.GetCreatedBy() << " deleted_by=" << t.GetDeletedBy() << "]\n";
                    }
                    scan.Close();
                    ctx.mvcc_manager_ = &mvcc_manager;
                } else {
                    std::cout << "Table not found." << std::endl;
                }
            } else {
                std::cout << "Command parsed but execution not fully wired in REPL." << std::endl;
            }
        }
    }

    if (current_txn) {
        delete current_txn;
    }
    return 0;
}
