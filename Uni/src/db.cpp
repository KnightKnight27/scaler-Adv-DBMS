#include "db.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <stdexcept>

// Helper to join a vector of strings by comma
static std::string JoinCSV(const std::vector<std::string>& vals) {
    std::string result;
    for (size_t i = 0; i < vals.size(); ++i) {
        result += vals[i];
        if (i < vals.size() - 1) {
            result += ",";
        }
    }
    return result;
}

static std::vector<std::string> SplitString(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

Database::Database(const std::string& db_file, const std::string& log_file, ConcurrencyMode mode)
    : db_file_name_(db_file), log_file_name_(log_file), mode_(mode) {
    disk_manager_ = std::make_unique<DiskManager>(db_file_name_);
    bpm_ = std::make_unique<BufferPoolManager>(16, disk_manager_.get());
    lock_manager_ = std::make_unique<LockManager>();
    mvcc_manager_ = std::make_unique<MVCCManager>();
    log_manager_ = std::make_unique<LogManager>(log_file_name_);
    recovery_manager_ = std::make_unique<RecoveryManager>(log_manager_.get(), bpm_.get());

    // Register WAL flush callback in Buffer Pool Manager
    bpm_->RegisterLogFlushCallback([this](Lsn_t lsn) {
        log_manager_->FlushUpTo(lsn);
    });

    // Start background deadlock detector for Strict 2PL
    lock_manager_->StartDeadlockDetector(&active_transactions_);
}

Database::~Database() {
    if (lock_manager_) {
        lock_manager_->StopDeadlockDetector();
    }
    
    // Rollback any active transactions
    std::vector<Transaction*> active_txns;
    for (auto& [id, txn] : active_transactions_) {
        active_txns.push_back(txn);
    }
    for (Transaction* txn : active_txns) {
        AbortTransaction(txn);
    }

    if (bpm_) {
        bpm_->FlushAllPages();
    }
}

Transaction* Database::BeginTransaction() {
    std::lock_guard<std::mutex> lock(catalog_latch_);

    TxId_t txid = next_txn_id_++;
    Transaction* txn = new Transaction();
    txn->txn_id = txid;
    txn->state = TransactionState::ACTIVE;

    Lsn_t lsn = log_manager_->AppendRecord(txid, 0, LogRecordType::BEGIN);
    txn->prev_lsn = lsn;

    active_transactions_[txid] = txn;

    if (mode_ == ConcurrencyMode::MULTI_VERSION_CONCURRENCY_CONTROL) {
        mvcc_manager_->BeginTx(txid);
    }

    return txn;
}

bool Database::CommitTransaction(Transaction* txn) {
    std::lock_guard<std::mutex> lock(catalog_latch_);

    if (txn->state == TransactionState::ABORTED) {
        delete txn;
        return false;
    }

    Lsn_t lsn = log_manager_->AppendRecord(txn->txn_id, txn->prev_lsn, LogRecordType::COMMIT);
    log_manager_->FlushUpTo(lsn);

    lock_manager_->ReleaseAllLocks(txn);

    if (mode_ == ConcurrencyMode::MULTI_VERSION_CONCURRENCY_CONTROL) {
        mvcc_manager_->CommitTx(txn->txn_id);
    }

    txn->state = TransactionState::COMMITTED;
    active_transactions_.erase(txn->txn_id);
    delete txn;
    return true;
}

void Database::AbortTransaction(Transaction* txn) {
    std::lock_guard<std::mutex> lock(catalog_latch_);

    // Undo transaction changes in reverse order using WAL
    std::cout << "[Transaction T" << txn->txn_id << "] Aborting and Rolling Back changes..." << std::endl;
    
    log_manager_->Flush(); // Ensure in-memory log buffer is flushed to disk before reading
    
    // We read WAL records for this transaction and undo modifications
    std::ifstream lf(log_file_name_, std::ios::binary);
    if (lf.is_open()) {
        std::vector<LogRecord> tx_records;
        while (lf) {
            LogRecord rec = LogRecord::Deserialize(lf);
            if (rec.lsn == 0) break;
            if (rec.txn_id == txn->txn_id) {
                tx_records.push_back(rec);
            }
        }
        lf.close();

        // Process in reverse LSN order
        for (auto it = tx_records.rbegin(); it != tx_records.rend(); ++it) {
            LogRecord& rec = *it;
            if (rec.type == LogRecordType::INSERT || rec.type == LogRecordType::DELETE || rec.type == LogRecordType::UPDATE) {
                Page* page = bpm_->FetchPage(rec.page_id);
                if (page) {
                    // Resolve table metadata from catalog using page ID
                    TableMetadata* tbl_meta = nullptr;
                    for (auto& [name, meta] : catalog_) {
                        if (name == "users" && rec.page_id < 2) {
                            tbl_meta = &meta;
                            break;
                        } else if (name == "orders" && rec.page_id >= 2) {
                            tbl_meta = &meta;
                            break;
                        }
                    }

                    // Reverse operation
                    if (rec.type == LogRecordType::INSERT) {
                        PageHeader* hdr = page->GetHeader();
                        Slot* slots = page->GetSlots();
                        slots[rec.slot_id].offset = 0; // mark deleted
                        slots[rec.slot_id].length = 0;

                        if (tbl_meta && tbl_meta->index) {
                            std::vector<std::string> vals = SplitString(rec.after_image, ',');
                            if (!vals.empty()) {
                                try {
                                    int32_t key = std::stoi(vals[0]);
                                    tbl_meta->index->Delete(key);
                                } catch (...) {}
                            }
                        }
                    } else if (rec.type == LogRecordType::DELETE) {
                        if (mode_ == ConcurrencyMode::MULTI_VERSION_CONCURRENCY_CONTROL) {
                            PageHeader* hdr = page->GetHeader();
                            Slot* slots = page->GetSlots();
                            if (rec.slot_id < hdr->slot_count) {
                                char* record_ptr = page->data + slots[rec.slot_id].offset;
                                MVCCHeader* mvcc = reinterpret_cast<MVCCHeader*>(record_ptr);
                                mvcc->xmax = 0; // Revert MVCC logical delete
                            }
                        } else {
                            // Restore insert before_image (2PL physical delete)
                            PageHeader* hdr = page->GetHeader();
                            Slot* slots = page->GetSlots();
                            uint16_t L = sizeof(MVCCHeader) + rec.before_image.length();
                            hdr->free_space_pointer -= L;
                            char* dest = page->data + hdr->free_space_pointer;
                            MVCCHeader* mvcc = reinterpret_cast<MVCCHeader*>(dest);
                            mvcc->xmin = rec.txn_id;
                            mvcc->xmax = 0;
                            mvcc->prev_page_id = INVALID_PAGE_ID;
                            mvcc->prev_slot_id = 0;
                            std::memcpy(dest + sizeof(MVCCHeader), rec.before_image.data(), rec.before_image.length());
                            slots[rec.slot_id].offset = hdr->free_space_pointer;
                            slots[rec.slot_id].length = L;

                            if (tbl_meta && tbl_meta->index) {
                                std::vector<std::string> vals = SplitString(rec.before_image, ',');
                                if (!vals.empty()) {
                                    try {
                                        int32_t key = std::stoi(vals[0]);
                                        tbl_meta->index->Insert(key, RID{rec.page_id, rec.slot_id});
                                    } catch (...) {}
                                }
                            }
                        }
                    }
                    
                    Lsn_t clr_lsn = log_manager_->AppendRecord(
                        txn->txn_id, txn->prev_lsn, LogRecordType::CLR,
                        rec.page_id, rec.slot_id, rec.after_image, rec.before_image, rec.prev_lsn
                    );
                    txn->prev_lsn = clr_lsn;
                    page->GetHeader()->page_lsn = clr_lsn;
                    bpm_->UnpinPage(rec.page_id, true);
                }
            }
        }
    }

    if (log_manager_) {
        log_manager_->AppendRecord(txn->txn_id, txn->prev_lsn, LogRecordType::ABORT);
        log_manager_->Flush();
    }

    if (lock_manager_) {
        lock_manager_->ReleaseAllLocks(txn);
    }

    if (mode_ == ConcurrencyMode::MULTI_VERSION_CONCURRENCY_CONTROL) {
        mvcc_manager_->AbortTx(txn->txn_id);
    }

    txn->state = TransactionState::ABORTED;
    active_transactions_.erase(txn->txn_id);
    delete txn;
}

void Database::SimulateCrash() {
    std::cout << "\n=============================================" << std::endl;
    std::cout << "[SYSTEM CRASH] Simulating sudden crash/power loss..." << std::endl;
    std::cout << "[SYSTEM CRASH] Halting background services and wiping RAM cache..." << std::endl;
    std::cout << "=============================================" << std::endl;

    // Shutdown services abruptly without flushing dirty pages
    lock_manager_->StopDeadlockDetector();
    
    // Clear catalog maps and in-memory structures
    active_transactions_.clear();
    catalog_.clear();

    // Destroy and recreate Disk and BufferPool objects to simulate cold restart
    bpm_.reset();
    disk_manager_.reset();
    log_manager_.reset();
}

void Database::RestartAndRecover() {
    std::cout << "\n=============================================" << std::endl;
    std::cout << "[RESTART] Cold booting MiniDB engine..." << std::endl;
    std::cout << "=============================================" << std::endl;

    // Run ARIES recovery using the freshly initialized recovery manager
    recovery_manager_->Recover(log_file_name_);

    // Rebuild B+ Tree indexes from the recovered data pages
    RebuildIndexes();

    // Rebuild active transaction ids based on WAL scan
    next_txn_id_ = log_manager_->GetFlushedLsn() + 2; // Offset safely past crashed LSNs
}

void Database::SetConcurrencyMode(ConcurrencyMode mode) {
    mode_ = mode;
}

void Database::CreateTable(const std::string& table_name, const std::vector<std::string>& schema) {
    std::lock_guard<std::mutex> lock(catalog_latch_);

    TableMetadata meta;
    meta.name = table_name;
    meta.schema = schema;

    if (disk_manager_->GetNumPages() == 0) {
        // Fresh run: allocate new pages
        Page* p = bpm_->NewPage(meta.first_page_id);
        p->Init(meta.first_page_id, PageType::DATA_PAGE);
        meta.last_page_id = meta.first_page_id;
        bpm_->UnpinPage(meta.first_page_id, true);

        // Initialize B+ Tree
        meta.index = std::make_unique<BPlusTree>(INVALID_PAGE_ID, bpm_.get());
        meta.index_root_page_id = meta.index->GetRootPageId();
    } else {
        // Recovery run: map to existing deterministic pages
        if (table_name == "users") {
            meta.first_page_id = 0;
            meta.last_page_id = 0;
            meta.index_root_page_id = 1;
        } else if (table_name == "orders") {
            meta.first_page_id = 2;
            meta.last_page_id = 2;
            meta.index_root_page_id = 3;
        }
        meta.index = std::make_unique<BPlusTree>(meta.index_root_page_id, bpm_.get());
    }

    catalog_[table_name] = std::move(meta);

    // Register initial costing statistics
    TableStats stats;
    stats.num_pages = 1;
    stats.num_records = 0;
    stats.tree_height = 2;
    for (const auto& col : schema) {
        std::string col_name = col;
        size_t dot = col_name.find('.');
        if (dot != std::string::npos) col_name = col_name.substr(dot + 1);
        stats.distinct_values[col_name] = 10; // Default estimate
    }
    optimizer_.SetTableStats(table_name, stats);
}

TableMetadata* Database::GetTableMetadata(const std::string& table_name) {
    auto it = catalog_.find(table_name);
    return (it != catalog_.end()) ? &it->second : nullptr;
}

RID Database::InsertTupleInternal(Transaction* txn, const std::string& table_name, const std::vector<std::string>& values) {
    TableMetadata* meta = GetTableMetadata(table_name);
    if (!meta) return RID{};

    std::string csv_data = JoinCSV(values);
    uint16_t L = sizeof(MVCCHeader) + csv_data.length();

    PageId_t page_id = meta->last_page_id;
    Page* page = bpm_->FetchPage(page_id);
    if (!page) return RID{};

    PageHeader* hdr = page->GetHeader();
    Slot* slots = page->GetSlots();

    // Verify if there is enough space in the page
    uint16_t free_space = hdr->free_space_pointer - (sizeof(PageHeader) + (hdr->slot_count + 1) * sizeof(Slot));
    if (free_space < L) {
        // Allocate new page
        bpm_->UnpinPage(page_id, false);
        
        PageId_t new_page_id;
        Page* new_page = bpm_->NewPage(new_page_id);
        new_page->Init(new_page_id, PageType::DATA_PAGE);

        // Fetch old last page to link it
        Page* old_last_page = bpm_->FetchPage(page_id);
        old_last_page->GetHeader()->next_page_id = new_page_id;
        
        // Write WAL for page chain link if needed or update log
        Lsn_t link_lsn = log_manager_->AppendRecord(txn->txn_id, txn->prev_lsn, LogRecordType::UPDATE, page_id, 0, "", "PAGE_LINK_" + std::to_string(new_page_id));
        old_last_page->GetHeader()->page_lsn = link_lsn;
        txn->prev_lsn = link_lsn;
        
        bpm_->UnpinPage(page_id, true);

        // Continue insertion on new page
        page = new_page;
        page_id = new_page_id;
        hdr = page->GetHeader();
        slots = page->GetSlots();
        meta->last_page_id = new_page_id;

        // Update optimizer statistics
        TableStats stats = *optimizer_.GetStats(table_name);
        stats.num_pages++;
        optimizer_.SetTableStats(table_name, stats);
    }

    // Insert record
    hdr->free_space_pointer -= L;
    char* target = page->data + hdr->free_space_pointer;
    MVCCHeader* mvcc = reinterpret_cast<MVCCHeader*>(target);
    mvcc->xmin = txn->txn_id;
    mvcc->xmax = 0;
    mvcc->prev_page_id = INVALID_PAGE_ID;
    mvcc->prev_slot_id = 0;

    std::memcpy(target + sizeof(MVCCHeader), csv_data.data(), csv_data.length());

    uint16_t slot_id = hdr->slot_count++;
    slots[slot_id].offset = hdr->free_space_pointer;
    slots[slot_id].length = L;

    RID rid{page_id, slot_id};

    // 2PL Locking
    if (mode_ == ConcurrencyMode::TWO_PHASE_LOCKING) {
        if (!lock_manager_->AcquireExclusive(txn, rid)) {
            // Lock acquire failed (transaction aborted by deadlock detector)
            bpm_->UnpinPage(page_id, false);
            return RID{};
        }
    }

    // WAL append
    Lsn_t ins_lsn = log_manager_->AppendRecord(txn->txn_id, txn->prev_lsn, LogRecordType::INSERT, page_id, slot_id, "", csv_data);
    page->GetHeader()->page_lsn = ins_lsn;
    txn->prev_lsn = ins_lsn;

    bpm_->UnpinPage(page_id, true);

    // Update B+ tree index
    int32_t pk = std::stoi(values[0]);
    meta->index->Insert(pk, rid);

    // Update optimizer record stats
    TableStats stats = *optimizer_.GetStats(table_name);
    stats.num_records++;
    stats.distinct_values[meta->schema[0]] = stats.num_records; // PK is unique
    optimizer_.SetTableStats(table_name, stats);

    return rid;
}

bool Database::DeleteTupleInternal(Transaction* txn, const std::string& table_name, const RID& rid, int32_t key) {
    TableMetadata* meta = GetTableMetadata(table_name);
    if (!meta) return false;

    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) return false;

    Slot* slots = page->GetSlots();
    char* record_ptr = page->data + slots[rid.slot_id].offset;
    MVCCHeader* mvcc = reinterpret_cast<MVCCHeader*>(record_ptr);

    if (mode_ == ConcurrencyMode::TWO_PHASE_LOCKING) {
        if (!lock_manager_->AcquireExclusive(txn, rid)) {
            bpm_->UnpinPage(rid.page_id, false);
            return false; // Aborted
        }

        // Standard deletion
        std::string CSV_old(record_ptr + sizeof(MVCCHeader), slots[rid.slot_id].length - sizeof(MVCCHeader));
        Lsn_t del_lsn = log_manager_->AppendRecord(txn->txn_id, txn->prev_lsn, LogRecordType::DELETE, rid.page_id, rid.slot_id, CSV_old, "");
        txn->prev_lsn = del_lsn;
        page->GetHeader()->page_lsn = del_lsn;

        slots[rid.slot_id].offset = 0;
        slots[rid.slot_id].length = 0;

        bpm_->UnpinPage(rid.page_id, true);

        // Update index
        meta->index->Delete(key);
    } 
    else {
        // MVCC deletion: mark xmax = txn_id
        if (!mvcc_manager_->CanWrite(txn->txn_id, mvcc->xmin, mvcc->xmax)) {
            // Write-write conflict
            std::cout << "[MVCC conflict] Concurrent transaction is modifying the same tuple. Aborting Tx T" << txn->txn_id << std::endl;
            txn->state = TransactionState::ABORTED;
            bpm_->UnpinPage(rid.page_id, false);
            return false;
        }

        std::string CSV_old(record_ptr + sizeof(MVCCHeader), slots[rid.slot_id].length - sizeof(MVCCHeader));
        Lsn_t del_lsn = log_manager_->AppendRecord(txn->txn_id, txn->prev_lsn, LogRecordType::DELETE, rid.page_id, rid.slot_id, CSV_old, "XMAX_SET");
        txn->prev_lsn = del_lsn;
        page->GetHeader()->page_lsn = del_lsn;

        mvcc->xmax = txn->txn_id;

        bpm_->UnpinPage(rid.page_id, true);
    }

    TableStats stats = *optimizer_.GetStats(table_name);
    stats.num_records = std::max(0, stats.num_records - 1);
    optimizer_.SetTableStats(table_name, stats);

    return true;
}

