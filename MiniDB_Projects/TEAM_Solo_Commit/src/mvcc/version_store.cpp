#include "version_store.h"

namespace minidb {

VersionStore::~VersionStore() {
    for (auto& [key, head] : heads_) {
        Version* v = head;
        while (v) { Version* older = v->older; delete v; v = older; }
    }
}

void VersionStore::Write(int txn, int64_t key, const std::string& value, bool deleted) {
    std::lock_guard<std::mutex> g(mu_);
    Version* v = new Version{PENDING, INF, txn, false, false, deleted, value, heads_[key]};
    heads_[key] = v;
    pending_[txn].push_back(v);
}

bool VersionStore::ReadSnapshot(int64_t snapshot_ts, int64_t key, std::string* out) const {
    std::lock_guard<std::mutex> g(mu_);
    auto it = heads_.find(key);
    if (it == heads_.end()) return false;
    // Walk newest-first; the first committed version with begin_ts <= snapshot is visible.
    for (Version* v = it->second; v; v = v->older) {
        if (!v->committed || v->aborted) continue;     // ignore in-flight / aborted writers
        if (v->begin_ts <= snapshot_ts) {              // committed at or before our snapshot
            if (v->deleted) return false;              // row was deleted as of the snapshot
            if (out) *out = v->value;
            return true;
        }
    }
    return false;
}

void VersionStore::Commit(int txn, int64_t commit_ts) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = pending_.find(txn);
    if (it == pending_.end()) return;
    for (Version* v : it->second) {
        v->begin_ts = commit_ts;
        v->committed = true;
        // The next committed version down the chain is now superseded as of commit_ts.
        for (Version* o = v->older; o; o = o->older) {
            if (o->committed && !o->aborted) { o->end_ts = commit_ts; break; }
        }
    }
    pending_.erase(it);
}

void VersionStore::Abort(int txn) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = pending_.find(txn);
    if (it == pending_.end()) return;
    for (Version* v : it->second) v->aborted = true;  // stays linked but never visible
    pending_.erase(it);
}

size_t VersionStore::VersionCount(int64_t key) const {
    std::lock_guard<std::mutex> g(mu_);
    size_t n = 0;
    auto it = heads_.find(key);
    for (Version* v = it == heads_.end() ? nullptr : it->second; v; v = v->older) ++n;
    return n;
}

}  // namespace minidb
