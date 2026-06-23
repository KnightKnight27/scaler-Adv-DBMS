package minidb.common;

import java.util.*;

/**
 * Shared type definitions used across all MiniDB layers.
 *
 * MiniDB supports two column types to keep the engine focused on database
 * internals rather than a rich type system:
 *   INT    -> stored as a Java Integer
 *   STRING -> stored as a Java String (variable length, length-prefixed on disk)
 */
public final class Types {

    public enum ColType { INT, STRING }

    /** A single column definition. */
    public static final class Column {
        public final String name;
        public final ColType type;
        public Column(String name, ColType type) {
            this.name = name;
            this.type = type;
        }
    }

    /** Ordered set of columns describing a table's rows. */
    public static final class Schema {
        public final List<Column> columns;
        public Schema(List<Column> columns) { this.columns = columns; }

        public int size() { return columns.size(); }

        public int indexOf(String colName) {
            for (int i = 0; i < columns.size(); i++)
                if (columns.get(i).name.equalsIgnoreCase(colName)) return i;
            return -1;
        }

        public Column column(int i) { return columns.get(i); }
    }

    /**
     * Record Identifier: physical address of a tuple = (pageId, slot).
     * This is what a B+ tree index points to, and how the buffer pool /
     * heap file locate a row.
     */
    public static final class RID {
        public final int pageId;
        public final int slot;
        public RID(int pageId, int slot) { this.pageId = pageId; this.slot = slot; }

        @Override public boolean equals(Object o) {
            if (!(o instanceof RID)) return false;
            RID r = (RID) o;
            return pageId == r.pageId && slot == r.slot;
        }
        @Override public int hashCode() { return pageId * 31 + slot; }
        @Override public String toString() { return "RID(" + pageId + "," + slot + ")"; }
    }

    /**
     * A logical row. Values are boxed Objects (Integer / String) aligned to
     * the table schema. Tuples carry an optional RID once they are stored,
     * and MVCC version metadata for the concurrency extension.
     */
    public static final class Tuple {
        public final Object[] values;
        public RID rid;            // physical location (null if not yet stored)

        // --- MVCC version metadata (used only by the MVCC extension) ---
        public long beginTs = 0;   // txn id / timestamp that created this version
        public long endTs = Long.MAX_VALUE; // txn id that deleted/superseded it
        public boolean deleted = false;

        public Tuple(Object[] values) { this.values = values; }

        public Object get(int i) { return values[i]; }

        public Tuple copy() {
            Tuple t = new Tuple(Arrays.copyOf(values, values.length));
            t.rid = rid;
            t.beginTs = beginTs;
            t.endTs = endTs;
            t.deleted = deleted;
            return t;
        }

        @Override public String toString() { return Arrays.toString(values); }
    }
}
