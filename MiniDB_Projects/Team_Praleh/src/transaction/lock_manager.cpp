#include "transaction/lock_manager.h"
#include <algorithm>
#include <chrono>

namespace minidb {

bool LockManager::LockShared(txn_id_t tid, int32_t rid) {
    std::unique_lock<std::mutex> lk(latch_);
    auto& q = table_[rid];
    q.entries.push_back({tid, LockMode::SHARED, false});
    bool success = q.cv.wait_for(lk, std::chrono::milliseconds(800), [&](){
        if (q.has_exclusive) return false;
        for (auto& e : q.entries)
            if (e.txn_id==tid && e.mode==LockMode::SHARED && !e.granted)
                { e.granted=true; q.shared_count++; return true; }
        return false;
    });
    if (!success) {
        auto it = std::find_if(q.entries.begin(), q.entries.end(),
            [tid](const LockEntry& e){ return e.txn_id==tid && !e.granted; });
        if (it != q.entries.end()) q.entries.erase(it);
        return false;
    }
    return true;
}

bool LockManager::LockExclusive(txn_id_t tid, int32_t rid) {
    std::unique_lock<std::mutex> lk(latch_);
    auto& q = table_[rid];
    q.entries.push_back({tid, LockMode::EXCLUSIVE, false});
    bool success = q.cv.wait_for(lk, std::chrono::milliseconds(800), [&](){
        if (q.has_exclusive) return false;
        if (q.shared_count > 0) return false;
        for (auto& e : q.entries)
            if (e.txn_id==tid && e.mode==LockMode::EXCLUSIVE && !e.granted)
                { e.granted=true; q.has_exclusive=true; return true; }
        return false;
    });
    if (!success) {
        auto it = std::find_if(q.entries.begin(), q.entries.end(),
            [tid](const LockEntry& e){ return e.txn_id==tid && !e.granted; });
        if (it != q.entries.end()) q.entries.erase(it);
        return false;
    }
    return true;
}

bool LockManager::Unlock(txn_id_t tid, int32_t rid) {
    std::unique_lock<std::mutex> lk(latch_);
    auto it = table_.find(rid);
    if (it==table_.end()) return false;
    auto& q = it->second;
    auto ei = std::find_if(q.entries.begin(), q.entries.end(),
        [tid](const LockEntry& e){ return e.txn_id==tid && e.granted; });
    if (ei==q.entries.end()) return false;
    if (ei->mode==LockMode::SHARED) q.shared_count--;
    else q.has_exclusive=false;
    q.entries.erase(ei);
    q.cv.notify_all();
    return true;
}

void LockManager::UnlockAll(txn_id_t tid) {
    // collect all rids held by this txn
    std::vector<int32_t> rids;
    {
        std::lock_guard<std::mutex> lk(latch_);
        for (auto& [rid, q] : table_)
            for (auto& e : q.entries)
                if (e.txn_id==tid) rids.push_back(rid);
    }
    for (auto rid : rids) Unlock(tid, rid);
}

} // namespace minidb
