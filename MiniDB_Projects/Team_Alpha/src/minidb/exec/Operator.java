package minidb.exec;

import minidb.common.Types.*;
import minidb.index.BPlusTree;
import minidb.sql.Catalog;
import minidb.storage.Table;
import java.util.*;

/**
 * Physical query operators in the Volcano / iterator model.
 *
 * Every operator exposes open() / next() / close(). A query plan is a TREE of
 * operators; calling next() on the root pulls one row, which recursively pulls
 * from children. This is exactly how real engines (Postgres, etc.) execute plans
 * and is what lets us compose scans, filters, and joins uniformly.
 *
 * Rows flowing between operators are represented as a Row: a map from
 * "table.column" -> value, so joins can carry columns from multiple tables.
 */
public abstract class Operator {
    public abstract void open();
    public abstract Row next();   // returns null when exhausted
    public abstract void close();

    public static final class Row {
        public final Map<String, Object> cols = new LinkedHashMap<>();
        public Row() {}
        public Row(Row a, Row b) { cols.putAll(a.cols); cols.putAll(b.cols); }
        public Object get(String key) { return cols.get(key); }
    }

    // ---------------- Sequential (table) scan ----------------
    public static final class SeqScan extends Operator {
        private final Table table;
        private final String alias;
        private Iterator<Tuple> it;
        public long rowsProduced = 0;

        public SeqScan(Table table, String alias) { this.table = table; this.alias = alias; }

        @Override public void open() { it = table.scan().iterator(); }
        @Override public Row next() {
            while (it.hasNext()) {
                Tuple t = it.next();
                if (t.deleted) continue;
                rowsProduced++;
                return toRow(t, table, alias);
            }
            return null;
        }
        @Override public void close() { it = null; }
    }

    // ---------------- Index scan (point lookup via B+ tree) ----------------
    public static final class IndexScan extends Operator {
        private final Table table;
        private final String alias;
        private final BPlusTree index;
        private final int key;
        private boolean done = false;
        public long rowsProduced = 0;

        public IndexScan(Table table, String alias, BPlusTree index, int key) {
            this.table = table; this.alias = alias; this.index = index; this.key = key;
        }
        @Override public void open() { done = false; }
        @Override public Row next() {
            if (done) return null;
            done = true;
            RID rid = index.search(key);
            if (rid == null) return null;
            Tuple t = table.read(rid);
            if (t == null || t.deleted) return null;
            rowsProduced++;
            return toRow(t, table, alias);
        }
        @Override public void close() {}
    }

    // ---------------- Filter (WHERE on a single relation) ----------------
    public static final class Filter extends Operator {
        private final Operator child;
        private final java.util.function.Predicate<Row> pred;
        public Filter(Operator child, java.util.function.Predicate<Row> pred) {
            this.child = child; this.pred = pred;
        }
        @Override public void open() { child.open(); }
        @Override public Row next() {
            Row r;
            while ((r = child.next()) != null) if (pred.test(r)) return r;
            return null;
        }
        @Override public void close() { child.close(); }
    }

    // ---------------- Nested-loop join ----------------
    public static final class NestedLoopJoin extends Operator {
        private final Operator left, right;
        private final java.util.function.BiPredicate<Row, Row> cond;
        private Row leftRow;
        public NestedLoopJoin(Operator left, Operator right,
                              java.util.function.BiPredicate<Row, Row> cond) {
            this.left = left; this.right = right; this.cond = cond;
        }
        @Override public void open() { left.open(); leftRow = left.next(); right.open(); }
        @Override public Row next() {
            while (leftRow != null) {
                Row rr;
                while ((rr = right.next()) != null) {
                    if (cond == null || cond.test(leftRow, rr)) return new Row(leftRow, rr);
                }
                // advance outer, restart inner
                leftRow = left.next();
                right.close(); right.open();
            }
            return null;
        }
        @Override public void close() { left.close(); right.close(); }
    }

    // ---------------- Projection ----------------
    public static final class Project extends Operator {
        private final Operator child;
        private final List<String> cols; // null/contains "*" => all
        public Project(Operator child, List<String> cols) { this.child = child; this.cols = cols; }
        @Override public void open() { child.open(); }
        @Override public Row next() {
            Row r = child.next();
            if (r == null) return null;
            if (cols == null || cols.contains("*")) return r;
            Row out = new Row();
            for (String c : cols) {
                Object v = r.get(c);
                if (v == null) { // try unqualified match
                    for (Map.Entry<String,Object> e : r.cols.entrySet())
                        if (e.getKey().endsWith("." + c)) { v = e.getValue(); break; }
                }
                out.cols.put(c, v);
            }
            return out;
        }
        @Override public void close() { child.close(); }
    }

    // helper: turn a stored Tuple into a qualified Row
    static Row toRow(Tuple t, Table table, String alias) {
        Row r = new Row();
        String q = (alias != null) ? alias : table.name;
        for (int i = 0; i < table.schema.size(); i++)
            r.cols.put(q + "." + table.schema.column(i).name, t.get(i));
        return r;
    }
}
