#include "query/executor.h"
#include "query/optimizer.h"
#include <iostream>
#include <iomanip>

// ─── Constructor ──────────────────────────────────────────────────────────────

Executor::Executor(BufferPool& bp, WAL& wal, TxManager& txm)
    : bp_(bp), wal_(wal), txm_(txm)
{}

// ─── registerTable ────────────────────────────────────────────────────────────

void Executor::registerTable(const std::string& name, TableEntry entry) {
    tables_[name] = std::move(entry);
}

bool Executor::tableExists(const std::string& name) const {
    return tables_.count(name) > 0;
}

std::pair<HeapFile*, BPlusTree*> Executor::getTablePointers(const std::string& name) {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return {nullptr, nullptr};
    }
    return {it->second.heap.get(), it->second.index.get()};
}

void Executor::showTables() const {
    if (tables_.empty()) {
        std::cout << "(no tables)\n";
        return;
    }
    std::cout << "Tables:\n";
    for (auto& [name, entry] : tables_) {
        std::cout << "  " << name
                  << "  (records=" << entry.heap->recordCount() << ")\n";
    }
}

// ─── getTable ─────────────────────────────────────────────────────────────────

TableEntry* Executor::getTable(const std::string& name) {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        std::cout << "ERROR: table '" << name << "' not found.\n";
        return nullptr;
    }
    return &it->second;
}

// ─── printRecord ──────────────────────────────────────────────────────────────

void Executor::printRecord(const RecordID& rid, const Record& rec) const {
    std::cout << "  [" << rid.page_id << ":" << rid.slot_id << "]"
              << "  key=" << std::setw(6) << rec.key
              << "  value=" << rec.value << "\n";
}

// ─── execute (dispatcher) ────────────────────────────────────────────────────

void Executor::execute(const ParsedQuery& q) {
    if (!q.valid) {
        std::cout << "Parse error: " << q.error_msg << "\n";
        return;
    }

    switch (q.type) {
        case CmdType::CREATE_TABLE:  doCreateTable(q);  break;
        case CmdType::INSERT:        doInsert(q);        break;
        case CmdType::SELECT_ALL:    doSelectAll(q);     break;
        case CmdType::SELECT_KEY:    doSelectKey(q);     break;
        case CmdType::SELECT_RANGE:  doSelectRange(q);   break;
        case CmdType::JOIN:          doJoin(q);          break;
        case CmdType::DELETE_KEY:    doDelete(q);        break;

        case CmdType::BEGIN:
            if (current_txid_ != 0) {
                std::cout << "ERROR: already in a transaction (TX " << current_txid_ << ").\n";
            } else {
                current_txid_ = txm_.begin();
                wal_.logBegin(current_txid_);
            }
            break;

        case CmdType::COMMIT:
            if (current_txid_ == 0) {
                std::cout << "ERROR: no active transaction.\n";
            } else {
                wal_.logCommit(current_txid_);
                txm_.commit(current_txid_);
                current_txid_ = 0;
            }
            break;

        case CmdType::ABORT:
            if (current_txid_ == 0) {
                std::cout << "ERROR: no active transaction.\n";
            } else {
                wal_.logAbort(current_txid_);
                txm_.abort(current_txid_);
                current_txid_ = 0;
            }
            break;

        case CmdType::SHOW_TABLES:
            showTables();
            break;

        case CmdType::HELP:
            std::cout << R"(
MiniDB Commands:
  CREATE TABLE <name>
  INSERT INTO <table> VALUES (<key>, <value>)
  SELECT * FROM <table>
  SELECT * FROM <table> WHERE id = <key>
  SELECT * FROM <table> WHERE id > <key>
  SELECT * FROM <t1> JOIN <t2> ON t1.id = t2.id
  DELETE FROM <table> WHERE id = <key>
  BEGIN
  COMMIT
  ABORT
  SHOW TABLES
  HELP
  QUIT
)";
            break;

        case CmdType::QUIT:
            std::cout << "Goodbye.\n";
            break;

        default:
            std::cout << "ERROR: unknown command.\n";
            break;
    }
}

// ─── doCreateTable ────────────────────────────────────────────────────────────

