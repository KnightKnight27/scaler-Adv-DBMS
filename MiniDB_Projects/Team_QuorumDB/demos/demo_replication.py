"""Demo: Track D — primary-replica replication and failover (two nodes).

Starts a primary that streams its redo log over a socket to a replica, shows
the replica serving consistent reads as live writes arrive, reports
replication lag, then kills the primary and promotes the replica so it accepts
writes (failover).
"""

import time

from _bootstrap import rule, scratch

from minidb.engine import Database
from minidb.replication.primary import Primary
from minidb.replication.replica import Replica


def _wait_caught_up(primary: Database, replica: Replica, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline and replica.applied_lsn < primary.log.current_lsn:
        time.sleep(0.05)


def main() -> None:
    primary_db = Database(scratch("primary"))
    replica_db = Database(scratch("replica"))

    rule("1. Primary loads data and starts serving its redo stream")
    c = primary_db.connect()
    c.execute("CREATE TABLE kv (k INT PRIMARY KEY, v TEXT)")
    c.execute("INSERT INTO kv VALUES (1,'one'),(2,'two'),(3,'three')")
    prim = Primary(primary_db)
    port = prim.serve("127.0.0.1", 0)
    print(f"primary listening on 127.0.0.1:{port}")

    rule("2. Replica connects and catches up")
    rep = Replica(replica_db)
    rep.start_following("127.0.0.1", port)
    _wait_caught_up(primary_db, rep)
    print("replica reads:", sorted(r[:] for r in rep.query("SELECT k, v FROM kv").rows))

    rule("3. Live writes on the primary stream to the replica")
    c.execute("INSERT INTO kv VALUES (4,'four')")
    c.execute("DELETE FROM kv WHERE k = 1")
    _wait_caught_up(primary_db, rep)
    print("replica reads:", sorted(r[:] for r in rep.query("SELECT k, v FROM kv").rows))
    time.sleep(0.3)  # let an ACK round-trip
    print("replication lag (primary_lsn - replica_acked):", prim.replication_lag())

    rule("4. Primary fails -> promote the replica (failover)")
    prim.stop()
    print("primary stopped.")
    rep.promote()
    print("replica promoted to writable.")
    rc = rep.db.connect()
    rc.execute("INSERT INTO kv VALUES (5,'written-after-failover')")
    print("new primary (ex-replica) reads:",
          sorted(r[:] for r in rc.execute("SELECT k, v FROM kv").rows))

    rep.stop()
    primary_db.close()
    replica_db.close()


if __name__ == "__main__":
    main()