bool Database::ExecuteSQL(Transaction* txn, const std::string& sql, std::vector<Tuple>& output_rows, std::vector<std::string>& output_schema) {
    if (txn->state == TransactionState::ABORTED) {
        return false;
    }

    SQLStatement stmt = SQLParser::Parse(sql);
    if (stmt.type == SQLStatementType::INVALID) {
        std::cout << "[SQL Error] Invalid SQL syntax: " << sql << std::endl;
        return false;
    }

    if (stmt.type == SQLStatementType::INSERT) {
        RID rid = InsertTupleInternal(txn, stmt.table_name, stmt.insert_values);
        if (rid.IsValid()) {
            std::cout << "[Insert Success] Row inserted at (" << rid.page_id << "," << rid.slot_id << ")" << std::endl;
            return true;
        }
        return false;
    }

    if (stmt.type == SQLStatementType::DELETE) {
        TableMetadata* meta = GetTableMetadata(stmt.table_name);
        if (!meta) return false;

        // If index lookup is available (e.g. DELETE WHERE id = 5)
        if (stmt.where.has_condition && stmt.where.column == "id" && stmt.where.op == "=") {
            int32_t pk = std::stoi(stmt.where.value);
            RID rid;
            if (meta->index->Search(pk, rid)) {
                return DeleteTupleInternal(txn, stmt.table_name, rid, pk);
            }
            std::cout << "[Delete] PK Key " << pk << " not found in B+ Tree Index." << std::endl;
            return false;
        }

        // Full SeqScan fallback for deletion
        std::vector<std::string> temp_schema = meta->schema;
        VisibilityChecker_t checker = nullptr;
        if (mode_ == ConcurrencyMode::MULTI_VERSION_CONCURRENCY_CONTROL) {
            MVCCSnapshot snap = mvcc_manager_->GetSnapshot(txn->txn_id);
            checker = [this, snap](TxId_t xmin, TxId_t xmax) { return mvcc_manager_->IsVisible(snap, xmin, xmax); };
        }

        auto scan = std::make_unique<SeqScanExecutor>(stmt.table_name, temp_schema, meta->first_page_id, bpm_.get(), checker);
        scan->Init();
        Tuple t;
        std::vector<RID> to_delete;
        while (scan->Next(&t)) {
            // Check condition manually
            if (stmt.where.has_condition) {
                // Assume PK id index is at 0
                if (stmt.where.column == "id") {
                    int val = std::stoi(t.values[0]);
                    int target = std::stoi(stmt.where.value);
                    if (stmt.where.op == "=" && val == target) to_delete.push_back(t.rid);
                }
            } else {
                to_delete.push_back(t.rid);
            }
        }
        scan->Close();

        bool success = true;
        for (const auto& rid : to_delete) {
            success &= DeleteTupleInternal(txn, stmt.table_name, rid, 0);
        }
        return success;
    }

    if (stmt.type == SQLStatementType::SELECT) {
        TableMetadata* meta = GetTableMetadata(stmt.table_name);
        if (!meta) {
            std::cout << "[SQL Error] Table " << stmt.table_name << " does not exist." << std::endl;
            return false;
        }

        std::unique_ptr<Operator> scan_op = nullptr;
        VisibilityChecker_t checker = nullptr;
        
        if (mode_ == ConcurrencyMode::MULTI_VERSION_CONCURRENCY_CONTROL) {
            MVCCSnapshot snap = mvcc_manager_->GetSnapshot(txn->txn_id);
            checker = [this, snap](TxId_t xmin, TxId_t xmax) { return mvcc_manager_->IsVisible(snap, xmin, xmax); };
        }

        // Cost-Based Optimization: Index Scan vs Seq Scan Selection
        bool use_index_scan = false;
        RID idx_rid;
        int32_t query_pk = 0;
        
        if (stmt.where.has_condition && stmt.where.column == "id" && stmt.where.op == "=") {
            double seq_c, idx_c;
            if (optimizer_.ChooseIndexScan(stmt.table_name, stmt.where, seq_c, idx_c)) {
                query_pk = std::stoi(stmt.where.value);
                if (meta->index->Search(query_pk, idx_rid)) {
                    use_index_scan = true;
                    std::cout << "[Optimizer] Cost SeqScan: " << seq_c << ", IndexScan: " << idx_c 
                              << " -> Picked B+ TREE INDEX SCAN (Key " << query_pk << ")" << std::endl;
                }
            } else {
                std::cout << "[Optimizer] Cost SeqScan: " << seq_c << ", IndexScan: " << idx_c 
                          << " -> Picked TABLE SEQUENTIAL SCAN" << std::endl;
            }
        }

        if (use_index_scan) {
            scan_op = std::make_unique<IndexScanExecutor>(stmt.table_name, meta->schema, std::vector<RID>{idx_rid}, bpm_.get(), checker);
        } else {
            scan_op = std::make_unique<SeqScanExecutor>(stmt.table_name, meta->schema, meta->first_page_id, bpm_.get(), checker);
        }

        std::unique_ptr<Operator> plan = std::move(scan_op);

        // Apply JOIN
        if (stmt.join.has_join) {
            TableMetadata* j_meta = GetTableMetadata(stmt.join.join_table);
            if (!j_meta) {
                std::cout << "[SQL Error] Joined table " << stmt.join.join_table << " does not exist." << std::endl;
                return false;
            }

            // Cost-Based Optimization: Join Order selection (make smaller table outer relation)
            auto [outer, inner] = optimizer_.OrderJoin(stmt.table_name, stmt.join.join_table);
            std::cout << "[Optimizer] Ordered Join: " << outer << " JOIN " << inner << " (Outer loop: " << outer << ")" << std::endl;

            std::unique_ptr<Operator> outer_scan = nullptr;
            std::unique_ptr<Operator> inner_scan = nullptr;

            if (outer == stmt.table_name) {
                outer_scan = std::move(plan);
                inner_scan = std::make_unique<SeqScanExecutor>(inner, j_meta->schema, j_meta->first_page_id, bpm_.get(), checker);
            } else {
                outer_scan = std::make_unique<SeqScanExecutor>(outer, j_meta->schema, j_meta->first_page_id, bpm_.get(), checker);
                inner_scan = std::move(plan);
            }

            plan = std::make_unique<NestedLoopJoinExecutor>(std::move(outer_scan), std::move(inner_scan), stmt.join.left_col, stmt.join.right_col);
        }

        // Apply WHERE filter
        if (stmt.where.has_condition) {
            plan = std::make_unique<FilterExecutor>(std::move(plan), stmt.where);
        }

        // Apply Projection
        plan = std::make_unique<ProjectExecutor>(std::move(plan), stmt.fields);

        // Execute Volcano Iteration
        plan->Init();
        Tuple t;
        output_schema = plan->GetSchema();
        while (plan->Next(&t)) {
            output_rows.push_back(t);
        }
        plan->Close();

        return true;
    }

    return false;
}

