#pragma once

#include "mvcc_types.h"
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <list>
#include <optional>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace mvcc {

//  Version Record — one entry in a version chain

struct VersionRecord {
    TxnID         creatorTxn;     // transaction that created this version
    Timestamp     beginTS;        // visible from this timestamp (inclusive)
    Timestamp     endTS;          // visible until this timestamp (exclusive)
    VersionStatus status;
    Tuple         data;           // the actual column values
    RID           rid;            // physical location hint

    // Pointer to the previous (older) version — forms the chain
    std::shared_ptr<VersionRecord> prev;

    VersionRecord(TxnID txn, Timestamp bts, Tuple d, RID r)
        : creatorTxn(txn), beginTS(bts), endTS(INF_TS),
          status(VersionStatus::ACTIVE), data(std::move(d)), rid(r) {}

    // Is this version visible to a transaction with the given read timestamp?
    bool visibleTo(Timestamp readTS) const {
        return status == VersionStatus::COMMITTED &&
               beginTS <= readTS && readTS < endTS;
    }

    std::string statusStr() const {
        switch (status) {
            case VersionStatus::ACTIVE:    return "ACTIVE";
            case VersionStatus::COMMITTED: return "COMMITTED";
            case VersionStatus::ABORTED:   return "ABORTED";
            case VersionStatus::DELETED:   return "DELETED";
        }
        return "?";
    }
};

//  Version Chain — linked list of VersionRecords for one logical row
//  Head = newest; tail = oldest
class VersionChain {
public:
    using RecordPtr = std::shared_ptr<VersionRecord>;

    explicit VersionChain(uint64_t key) : logicalKey_(key) {}

    // Append a new version at the head (most recent)
    void append(RecordPtr rec) {
        std::unique_lock lock(mu_);
        rec->prev = head_;
        head_     = rec;
        ++chainLen_;
    }

    // Traverse the chain to find the visible version for `readTS`
    std::optional<Tuple> readVersion(Timestamp readTS) const {
        std::shared_lock lock(mu_);
        auto cur = head_;
        while (cur) {
            if (cur->visibleTo(readTS))
                return cur->data;
            cur = cur->prev;
        }
        return std::nullopt;
    }

    // Get the head (latest) record — used during write conflict detection
    RecordPtr head() const {
        std::shared_lock lock(mu_);
        return head_;
    }

    // Logical key (primary key value) this chain belongs to
    uint64_t logicalKey() const { return logicalKey_; }

    // Chain length (including all versions, even old ones)
    size_t length() const {
        std::shared_lock lock(mu_);
        return chainLen_;
    }

    // Garbage collect versions whose endTS < horizonTS
    size_t gc(Timestamp horizonTS) {
        std::unique_lock lock(mu_);
        if (!head_) return 0;

        size_t removed = 0;
        // Walk to find first version fully before horizon
        auto cur = head_;
        while (cur && cur->prev) {
            if (cur->prev->endTS < horizonTS) {
                removed += countChain(cur->prev);
                cur->prev = nullptr;
                chainLen_ -= removed;
                break;
            }
            cur = cur->prev;
        }
        return removed;
    }

    // Print the version chain (debug / demo)
    void dump(const TableSchema& schema, std::ostream& os = std::cout) const {
        std::shared_lock lock(mu_);
        os << "  Version Chain [key=" << logicalKey_ << ", len=" << chainLen_ << "]\n";
        auto cur = head_;
        int idx = 0;
        while (cur) {
            os << "    [v" << idx++ << "] txn=" << cur->creatorTxn
               << " ts=[" << cur->beginTS << "," ;
            if (cur->endTS == INF_TS) os << "INF";
            else                      os << cur->endTS;
            os << ") status=" << cur->statusStr() << " | ";
            for (size_t i = 0; i < cur->data.size() && i < schema.columns.size(); ++i) {
                os << schema.columns[i].name << "=" << cur->data[i].toString();
                if (i + 1 < cur->data.size()) os << ", ";
            }
            os << "\n";
            cur = cur->prev;
        }
    }

private:
    static size_t countChain(const RecordPtr& p) {
        size_t n = 0;
        auto cur = p;
        while (cur) { ++n; cur = cur->prev; }
        return n;
    }

    uint64_t            logicalKey_;
    RecordPtr           head_     = nullptr;
    size_t              chainLen_ = 0;
    mutable std::shared_mutex mu_;
};

//  Version Chain Index — maps logical key → VersionChain

class VersionChainIndex {
public:
    using ChainPtr = std::shared_ptr<VersionChain>;

    ChainPtr getOrCreate(uint64_t key) {
        std::unique_lock lock(mu_);
        auto it = index_.find(key);
        if (it != index_.end()) return it->second;
        auto chain = std::make_shared<VersionChain>(key);
        index_[key] = chain;
        return chain;
    }

    ChainPtr get(uint64_t key) const {
        std::shared_lock lock(mu_);
        auto it = index_.find(key);
        return it != index_.end() ? it->second : nullptr;
    }

    // Run GC across all chains
    size_t gcAll(Timestamp horizonTS) {
        std::shared_lock lock(mu_);
        size_t total = 0;
        for (auto& [k, chain] : index_)
            total += chain->gc(horizonTS);
        return total;
    }

    size_t size() const {
        std::shared_lock lock(mu_);
        return index_.size();
    }

    void dumpAll(const TableSchema& schema, std::ostream& os = std::cout) const {
        std::shared_lock lock(mu_);
        os << "=== Version Chain Index (" << index_.size() << " keys) ===\n";
        for (auto& [k, chain] : index_)
            chain->dump(schema, os);
    }

private:
    mutable std::shared_mutex mu_;
    std::unordered_map<uint64_t, ChainPtr> index_;
};

} 