void Executor::doCreateTable(const ParsedQuery& q) {
    if (tableExists(q.table1)) {
        std::cout << "ERROR: table '" << q.table1 << "' already exists.\n";
        return;
    }

    auto heap  = std::make_unique<HeapFile>(q.table1, bp_);
    auto index = std::make_unique<BPlusTree>();

    heap->create();

    TableEntry entry;
    entry.heap  = std::move(heap);
    entry.index = std::move(index);
    tables_[q.table1] = std::move(entry);

    std::cout << "Table '" << q.table1 << "' created.\n";
}

// ─── doInsert ─────────────────────────────────────────────────────────────────

void Executor::doInsert(const ParsedQuery& q) {
    TableEntry* entry = getTable(q.table1);
    if (!entry) return;

    // Auto-commit mode if not in a transaction
    bool auto_commit = (current_txid_ == 0);
    TxID txid = current_txid_;

    if (auto_commit) {
        txid = txm_.begin();
        wal_.logBegin(txid);
    }

    // Acquire exclusive lock on the row (Strict 2PL)
    std::string rkey = TxManager::rowKey(q.table1, q.key);
    try {
        txm_.lockWrite(txid, rkey);
    } catch (DeadlockException& e) {
        std::cout << "DEADLOCK: " << e.what() << "\n";
        txm_.abort(txid);
        if (auto_commit) current_txid_ = 0;
        return;
    }

    // Log before data change (Write-Ahead rule)
    wal_.logInsert(txid, q.table1, q.key, q.value);

    // Write to storage
    Record rec{q.key, q.value};
    RecordID rid = entry->heap->insertRecord(rec);

    if (!rid.isValid()) {
        std::cout << "ERROR: insert failed.\n";
        if (auto_commit) { txm_.abort(txid); }
        return;
    }

    // Update primary key index
    entry->index->insert(q.key, rid);

    if (auto_commit) {
        wal_.logCommit(txid);
        txm_.commit(txid);
    }

    std::cout << "Inserted: key=" << q.key << " value=" << q.value
              << " at [" << rid.page_id << ":" << rid.slot_id << "]\n";
}

// ─── doSelectAll ──────────────────────────────────────────────────────────────

void Executor::doSelectAll(const ParsedQuery& q) {
    TableEntry* entry = getTable(q.table1);
    if (!entry) return;

    TableStats stats = Optimizer::collectStats(*entry->heap);
    QueryPlan  plan  = optimizer_.choosePlanForScan(stats);

    std::cout << "[Optimizer] " << plan.description << "\n";
    std::cout << "Results from '" << q.table1 << "':\n";

    int count = 0;
    // TABLE_SCAN is always chosen for SELECT *
    entry->heap->scanAll([&](const RecordID& rid, const Record& rec) {
        printRecord(rid, rec);
        count++;
        return true;
    });

    std::cout << count << " row(s) found.\n";
}

// ─── doSelectKey ──────────────────────────────────────────────────────────────

void Executor::doSelectKey(const ParsedQuery& q) {
    TableEntry* entry = getTable(q.table1);
    if (!entry) return;

    TableStats stats = Optimizer::collectStats(*entry->heap);
    QueryPlan  plan  = optimizer_.choosePlanForKey(stats, true);

    std::cout << "[Optimizer] " << plan.description << "\n";

    if (plan.strategy == ScanStrategy::INDEX_SCAN) {
        // INDEX SCAN path — O(log N)
        auto rid_opt = entry->index->search(q.key);
        if (!rid_opt) {
            std::cout << "Not found.\n";
            return;
        }
        auto rec_opt = entry->heap->getRecord(*rid_opt);
        if (rec_opt) {
            std::cout << "Result:\n";
            printRecord(*rid_opt, *rec_opt);
            std::cout << "1 row found.\n";
        }
    } else {
        // TABLE SCAN path — O(N)
        int count = 0;
        std::cout << "Results:\n";
        entry->heap->scanAll([&](const RecordID& rid, const Record& rec) {
            if (rec.key == q.key) {
                printRecord(rid, rec);
                count++;
            }
            return true;
        });
        std::cout << count << " row(s) found.\n";
    }
}