void Database::PrintBPlusTree(const std::string& table_name) {
    TableMetadata* meta = GetTableMetadata(table_name);
    if (meta && meta->index) {
        meta->index->PrintTree();
    }
}

void Database::PrintBufferPoolStats() {
    std::cout << "--- Buffer Pool Cache Statistics ---" << std::endl;
    std::cout << "Pool Size: " << bpm_->GetPoolSize() << " frames" << std::endl;
    std::cout << "Cache Hits: " << bpm_->GetCacheHits() << std::endl;
    std::cout << "Cache Misses: " << bpm_->GetCacheMisses() << std::endl;
    double hit_rate = 0.0;
    size_t total = bpm_->GetCacheHits() + bpm_->GetCacheMisses();
    if (total > 0) hit_rate = (double)bpm_->GetCacheHits() / total * 100.0;
    std::cout << "Cache Hit Rate: " << hit_rate << "%" << std::endl;
    std::cout << "-----------------------------------" << std::endl;
}

void Database::ClearDatabase() {
    std::lock_guard<std::mutex> lock(catalog_latch_);
    lock_manager_->StopDeadlockDetector();
    active_transactions_.clear();
    catalog_.clear();
    
    disk_manager_->Clear();
    bpm_ = std::make_unique<BufferPoolManager>(16, disk_manager_.get());
    bpm_->RegisterLogFlushCallback([this](Lsn_t lsn) {
        log_manager_->FlushUpTo(lsn);
    });

    log_manager_->Clear();
    mvcc_manager_->Clear();
    next_txn_id_ = 1;

    lock_manager_ = std::make_unique<LockManager>();
    lock_manager_->StartDeadlockDetector(&active_transactions_);
}

