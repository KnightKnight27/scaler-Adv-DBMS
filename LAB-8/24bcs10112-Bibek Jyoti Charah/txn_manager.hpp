// Lab 8 - In-memory transaction manager (MVCC reads, strict-2PL writes)
// Bibek Jyoti Charah (24bcs10112)
//
// Reads are served from a snapshot taken at begin(): a version is visible to
// snapshot S when begin_ts <= S and (end_ts == 0 or end_ts > S). Readers
// never lock. Writes take a per-key exclusive lock held until commit/abort;
// a blocked writer walks the waits-for graph and aborts the youngest txn in
// any cycle. Commit applies first-updater-wins: if a key it wrote gained a
// newer committed version since its snapshot, the commit is rejected.
//
// Single-threaded: a contended write returns LockWait and the caller retries.

#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace minidb {

using TxId = std::uint64_t;
using Ts   = std::uint64_t;

enum class Result { Ok, NotFound, LockWait, Aborted, Serialization };
enum class State  { Active, Committed, Aborted };

inline const char *name(Result r) {
    switch (r) {
        case Result::Ok:            return "OK";
        case Result::NotFound:      return "NOT_FOUND";
        case Result::LockWait:      return "LOCK_WAIT";
        case Result::Aborted:       return "ABORTED";
        case Result::Serialization: return "SERIALIZATION_FAILURE";
    }
    return "?";
}

inline const char *name(State s) {
    switch (s) {
        case State::Active:    return "Active";
        case State::Committed: return "Committed";
        case State::Aborted:   return "Aborted";
    }
    return "?";
}

class TxnManager {
public:
    TxId begin() {
        TxId id = ++last_tx_;
        Txn &t = txns_[id];
        t.id = id;
        t.snapshot = clock_;
        t.state = State::Active;
        return id;
    }

    Result read(TxId tx, const std::string &key, std::string &out) {
        Txn *t = active(tx);
        if (!t) return Result::Aborted;
        if (auto it = t->writes.find(key); it != t->writes.end()) {   // own write
            if (it->second.dead) return Result::NotFound;
            out = it->second.value;
            return Result::Ok;
        }
        const Version *v = visible(key, t->snapshot);
        if (!v || v->dead) return Result::NotFound;
        out = v->value;
        return Result::Ok;
    }

    Result write(TxId tx, const std::string &key, std::string value) {
        return stage(tx, key, std::move(value), false);
    }

    Result remove(TxId tx, const std::string &key) {
        return stage(tx, key, std::string(), true);
    }

    Result commit(TxId tx) {
        Txn *t = active(tx);
        if (!t) return Result::Aborted;

        for (const auto &kv : t->writes) {                 // first-updater-wins
            auto it = store_.find(kv.first);
            if (it != store_.end() && !it->second.empty() &&
                it->second.back().begin_ts > t->snapshot) {
                rollback(*t);
                return Result::Serialization;
            }
        }

        Ts ts = ++clock_;
        for (auto &kv : t->writes) {
            auto &chain = store_[kv.first];
            if (!chain.empty()) chain.back().end_ts = ts;   // supersede current head
            chain.push_back(Version{std::move(kv.second.value), kv.second.dead, ts, 0, t->id});
        }
        releaseLocks(*t);
        t->state = State::Committed;
        return Result::Ok;
    }

    void abort(TxId tx) {
        if (Txn *t = active(tx)) rollback(*t);
    }

    // Drop dead versions whose end_ts is at or below the oldest live snapshot.
    std::size_t gc() {
        Ts floor = oldestSnapshot();
        std::size_t removed = 0;
        for (auto &kv : store_) {
            std::vector<Version> kept;
            kept.reserve(kv.second.size());
            for (auto &v : kv.second) {
                if (v.end_ts != 0 && v.end_ts <= floor) ++removed;
                else kept.push_back(std::move(v));
            }
            kv.second.swap(kept);
        }
        return removed;
    }

    State state_of(TxId tx) const {
        auto it = txns_.find(tx);
        return it == txns_.end() ? State::Aborted : it->second.state;
    }
    TxId last_victim() const { return victim_; }

    std::size_t live_txns() const {
        std::size_t n = 0;
        for (const auto &kv : txns_) if (kv.second.state == State::Active) ++n;
        return n;
    }
    std::size_t locks_held() const { return lock_owner_.size(); }
    std::size_t versions() const {
        std::size_t n = 0;
        for (const auto &kv : store_) n += kv.second.size();
        return n;
    }

    std::string dump(const std::string &key) const {
        auto it = store_.find(key);
        if (it == store_.end() || it->second.empty()) return "(none)";
        std::ostringstream os;
        for (const Version &v : it->second)
            os << "[" << v.begin_ts << ".." << v.end_ts << " "
               << (v.dead ? "<del>" : v.value) << "] ";
        return os.str();
    }

