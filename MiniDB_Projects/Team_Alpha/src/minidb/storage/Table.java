package minidb.storage;

import minidb.common.Types.*;
import minidb.recovery.WAL;
import java.io.*;
import java.util.*;

/**
 * Table is a heap file: an unordered collection of tuples spread across pages.
 * Each table owns its OWN heap file + buffer pool, so every page is interpreted
 * with exactly one schema. This is the bridge between physical pages and the
 * logical relational model.
 *
 * Every mutating operation writes a WAL record BEFORE the page is modified
 * (write-ahead logging), which is what makes crash recovery possible.
 */
public final class Table {
    public final String name;
    public final Schema schema;
    private final BufferPool pool;
    private final WAL wal;
    private final List<Integer> pageIds = new ArrayList<>();

    public Table(String name, Schema schema, DiskManager disk, WAL wal, int poolSize) {
        this.name = name;
        this.schema = schema;
        this.pool = new BufferPool(disk, schema, poolSize);
        this.wal = wal;
        for (int i = 0; i < disk.numPages(); i++) pageIds.add(i);
    }

    public BufferPool pool() { return pool; }
    public void registerPage(int pageId) { if (!pageIds.contains(pageId)) pageIds.add(pageId); }
    public List<Integer> pageIds() { return pageIds; }

    public RID insert(Tuple t, long txnId) {
        for (int pid : pageIds) {
            Page p = pool.fetchPage(pid);
            int slot = p.insert(t);
            if (slot >= 0) {
                RID rid = new RID(pid, slot);
                t.rid = rid;
                if (wal != null) wal.logInsert(txnId, name, rid, t);
                pool.unpin(p, true);
                return rid;
            }
            pool.unpin(p, false);
        }
        Page p = pool.newPage();
        registerPage(p.getPageId());
        int slot = p.insert(t);
        RID rid = new RID(p.getPageId(), slot);
        t.rid = rid;
        if (wal != null) wal.logInsert(txnId, name, rid, t);
        pool.unpin(p, true);
        return rid;
    }

    public Tuple read(RID rid) {
        Page p = pool.fetchPage(rid.pageId);
        Tuple t = p.read(rid.slot);
        pool.unpin(p, false);
        return t;
    }

    public void delete(RID rid, long txnId) {
        Page p = pool.fetchPage(rid.pageId);
        Tuple before = p.read(rid.slot);
        if (before != null && wal != null) wal.logDelete(txnId, name, rid, before);
        p.delete(rid.slot);
        pool.unpin(p, true);
    }

    public void update(RID rid, Tuple newTuple, long txnId) {
        Page p = pool.fetchPage(rid.pageId);
        Tuple before = p.read(rid.slot);
        if (wal != null) wal.logUpdate(txnId, name, rid, before, newTuple);
        if (!p.update(rid.slot, newTuple)) {
            p.delete(rid.slot);
            pool.unpin(p, true);
            insert(newTuple, txnId);
            return;
        }
        pool.unpin(p, true);
    }

    public List<Tuple> scan() {
        List<Tuple> out = new ArrayList<>();
        for (int pid : pageIds) {
            Page p = pool.fetchPage(pid);
            out.addAll(p.scan());
            pool.unpin(p, false);
        }
        return out;
    }

    public void redoInsert(RID rid, Tuple t) {
        while (pool.numPages() <= rid.pageId) { Page np = pool.newPage(); registerPage(np.getPageId()); pool.unpin(np, true); }
        registerPage(rid.pageId);
        Page p = pool.fetchPage(rid.pageId);
        p.insert(t);
        pool.unpin(p, true);
    }

    public void flush() { pool.flushAll(); }
}
