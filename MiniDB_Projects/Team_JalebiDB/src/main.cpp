#include "common/config.h"
#include "common/types.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "execution/catalog.h"
#include "execution/executor.h"
#include "parser/parser.h"
#include "optimizer/optimizer.h"
#include "distributed/replica_manager.h"

#include <iostream>
#include <string>
#include <memory>
#include <sstream>
#include <vector>
#include <iomanip>

using namespace minidb;

// Helper to print ASCII table
void PrintResultSet(AbstractExecutor *executor) {
    const Schema &schema = executor->GetOutputSchema();
    const auto &cols = schema.GetColumns();

    // Print headers
    std::cout << "+";
    for (const auto &col : cols) {
        std::cout << std::string(col.name.size() + 4, '-') << "+";
    }
    std::cout << std::endl;

    std::cout << "|";
    for (const auto &col : cols) {
        std::cout << "  " << col.name << "  |";
    }
    std::cout << std::endl;

    std::cout << "+";
    for (const auto &col : cols) {
        std::cout << std::string(col.name.size() + 4, '-') << "+";
    }
    std::cout << std::endl;

    Tuple tuple;
    RID rid;
    int row_count = 0;
    while (executor->Next(tuple, rid)) {
        std::cout << "|";
        for (size_t i = 0; i < cols.size(); ++i) {
            std::string val_str = tuple.GetValues()[i].ToString();
            int pad = cols[i].name.size() + 4 - val_str.size();
            int pad_left = pad / 2;
            int pad_right = pad - pad_left;
            std::cout << std::string(pad_left, ' ') << val_str << std::string(pad_right, ' ') << "|";
        }
        std::cout << std::endl;
        row_count++;
    }

    std::cout << "+";
    for (const auto &col : cols) {
        std::cout << std::string(col.name.size() + 4, '-') << "+";
    }
    std::cout << std::endl;
    std::cout << "(" << row_count << " rows)" << std::endl;
}

// Global engine components
std::unique_ptr<DiskManager> disk_mgr;
std::unique_ptr<LogManager> log_mgr;
std::unique_ptr<BufferPoolManager> bpm;
std::unique_ptr<LockManager> lock_mgr;
std::unique_ptr<TransactionManager> txn_mgr;
std::unique_ptr<Catalog> catalog;
std::unique_ptr<Optimizer> optimizer;
std::unique_ptr<ReplicaManager> replica_mgr;

Transaction *active_txn = nullptr;

void InitializeDatabase(const std::string &db_name, bool is_replica, int replica_port) {
    std::cout << "[System] Initializing database files for: " << db_name << std::endl;
    
    disk_mgr = std::make_unique<DiskManager>(db_name);
    log_mgr = std::make_unique<LogManager>(disk_mgr.get());
    bpm = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, disk_mgr.get(), log_mgr.get());
    lock_mgr = std::make_unique<LockManager>();
    txn_mgr = std::make_unique<TransactionManager>(lock_mgr.get(), log_mgr.get(), bpm.get());
    catalog = std::make_unique<Catalog>();
    optimizer = std::make_unique<Optimizer>(catalog.get());
    replica_mgr = std::make_unique<ReplicaManager>(bpm.get(), log_mgr.get(), disk_mgr.get());

    // Define table schemas
    // Table: users (id INT [PK], name VARCHAR)
    Schema user_schema({
        {"id", TypeId::INT, 4},
        {"name", TypeId::VARCHAR, 32}
    });

    // Table: orders (order_id INT [PK], user_id INT, details VARCHAR)
    Schema order_schema({
        {"order_id", TypeId::INT, 4},
        {"user_id", TypeId::INT, 4},
        {"details", TypeId::VARCHAR, 32}
    });

    // Check if first page exists, otherwise allocate
    page_id_t users_first_page = 0;
    page_id_t orders_first_page = 1;
    page_id_t users_root = 2;
    page_id_t orders_root = 3;

    // Simulate allocating pages on a new database file
    int log_size = disk_mgr->GetLogFileSize();
    if (log_size == 0) {
        // Allocate initial pages
        users_first_page = bpm->NewPage()->GetPageId();
        orders_first_page = bpm->NewPage()->GetPageId();
        users_root = bpm->NewPage()->GetPageId();
        orders_root = bpm->NewPage()->GetPageId();

        // Initialize B+ Trees roots
        BPlusTreeNode u_root(bpm->FetchPage(users_root));
        u_root.SetIsLeaf(true);
        u_root.SetSize(0);
        u_root.SetParentPageId(INVALID_PAGE_ID);
        u_root.SetNextPageId(INVALID_PAGE_ID);
        bpm->UnpinPage(users_root, true);

        BPlusTreeNode o_root(bpm->FetchPage(orders_root));
        o_root.SetIsLeaf(true);
        o_root.SetSize(0);
        o_root.SetParentPageId(INVALID_PAGE_ID);
        o_root.SetNextPageId(INVALID_PAGE_ID);
        bpm->UnpinPage(orders_root, true);

        bpm->UnpinPage(users_first_page, true);
        bpm->UnpinPage(orders_first_page, true);
        
        bpm->FlushAllPages();
        std::cout << "[System] Allocated new database pages and index structures." << std::endl;
    }

    catalog->CreateTable("users", user_schema, users_first_page, users_root, "id");
    catalog->CreateTable("orders", order_schema, orders_first_page, orders_root, "order_id");

    // Set CBO statistics
    optimizer->SetTableStats("users", 100, 5);
    optimizer->SetTableStats("orders", 200, 8);

    // Run Crash Recovery (ARIES) automatically if log has records
    std::cout << "[ARIES] Performing system startup checks..." << std::endl;
    RecoveryManager rec_mgr(disk_mgr.get(), bpm.get());
    rec_mgr.RunRecovery();

    // Start distributed replication
    if (is_replica) {
        replica_mgr->StartReplica("127.0.0.1", replica_port);
    } else {
        replica_mgr->StartPrimary(replica_port);
    }
}