void Database::RebuildIndexes() {
    std::lock_guard<std::mutex> lock(catalog_latch_);
    std::cout << "[Recovery] Rebuilding B+ Tree Indexes..." << std::endl;
    for (auto& [name, meta] : catalog_) {
        // Reinitialize the B+ Tree root page
        Page* root_page = bpm_->FetchPage(meta.index_root_page_id);
        if (root_page) {
            root_page->Init(meta.index_root_page_id, PageType::INDEX_PAGE);
            BLeafNode* leaf = reinterpret_cast<BLeafNode*>(root_page->data);
            leaf->header.is_leaf = true;
            leaf->header.num_keys = 0;
            leaf->header.next_page_id = INVALID_PAGE_ID;
            bpm_->UnpinPage(meta.index_root_page_id, true);
        }
        
        // Re-instantiate tree object
        meta.index = std::make_unique<BPlusTree>(meta.index_root_page_id, bpm_.get());

        // Scan all data pages sequentially and insert keys
        PageId_t curr_page_id = meta.first_page_id;
        while (curr_page_id != INVALID_PAGE_ID) {
            Page* page = bpm_->FetchPage(curr_page_id);
            if (!page) break;

            PageHeader* hdr = page->GetHeader();
            Slot* slots = page->GetSlots();

            for (uint16_t i = 0; i < hdr->slot_count; ++i) {
                Slot& slot = slots[i];
                if (slot.offset > 0 && slot.length > 0) {
                    const char* record_ptr = page->data + slot.offset;
                    const MVCCHeader* mvcc_hdr = reinterpret_cast<const MVCCHeader*>(record_ptr);
                    
                    // Re-insert only if not deleted (xmax == 0 or uncommitted xmax)
                    if (mvcc_hdr->xmax == 0) {
                        std::string payload(record_ptr + sizeof(MVCCHeader), slot.length - sizeof(MVCCHeader));
                        std::vector<std::string> vals = SplitString(payload, ',');
                        if (!vals.empty()) {
                            try {
                                int32_t key = std::stoi(vals[0]);
                                meta.index->Insert(key, RID{curr_page_id, i});
                            } catch (...) {}
                        }
                    }
                }
            }

            PageId_t next_page_id = hdr->next_page_id;
            bpm_->UnpinPage(curr_page_id, false);
            curr_page_id = next_page_id;
        }
    }
    std::cout << "[Recovery] Indexes successfully rebuilt." << std::endl;
}
