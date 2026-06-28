// M4 + Track B test — concurrency control.
//  MVCC : snapshot isolation, first-updater-wins, deadlock victim selection.
//  2PL  : the SAME read-under-write scenario blocks (the contrast that
//         motivates the extension).
#include "mvcc.hpp"
#include "two_pl.hpp"
#include <cassert>
#include <cstdio>
#include <string>

using namespace minidb;

int main() {
    // --- MVCC: snapshot isolation ------------------------------------------
    {
        MVCCManager m;
        auto t0 = m.begin();
        m.write(t0, "alice", "1000"); m.commit(t0);

        auto reader = m.begin();               // snapshot sees alice=1000
        auto writer = m.begin();
        m.write(writer, "alice", "1500"); m.commit(writer);

        std::string v;
        m.read(reader, "alice", v);
        assert(v == "1000" && "reader keeps its snapshot despite a committed write");
        auto fresh = m.begin();
        m.read(fresh, "alice", v);
        assert(v == "1500" && "a new snapshot sees the new value");
        std::printf("[Track B] MVCC snapshot isolation: reader=1000, fresh=1500  OK\n");
    }

    // --- MVCC: first-updater-wins ------------------------------------------
    {
        MVCCManager m;
        auto s = m.begin(); m.write(s, "x", "0"); m.commit(s);
        auto t1 = m.begin();
        auto t2 = m.begin();
        assert(m.write(t1, "x", "1") == TxnResult::Ok);
        // t2 wants x too -> writer lock held by t1 -> LockWait
        assert(m.write(t2, "x", "2") == TxnResult::LockWait);
        assert(m.commit(t1) == TxnResult::Ok);
        // t1 already committed a newer version; t2's commit must fail-stop
        m.write(t2, "x", "2");
        assert(m.commit(t2) == TxnResult::SerializationFailure);
        std::printf("[Track B] MVCC first-updater-wins: T2 -> SERIALIZATION_FAILURE  OK\n");
    }

    // --- MVCC: deadlock picks the youngest victim --------------------------
    {
        MVCCManager m;
        auto s = m.begin(); m.write(s, "a", "0"); m.write(s, "b", "0"); m.commit(s);
        auto t1 = m.begin();   // older
        auto t2 = m.begin();   // younger
        m.write(t1, "a", "1");
        m.write(t2, "b", "1");
        m.write(t1, "b", "1");                 // t1 waits on t2
        TxnResult r = m.write(t2, "a", "1");   // closes the cycle
        assert(r == TxnResult::Aborted && "younger T2 is the victim");
        assert(m.last_victim() == t2);
        std::printf("[Track B] MVCC deadlock: youngest (T%llu) aborted  OK\n",
                    (unsigned long long)m.last_victim());
    }

    // --- 2PL: a read BLOCKS under a concurrent write (the contrast) ---------
    {
        TwoPLManager p;
        auto w = p.begin();
        auto r = p.begin();
        std::string v;
        assert(p.write(w, "k", "99") == TxnResult::Ok);     // X-lock on k
        TxnResult rd = p.read(r, "k", v);
        assert(rd == TxnResult::LockWait && "2PL reader blocks behind the writer");
        std::printf("[Baseline] 2PL read under write -> LOCK_WAIT (MVCC would not block)\n");

        // after the writer commits, the reader proceeds and sees the new value
        p.commit(w);
        assert(p.read(r, "k", v) == TxnResult::Ok && v == "99");
        std::printf("[Baseline] 2PL read after writer commit -> 99  OK\n");
    }

    std::printf("[Track B] concurrency control: ALL CHECKS PASSED\n");
    return 0;
}