// Executes an SQL-like statement
void ExecuteStatement(const std::string &sql) {
    try {
        SQLStatement stmt = SQLParser::Parse(sql);

        // Check if write command is executed on read-only replica
        if (!replica_mgr->IsPrimary() && stmt.type != SQLStatementType::SELECT) {
            std::cout << "[Distributed Error] Write queries rejected. Replica node is read-only!" << std::endl;
            return;
        }

        // Handle SELECT (Reads)
        if (stmt.type == SQLStatementType::SELECT) {
            std::unique_ptr<PhysicalPlan> plan = optimizer->Optimize(stmt);
            std::cout << "[CBO Optimizer] Selected Physical Plan: " << plan->ToString() << std::endl;

            // Generate executors recursively based on the optimized plan
            std::unique_ptr<AbstractExecutor> exec;
            auto compile_plan = [&](auto &self, const PhysicalPlan *p) -> std::unique_ptr<AbstractExecutor> {
                if (p->type == PlanType::SEQ_SCAN) {
                    return std::make_unique<SeqScanExecutor>(
                        active_txn, catalog->GetTable(p->table_name), p->scan_op, p->scan_col, p->scan_val, bpm.get(), lock_mgr.get()
                    );
                } else if (p->type == PlanType::INDEX_SCAN) {
                    return std::make_unique<IndexScanExecutor>(
                        active_txn, catalog->GetTable(p->table_name), p->scan_col, p->scan_val, bpm.get(), lock_mgr.get()
                    );
                } else {
                    auto left = self(self, p->left_plan.get());
                    auto right = self(self, p->right_plan.get());
                    return std::make_unique<NestedLoopJoinExecutor>(
                        std::move(left), std::move(right), p->join_col_left, p->join_col_right
                    );
                }
            };

            exec = compile_plan(compile_plan, plan.get());
            exec->Init();
            PrintResultSet(exec.get());
        }
        // Handle INSERT
        else if (stmt.type == SQLStatementType::INSERT) {
            bool local_txn = (active_txn == nullptr);
            if (local_txn) {
                active_txn = txn_mgr->Begin();
                std::cout << "[Txn Manager] Started implicit transaction: " << active_txn->GetTxnId() << std::endl;
            }

            TableMetadata *meta = catalog->GetTable(stmt.insert_table);
            if (meta == nullptr) {
                throw std::runtime_error("Table not found: " + stmt.insert_table);
            }

            Tuple t(stmt.insert_values);
            std::string serialized = t.Serialize(meta->schema);

            // Find a page to insert the tuple
            Page *target_page = nullptr;
            page_id_t page_id = meta->first_page_id;
            RID rid;
            
            while (true) {
                target_page = bpm->FetchPage(page_id);
                SlottedPage slotted(target_page);
                if (slotted.InsertTuple(serialized.data(), serialized.size(), rid)) {
                    break;
                }
                
                page_id_t next_page_id = slotted.GetNextPageId();
                if (next_page_id == INVALID_PAGE_ID) {
                    Page *new_page = bpm->NewPage();
                    next_page_id = new_page->GetPageId();
                    slotted.SetNextPageId(next_page_id);
                    bpm->UnpinPage(page_id, true);
                    
                    target_page = new_page;
                    page_id = next_page_id;
                    SlottedPage slotted_new(target_page);
                    slotted_new.InsertTuple(serialized.data(), serialized.size(), rid);
                    break;
                } else {
                    bpm->UnpinPage(page_id, false);
                    page_id = next_page_id;
                }
            }

            // Strict 2PL: acquire Exclusive lock on the inserted RID
            if (lock_mgr) {
                if (!lock_mgr->AcquireExclusive(active_txn, rid)) {
                    bpm->UnpinPage(rid.page_id, false);
                    throw std::runtime_error("Exclusive lock acquisition failed for insert RID: " + rid.ToString());
                }
            }

            // Write WAL insert log record
            if (log_mgr) {
                LogRecord rec(active_txn->GetTxnId(), active_txn->GetPrevLSN(), LogRecordType::INSERT, rid, serialized);
                lsn_t lsn = log_mgr->AppendLogRecord(&rec);
                active_txn->SetPrevLSN(lsn);
                target_page->SetLSN(lsn);
            }

            bpm->UnpinPage(rid.page_id, true);

            // Insert into primary key B+ Tree index
            if (meta->root_page_id != INVALID_PAGE_ID) {
                BPlusTree tree(meta->root_page_id, bpm.get());
                // Find primary key column value (assume first column)
                int pk_val = stmt.insert_values[0].GetInt();
                tree.Insert(pk_val, rid);
            }

            std::cout << "[Executor] Inserted tuple " << stmt.insert_values[0].ToString() << " successfully into RID " << rid.ToString() << std::endl;

            if (local_txn) {
                txn_mgr->Commit(active_txn);
                std::cout << "[Txn Manager] Committed implicit transaction." << std::endl;
                active_txn = nullptr;
            }
        }
        // Handle DELETE
        else if (stmt.type == SQLStatementType::DELETE) {
            bool local_txn = (active_txn == nullptr);
            if (local_txn) {
                active_txn = txn_mgr->Begin();
                std::cout << "[Txn Manager] Started implicit transaction: " << active_txn->GetTxnId() << std::endl;
            }

            TableMetadata *meta = catalog->GetTable(stmt.delete_table);
            if (meta == nullptr) {
                throw std::runtime_error("Table not found: " + stmt.delete_table);
            }

            // Scan to locate matches
            SeqScanExecutor scanner(
                active_txn, meta, stmt.where_op, stmt.where_col, stmt.where_val, bpm.get(), lock_mgr.get()
            );
            scanner.Init();

            Tuple t;
            RID rid;
            std::vector<std::pair<Tuple, RID>> victims;
            while (scanner.Next(t, rid)) {
                victims.push_back({t, rid});
            }

            for (const auto &pair : victims) {
                RID v_rid = pair.second;
                Tuple v_tuple = pair.first;
                std::string serialized = v_tuple.Serialize(meta->schema);

                // Strict 2PL: acquire Exclusive lock before deleting
                if (lock_mgr) {
                    if (!lock_mgr->AcquireExclusive(active_txn, v_rid)) {
                        throw std::runtime_error("Exclusive lock acquisition failed for delete RID " + v_rid.ToString());
                    }
                }

                // Write WAL delete record
                if (log_mgr) {
                    LogRecord rec(active_txn->GetTxnId(), active_txn->GetPrevLSN(), LogRecordType::DELETE, v_rid, serialized);
                    lsn_t lsn = log_mgr->AppendLogRecord(&rec);
                    active_txn->SetPrevLSN(lsn);

                    Page *page = bpm->FetchPage(v_rid.page_id);
                    page->SetLSN(lsn);
                    SlottedPage slotted(page);
                    slotted.DeleteTuple(v_rid.slot_id);
                    bpm->UnpinPage(v_rid.page_id, true);
                }

                // Delete key from B+ tree index
                if (meta->root_page_id != INVALID_PAGE_ID) {
                    BPlusTree tree(meta->root_page_id, bpm.get());
                    int pk_val = v_tuple.GetValues()[0].GetInt();
                    tree.Delete(pk_val);
                }

                std::cout << "[Executor] Deleted tuple with key " << v_tuple.GetValues()[0].ToString() << " from RID " << v_rid.ToString() << std::endl;
            }

            if (local_txn) {
                txn_mgr->Commit(active_txn);
                std::cout << "[Txn Manager] Committed implicit transaction." << std::endl;
                active_txn = nullptr;
            }
        }
    }
    catch (const std::exception &ex) {
        std::cerr << "\n[Query Execution Error] " << ex.what() << std::endl;
        if (active_txn) {
            std::cout << "[Txn Manager] Transaction " << active_txn->GetTxnId() << " aborted due to error." << std::endl;
            txn_mgr->Abort(active_txn);
            active_txn = nullptr;
        }
    }
}

