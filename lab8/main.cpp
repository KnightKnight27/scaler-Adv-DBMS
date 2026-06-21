// main.cpp — Lab 8 demo (Aman Yadav, 24BCS10183, Class B, 2nd Year)
//
// Drives the transaction manager through six bank-account scenarios:
//   1. MVCC snapshot isolation
//   2. Tombstone visibility across snapshots
//   3. Strict 2PL — second writer blocks with LockWait
//   4. Waits-for cycle deadlock detection (youngest aborted)
//   5. First-updater-wins serialization conflict
//   6. gc() vacuums dead versions once long readers finish

#include "txn_manager.hpp"

#include <cassert>
#include <iostream>
#include <string>

using adbms::txn::Manager;
using adbms::txn::Result;
using adbms::txn::State;
using adbms::txn::txn_id_t;

namespace {

void section(const std::string& s) { std::cout << "\n=== " << s << " ===\n"; }

void check(bool ok_, const std::string& what) {
    std::cout << (ok_ ? "  [pass] " : "  [FAIL] ") << what << "\n";
    if (!ok_) std::exit(1);
}

void invariants(const Manager& m, const std::string& after) {
    auto e = m.check_invariants();
    if (!e.empty()) {
        std::cerr << "INVARIANT FAIL after " << after << ": " << e << "\n";
        std::exit(1);
    }
}

std::string rd(Manager& m, txn_id_t tx, const std::string& key) {
    std::string v;
    Result r = m.read(tx, key, v);
    return r == Result::Ok ? v : std::string("<none>");
}

}  // namespace

int main() {
    std::cout << "Lab 8 — Transaction Manager Demo\n";
    std::cout << "Aman Yadav | 24BCS10183 | Class B | 2nd Year\n";

    Manager bank;

    section("0) Seed alice=1000, bob=500, carol=750");
    {
        txn_id_t s = bank.begin();
        bank.write(s, "alice", "1000");
        bank.write(s, "bob",    "500");
        bank.write(s, "carol",  "750");
        check(bank.commit(s) == Result::Ok, "seed committed");
        invariants(bank, "seed commit");
    }

    section("1) MVCC snapshot isolation");
    {
        txn_id_t reader = bank.begin();
        check(rd(bank, reader, "alice") == "1000", "reader snapshot sees alice=1000");

        txn_id_t writer = bank.begin();
        check(bank.write(writer, "alice", "1500") == Result::Ok, "writer X-locks alice");
        check(bank.commit(writer) == Result::Ok,                  "writer commits alice=1500");

        check(rd(bank, reader, "alice") == "1000", "reader STILL sees alice=1000");

        txn_id_t fresh = bank.begin();
        check(rd(bank, fresh, "alice") == "1500", "fresh reader sees alice=1500");
        bank.commit(reader);
        bank.commit(fresh);
        invariants(bank, "mvcc scenario");
    }

    section("2) Tombstones — old snapshot still sees deleted row");
    {
        txn_id_t older = bank.begin();
        check(rd(bank, older, "carol") == "750", "older snapshot sees carol=750");

        txn_id_t closer = bank.begin();
        check(bank.remove(closer, "carol") == Result::Ok, "delete carol");
        check(bank.commit(closer) == Result::Ok,          "delete committed");

        check(rd(bank, older, "carol") == "750",   "older snapshot still sees carol=750");
        txn_id_t fresh = bank.begin();
        check(rd(bank, fresh, "carol") == "<none>", "fresh snapshot — carol gone");
        bank.commit(older);
        bank.commit(fresh);
        invariants(bank, "tombstone scenario");
    }

    section("3) Strict 2PL — second writer blocks");
    {
        txn_id_t t1 = bank.begin();
        txn_id_t t2 = bank.begin();
        check(bank.write(t1, "bob", "600") == Result::Ok,        "T1 X-locks bob");
        check(bank.write(t2, "bob", "700") == Result::LockWait,  "T2 -> LockWait");

        bank.abort(t1);
        check(bank.state_of(t1) == State::Aborted, "T1 aborted");
        check(bank.write(t2, "bob", "700") == Result::Ok, "T2 retries -> Ok");
        check(bank.commit(t2) == Result::Ok,              "T2 commits bob=700");

        txn_id_t v = bank.begin();
        check(rd(bank, v, "bob") == "700", "bob = 700");
        bank.commit(v);
        invariants(bank, "2pl scenario");
    }

    section("4) Deadlock detection — waits-for cycle, youngest aborted");
    {
        txn_id_t ab = bank.begin();
        txn_id_t ba = bank.begin();
        check(bank.write(ab, "alice", "1400") == Result::Ok, "T_AB locks alice");
        check(bank.write(ba, "bob",   "650")  == Result::Ok, "T_BA locks bob");

        // T_AB now wants bob (held by T_BA)
        check(bank.write(ab, "bob", "1") == Result::LockWait, "T_AB waits on bob");
        // T_BA now wants alice (held by T_AB) -> cycle, victim = max(ab, ba) = ba
        Result r = bank.write(ba, "alice", "1");
        check(r == Result::Aborted || bank.state_of(ba) == State::Aborted,
              "T_BA detected as victim and aborted");
        check(bank.last_victim().has_value() && *bank.last_victim() == ba,
              "last_victim() == T_BA (youngest)");

        // T_AB can now finish
        check(bank.write(ab, "bob", "650") == Result::Ok, "T_AB acquires bob after victim release");
        check(bank.commit(ab) == Result::Ok,              "T_AB commits");
        invariants(bank, "deadlock scenario");
    }

    section("5) First-updater-wins — late commit hits SerializationFailure");
    {
        txn_id_t a = bank.begin();
        txn_id_t b = bank.begin();

        // both snapshot the same alice value
        std::string sa, sb;
        bank.read(a, "alice", sa);
        bank.read(b, "alice", sb);
        check(sa == sb, "both txns saw same alice snapshot");

        check(bank.write(a, "alice", "2000") == Result::Ok,    "T_A X-locks alice");
        // T_B can't even take the lock — already in LockWait
        check(bank.write(b, "alice", "2500") == Result::LockWait, "T_B -> LockWait");

        check(bank.commit(a) == Result::Ok, "T_A commits alice=2000");

        // After T_A commits, T_B retries, sees newer xmin, and serialization-fails
        check(bank.write(b, "alice", "2500") == Result::Ok, "T_B reacquires lock");
        check(bank.commit(b) == Result::SerializationFailure,
              "T_B fails first-updater-wins check");
        invariants(bank, "fuw scenario");
    }

    section("6) gc() reclaims dead versions");
    {
        std::size_t before = bank.version_count();
        bank.gc();
        std::size_t after = bank.version_count();
        std::cout << "  versions: " << before << " -> " << after << "\n";
        check(after <= before, "vacuum did not grow the version set");

        // sanity check — latest values still readable
        txn_id_t v = bank.begin();
        check(rd(bank, v, "alice") == "2000", "alice still = 2000 after gc");
        check(rd(bank, v, "bob")   == "650",  "bob still = 650 after gc");
        bank.commit(v);
        invariants(bank, "gc scenario");
    }

    std::cout << "\nAll transaction-manager checks passed.\n";
    return 0;
}