// ─── doSelectRange ────────────────────────────────────────────────────────────

void Executor::doSelectRange(const ParsedQuery& q) {
    TableEntry* entry = getTable(q.table1);
    if (!entry) return;

    TableStats stats = Optimizer::collectStats(*entry->heap);
    QueryPlan  plan  = optimizer_.choosePlanForRange(stats, true);

    std::cout << "[Optimizer] " << plan.description << "\n";
    std::cout << "Results (key " << q.op << " " << q.key << "):\n";

    int count = 0;

    if (plan.strategy == ScanStrategy::INDEX_SCAN && q.op == ">") {
        // Use leaf-linked-list scan starting from the key boundary
        entry->index->scanAll([&](int32_t k, const RecordID& rid) {
            if (k > q.key) {
                auto rec_opt = entry->heap->getRecord(rid);
                if (rec_opt) { printRecord(rid, *rec_opt); count++; }
            }
        });
    } else {
        // TABLE SCAN for other operators
        entry->heap->scanAll([&](const RecordID& rid, const Record& rec) {
            bool match = false;
            if (q.op == ">")       match = rec.key >  q.key;
            else if (q.op == "<")  match = rec.key <  q.key;
            else if (q.op == ">=") match = rec.key >= q.key;
            else if (q.op == "<=") match = rec.key <= q.key;
            if (match) { printRecord(rid, rec); count++; }
            return true;
        });
    }

    std::cout << count << " row(s) found.\n";
}

// ─── doJoin ───────────────────────────────────────────────────────────────────
// Nested-loop join: for each row in t1, index-lookup matching key in t2.

void Executor::doJoin(const ParsedQuery& q) {
    TableEntry* e1 = getTable(q.table1);
    TableEntry* e2 = getTable(q.table2);
    if (!e1 || !e2) return;

    std::cout << "JOIN " << q.table1 << " ⋈ " << q.table2 << " ON id=id\n";
    std::cout << "[Optimizer] NestedLoopJoin (outer=" << q.table1
              << ", inner=IndexScan(" << q.table2 << "))\n";
    std::cout << "Results:\n";

    int count = 0;
    e1->heap->scanAll([&](const RecordID& /*r1*/, const Record& rec1) {
        // For each outer row, look up matching key in t2's index
        auto rid2_opt = e2->index->search(rec1.key);
        if (rid2_opt) {
            auto rec2_opt = e2->heap->getRecord(*rid2_opt);
            if (rec2_opt) {
                std::cout << "  key=" << rec1.key
                          << "  " << q.table1 << ".value=" << rec1.value
                          << "  " << q.table2 << ".value=" << rec2_opt->value << "\n";
                count++;
            }
        }
        return true;
    });

    std::cout << count << " joined row(s).\n";
}

// ─── doDelete ─────────────────────────────────────────────────────────────────

void Executor::doDelete(const ParsedQuery& q) {
    TableEntry* entry = getTable(q.table1);
    if (!entry) return;

    bool auto_commit = (current_txid_ == 0);
    TxID txid = current_txid_;

    if (auto_commit) {
        txid = txm_.begin();
        wal_.logBegin(txid);
    }

    std::string rkey = TxManager::rowKey(q.table1, q.key);
    try {
        txm_.lockWrite(txid, rkey);
    } catch (DeadlockException& e) {
        std::cout << "DEADLOCK: " << e.what() << "\n";
        txm_.abort(txid);
        return;
    }

    // Find the record via index
    auto rid_opt = entry->index->search(q.key);
    if (!rid_opt) {
        std::cout << "Not found: key=" << q.key << "\n";
        if (auto_commit) { txm_.abort(txid); }
        return;
    }

    // Log before deleting (Write-Ahead rule)
    wal_.logDelete(txid, q.table1, q.key);

    entry->heap->deleteRecord(*rid_opt);
    entry->index->remove(q.key);

    if (auto_commit) {
        wal_.logCommit(txid);
        txm_.commit(txid);
    }

    std::cout << "Deleted key=" << q.key << " from '" << q.table1 << "'.\n";
}