int main(int argc, char **argv) {
    std::cout << "==========================================================" << std::endl;
    std::cout << "      MINIDB RELATIONAL ENGINE — CAPSTONE SYSTEM CLI      " << std::endl;
    std::cout << "==========================================================" << std::endl;

    bool is_replica = false;
    int port = 9000;
    std::string db_file = "minidb_primary.db";

    if (argc >= 2) {
        std::string mode = argv[1];
        if (mode == "--replica") {
            is_replica = true;
            db_file = "minidb_replica.db";
            if (argc >= 3) {
                port = std::stoi(argv[2]);
            }
        } else {
            port = std::stoi(argv[1]);
        }
    }

    InitializeDatabase(db_file, is_replica, port);

    std::cout << "\nInteractive SQL shell started. Type /help for assistance." << std::endl;
    std::cout << "Node mode: " << (replica_mgr->IsPrimary() ? "PRIMARY (Read-Write)" : "REPLICA (Read-Only)") << std::endl;
    std::cout << "==========================================================" << std::endl;

    std::string line;
    while (true) {
        std::cout << (active_txn ? "minidb (txn " + std::to_string(active_txn->GetTxnId()) + ")> " : "minidb> ") << std::flush;
        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line.empty()) continue;

        if (line == "/exit" || line == "/quit") {
            std::cout << "[System] Flushing dirty pages and shutting down..." << std::endl;
            break;
        } else if (line == "/help") {
            std::cout << "Available Commands:\n"
                      << "  /begin                  - Start a new transaction\n"
                      << "  /commit                 - Commit the current transaction\n"
                      << "  /abort                  - Abort and roll back the transaction\n"
                      << "  /crash                  - Simulate a hard system crash (closes file without flush)\n"
                      << "  /print_tree             - Display the current B+ Tree indexes\n"
                      << "  /exit                   - Shutdown database and exit\n"
                      << "\nExample SQL Queries:\n"
                      << "  INSERT INTO users VALUES (1, Alice)\n"
                      << "  INSERT INTO orders VALUES (101, 1, MacBook)\n"
                      << "  SELECT id, name FROM users\n"
                      << "  SELECT order_id, user_id, details FROM orders WHERE user_id = 1\n"
                      << "  SELECT id, name, details FROM users JOIN orders ON users.id = orders.user_id WHERE id = 1\n"
                      << "  DELETE FROM users WHERE id = 1\n";
        } else if (line == "/begin") {
            if (active_txn) {
                std::cout << "[Txn Manager] Transaction already active: " << active_txn->GetTxnId() << std::endl;
            } else {
                active_txn = txn_mgr->Begin();
                std::cout << "[Txn Manager] Started transaction: " << active_txn->GetTxnId() << std::endl;
            }
        } else if (line == "/commit") {
            if (!active_txn) {
                std::cout << "[Txn Manager] No active transaction." << std::endl;
            } else {
                txn_mgr->Commit(active_txn);
                std::cout << "[Txn Manager] Transaction committed successfully." << std::endl;
                active_txn = nullptr;
            }
        } else if (line == "/abort") {
            if (!active_txn) {
                std::cout << "[Txn Manager] No active transaction." << std::endl;
            } else {
                txn_mgr->Abort(active_txn);
                std::cout << "[Txn Manager] Transaction aborted. All changes rolled back." << std::endl;
                active_txn = nullptr;
            }
        } else if (line == "/crash") {
            std::cout << "\n[System] !!! HARD CRASH SIMULATED !!!" << std::endl;
            std::cout << "[System] Closing file streams without flushing buffer pool dirty pages." << std::endl;
            disk_mgr->ShutDown();
            // Force exit to prevent destructor page flush
            std::_Exit(0);
        } else if (line == "/print_tree") {
            std::cout << "Users B+ Tree:" << std::endl;
            BPlusTree u_tree(catalog->GetTable("users")->root_page_id, bpm.get());
            u_tree.PrintTree();

            std::cout << "\nOrders B+ Tree:" << std::endl;
            BPlusTree o_tree(catalog->GetTable("orders")->root_page_id, bpm.get());
            o_tree.PrintTree();
        } else {
            ExecuteStatement(line);
        }
        std::cout << std::endl;
    }

    replica_mgr->Stop();
    bpm->FlushAllPages();
    disk_mgr->ShutDown();
    return 0;
}
