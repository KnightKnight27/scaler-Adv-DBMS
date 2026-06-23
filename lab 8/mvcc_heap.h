#pragma once
#include "tx_types.h"
#include <unordered_map>
#include <list>
#include <mutex>
#include <optional>
#include <atomic>

class MvccHeap {
public:
    static MvccHeap& getInstance();

    TxID beginTransaction();
    bool isCommitted(TxID xid);
    bool isAborted(TxID xid);
    void commitTransaction(TxID xid);
    void abortTransaction(TxID xid);

    bool checkShrinking(TxID xid);
    void setShrinking(TxID xid);

    std::optional<std::string> readKey(const RowKey& key, TxID reader_xid);
    void insertKey(const RowKey& key, const std::string& value, TxID xid);
    void updateKey(const RowKey& key, const std::string& value, TxID xid);
    void deleteKey(const RowKey& key, TxID xid);

private:
    MvccHeap() = default;
    ~MvccHeap() = default;
    MvccHeap(const MvccHeap&) = delete;
    MvccHeap& operator=(const MvccHeap&) = delete;

    bool isVisible(const RowVersion& version, TxID snapshot_xid, TxID reader_xid);

    std::atomic<TxID> next_xid{1};
    std::mutex tx_mutex;
    std::unordered_map<TxID, Transaction> transactions;

    std::mutex heap_mutex;
    std::unordered_map<RowKey, std::list<RowVersion>> heap;
};
