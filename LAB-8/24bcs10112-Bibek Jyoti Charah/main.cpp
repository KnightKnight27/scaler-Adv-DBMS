// Lab 8 - Transaction manager demo / 24bcs10112 Bibek Jyoti Charah
//
// Six bank-account scenarios, each asserted: MVCC snapshot isolation,
// tombstone visibility, strict 2PL, deadlock detection, first-updater-wins,
// and garbage collection. invariants() runs after every scenario.

#include "txn_manager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using minidb::Result;
using minidb::State;
using minidb::TxId;
using minidb::TxnManager;
using minidb::name;

namespace {

int failures = 0;

void title(const std::string &s) { std::cout << "\n== " << s << " ==\n"; }

void check(bool ok, const std::string &what) {
    std::cout << (ok ? "  [ok]   " : "  [FAIL] ") << what << '\n';
    if (!ok) ++failures;
}

void healthy(const TxnManager &m, const std::string &where) {
    std::string e = m.invariants();
    if (!e.empty()) {
        std::cerr << "invariant broken after " << where << ": " << e << '\n';
        std::exit(1);
    }
}

std::string readOr(TxnManager &m, TxId tx, const std::string &key) {
    std::string v;
    return m.read(tx, key, v) == Result::Ok ? v : std::string("<none>");
}

}  // namespace

int main() {
    TxnManager db;

    title("seed alice=1000 bob=500 carol=750");
    {
        TxId s = db.begin();
        db.write(s, "alice", "1000");
        db.write(s, "bob", "500");
        db.write(s, "carol", "750");
        check(db.commit(s) == Result::Ok, "seed committed");
        healthy(db, "seed");
    }

    title("MVCC snapshot isolation");
    {
        TxId reader = db.begin();
        check(readOr(db, reader, "alice") == "1000", "reader sees 1000");
        TxId writer = db.begin();
        check(db.write(writer, "alice", "1500") == Result::Ok, "writer locks alice");
        check(db.commit(writer) == Result::Ok, "writer commits 1500");
        check(readOr(db, reader, "alice") == "1000", "old reader still sees 1000");
        TxId fresh = db.begin();
        check(readOr(db, fresh, "alice") == "1500", "fresh reader sees 1500");
        db.commit(reader);
        db.commit(fresh);
        healthy(db, "mvcc");
    }

    title("tombstone visibility");
    {
        TxId older = db.begin();
        check(readOr(db, older, "carol") == "750", "older reader sees carol");
        TxId del = db.begin();
        check(db.remove(del, "carol") == Result::Ok, "delete carol");
        check(db.commit(del) == Result::Ok, "delete committed");
        check(readOr(db, older, "carol") == "750", "old snapshot still sees carol");
        TxId fresh = db.begin();
        check(readOr(db, fresh, "carol") == "<none>", "new snapshot: carol gone");
        db.commit(older);
        db.commit(fresh);
        healthy(db, "tombstone");
    }

    title("strict 2PL: second writer waits");
    {
        TxId t1 = db.begin(), t2 = db.begin();
        check(db.write(t1, "bob", "600") == Result::Ok, "t1 locks bob");
        check(db.write(t2, "bob", "700") == Result::LockWait, "t2 -> LOCK_WAIT");
        db.abort(t1);
        check(db.state_of(t1) == State::Aborted, "t1 aborted, lock freed");
        check(db.write(t2, "bob", "700") == Result::Ok, "t2 retries -> ok");
        check(db.commit(t2) == Result::Ok, "t2 commits bob=700");
        TxId v = db.begin();
        check(readOr(db, v, "bob") == "700", "bob = 700");
        db.commit(v);
        healthy(db, "2pl");
    }

    title("deadlock detection");
    {
        TxId ab = db.begin(), ba = db.begin();
        check(db.write(ab, "alice", "1400") == Result::Ok, "ab locks alice");
        check(db.write(ba, "bob", "650") == Result::Ok, "ba locks bob");
        check(db.write(ab, "bob", "750") == Result::LockWait, "ab waits for bob");
        Result r = db.write(ba, "alice", "1600");
        std::cout << "  ba wants alice -> " << name(r) << "; victim T" << db.last_victim() << '\n';
        check(r == Result::Aborted, "younger txn aborted");
        check(db.last_victim() == ba, "victim is ba (higher id)");
        check(db.write(ab, "bob", "750") == Result::Ok, "ab grabs freed bob");
        check(db.commit(ab) == Result::Ok, "ab commits");
        healthy(db, "deadlock");
    }

    title("first-updater-wins");
    {
        TxId t1 = db.begin(), t2 = db.begin();
        check(readOr(db, t1, "alice") == "1400", "t1 reads 1400");
        check(readOr(db, t2, "alice") == "1400", "t2 reads 1400");
        check(db.write(t1, "alice", "2000") == Result::Ok, "t1 writes 2000");
        check(db.write(t2, "alice", "100") == Result::LockWait, "t2 blocked by t1");
        check(db.commit(t1) == Result::Ok, "t1 commits");
        check(db.write(t2, "alice", "100") == Result::Ok, "t2 takes freed lock");
        Result r = db.commit(t2);
        std::cout << "  t2 commit -> " << name(r) << '\n';
        check(r == Result::Serialization, "t2 rejected: alice moved past its snapshot");
        TxId v = db.begin();
        check(readOr(db, v, "alice") == "2000", "alice = 2000 (first updater won)");
        db.commit(v);
        healthy(db, "first-updater-wins");
    }

    title("gc reclaims dead versions");
    {
        std::cout << "  alice before: " << db.dump("alice") << '\n';
        std::size_t before = db.versions();
        std::size_t pruned = db.gc();
        std::size_t after = db.versions();
        std::cout << "  versions " << before << " -> " << after << " (pruned " << pruned << ")\n";
        std::cout << "  alice after:  " << db.dump("alice") << '\n';
        check(pruned > 0, "reclaimed at least one dead version");
        check(before - pruned == after, "version count consistent");
        TxId v = db.begin();
        check(readOr(db, v, "alice") == "2000", "alice survives gc");
        check(readOr(db, v, "bob") == "750", "bob survives gc");
        db.commit(v);
        healthy(db, "gc");
    }

    std::cout << "\nstats: " << db.live_txns() << " live txns, " << db.locks_held()
              << " locks, " << db.versions() << " versions\n";
    if (failures) {
        std::cout << failures << " checks FAILED\n";
        return 1;
    }
    std::cout << "All transaction-manager checks passed.\n";
    return 0;
}