    // Returns "" when the internal structures are healthy.
    std::string invariants() const {
        for (const auto &kv : lock_owner_) {
            auto it = txns_.find(kv.second);
            if (it == txns_.end() || it->second.state != State::Active)
                return "lock on '" + kv.first + "' held by inactive txn";
        }
        for (const auto &kv : store_) {
            Ts prev = 0;
            const auto &chain = kv.second;
            for (std::size_t i = 0; i < chain.size(); ++i) {
                const Version &v = chain[i];
                if (v.begin_ts <= prev)
                    return "begin_ts not increasing on '" + kv.first + "'";
                if (v.end_ts != 0 && v.end_ts <= v.begin_ts)
                    return "end_ts <= begin_ts on '" + kv.first + "'";
                if (i + 1 < chain.size() && v.end_ts == 0)
                    return "live version not at tail on '" + kv.first + "'";
                prev = v.begin_ts;
            }
        }
        return "";
    }

private:
    struct Version {
        std::string value;
        bool dead;
        Ts begin_ts;
        Ts end_ts;     // 0 => still live
        TxId by;
    };
    struct Write { std::string value; bool dead; };
    struct Txn {
        TxId id = 0;
        Ts snapshot = 0;
        State state = State::Active;
        std::unordered_map<std::string, Write> writes;
        std::unordered_set<std::string> locks;
    };

    TxId last_tx_ = 0;
    Ts clock_ = 0;
    TxId victim_ = 0;

    std::unordered_map<TxId, Txn> txns_;
    std::unordered_map<std::string, std::vector<Version>> store_;
    std::unordered_map<std::string, TxId> lock_owner_;   // key -> holder
    std::unordered_map<TxId, TxId> waits_for_;           // waiter -> holder

    Txn *active(TxId tx) {
        auto it = txns_.find(tx);
        return (it != txns_.end() && it->second.state == State::Active) ? &it->second : nullptr;
    }

    const Version *visible(const std::string &key, Ts snap) const {
        auto it = store_.find(key);
        if (it == store_.end()) return nullptr;
        const auto &chain = it->second;
        for (auto i = chain.rbegin(); i != chain.rend(); ++i)
            if (i->begin_ts <= snap && (i->end_ts == 0 || i->end_ts > snap)) return &*i;
        return nullptr;
    }

    Result stage(TxId tx, const std::string &key, std::string value, bool dead) {
        Txn *t = active(tx);
        if (!t) return Result::Aborted;
        Result g = lock(*t, key);
        if (g != Result::Ok) return g;
        t->writes[key] = Write{std::move(value), dead};
        return Result::Ok;
    }

    Result lock(Txn &t, const std::string &key) {
        auto it = lock_owner_.find(key);
        if (it == lock_owner_.end()) {
            lock_owner_[key] = t.id;
            t.locks.insert(key);
            waits_for_.erase(t.id);
            return Result::Ok;
        }
        if (it->second == t.id) return Result::Ok;          // reentrant

        waits_for_[t.id] = it->second;
        if (TxId v = cycleVictim(t.id)) {
            victim_ = v;
            rollback(txns_[v]);
            if (v == t.id) return Result::Aborted;
            return lock(t, key);                             // victim freed it; retry
        }
        return Result::LockWait;
    }

    // Follows waits_for_ from `start`; if it loops back to `start`, returns the
    // youngest (highest-id) txn on the cycle, else 0.
    TxId cycleVictim(TxId start) const {
        TxId cur = start, youngest = start;
        std::unordered_set<TxId> seen;
        for (;;) {
            auto it = waits_for_.find(cur);
            if (it == waits_for_.end()) return 0;
            cur = it->second;
            if (cur == start) return youngest;
            if (!seen.insert(cur).second) return 0;
            youngest = std::max(youngest, cur);
        }
    }

    void releaseLocks(Txn &t) {
        for (const std::string &k : t.locks) lock_owner_.erase(k);
        t.locks.clear();
        for (auto it = waits_for_.begin(); it != waits_for_.end();)
            it = (it->second == t.id) ? waits_for_.erase(it) : std::next(it);
        waits_for_.erase(t.id);
    }

    void rollback(Txn &t) {
        t.writes.clear();
        releaseLocks(t);
        t.state = State::Aborted;
    }

    Ts oldestSnapshot() const {
        Ts low = clock_;
        for (const auto &kv : txns_)
            if (kv.second.state == State::Active) low = std::min(low, kv.second.snapshot);
        return low;
    }
};

}  // namespace minidb
