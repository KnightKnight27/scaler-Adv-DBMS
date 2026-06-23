package minidb.exec;

import minidb.common.Types.*;
import minidb.index.BPlusTree;
import minidb.sql.*;
import minidb.storage.Table;
import java.util.*;

/**
 * A cost-based optimizer. Given a parsed SELECT, it builds a physical operator
 * tree, making two kinds of cost-driven decisions:
 *
 *   1. ACCESS PATH: for each table, choose a full table scan vs. an index scan.
 *      An index scan is chosen when a WHERE predicate is an equality on an
 *      indexed column (very low selectivity => few rows => cheap).
 *
 *   2. JOIN ORDER: for multi-table queries, order tables so the smallest
 *      estimated relation is the outer (driving) relation of the nested-loop
 *      join, minimizing total tuples examined.
 *
 * SELECTIVITY ESTIMATION: we estimate the fraction of rows a predicate keeps.
 *   - equality on a column: 1 / distinctValues   (default 0.1 if unknown)
 *   - range (<, >):          ~0.33
 * COST MODEL: cost(scan) ≈ rows; cost(indexScan) ≈ log(rows) + matches.
 *
 * The optimizer prints its chosen plan + estimates so the choice is visible in
 * the demo and defensible in the viva.
 */
public final class Optimizer {
    private final Catalog catalog;
    public StringBuilder explain = new StringBuilder();

    public Optimizer(Catalog catalog) { this.catalog = catalog; }

    /** Estimated row count of a table (scan its pages once; cached would be better). */
    private int rowCount(String table) {
        return catalog.rowCount(table);
    }

    /** Estimate selectivity of a predicate against a base table. */
    double selectivity(Ast.Predicate p, String table) {
        if (p.rhsIsColumn) return 0.1; // join predicate, handled separately
        if (p.op.equals("=")) {
            // distinct-value heuristic: assume uniform; sel = 1 / distinct
            int distinct = estimateDistinct(table, p.leftCol);
            return 1.0 / Math.max(1, distinct);
        }
        return 0.33; // range predicate
    }

    private int estimateDistinct(String table, String col) {
        return catalog.distinctCount(table, col);
    }

    /** Build the optimized physical plan for a SELECT. */
    public Operator buildPlan(Ast.Select s) {
        explain.setLength(0);
        // resolve aliases: each table used directly (alias == table name)
        List<String> tables = s.fromTables;

        // group single-table predicates and join predicates
        Map<String, List<Ast.Predicate>> single = new HashMap<>();
        List<Ast.Predicate> joins = new ArrayList<>();
        for (String t : tables) single.put(t, new ArrayList<>());
        for (Ast.Predicate p : s.wheres) {
            if (p.rhsIsColumn) joins.add(p);
            else single.computeIfAbsent(resolveTable(p, tables), k -> new ArrayList<>()).add(p);
        }

        // --- estimate each table's output size after its local predicates ---
        Map<String, Double> estSize = new HashMap<>();
        Map<String, Operator> baseOps = new HashMap<>();
        for (String t : tables) {
            int n = rowCount(t);
            double sel = 1.0;
            Operator op = chooseAccessPath(t, single.get(t));
            for (Ast.Predicate p : single.get(t)) sel *= selectivity(p, t);
            double est = Math.max(1, n * sel);
            estSize.put(t, est);
            // wrap remaining (non-indexed) single-table predicates in a Filter
            Operator filtered = wrapSingleFilters(op, t, single.get(t));
            baseOps.put(t, filtered);
            explain.append(String.format("  %s: base rows=%d, est after filters=%.1f%n", t, n, est));
        }

        // --- JOIN ORDERING: sort tables by estimated size ascending (greedy) ---
        List<String> order = new ArrayList<>(tables);
        order.sort(Comparator.comparingDouble(estSize::get));
        if (tables.size() > 1)
            explain.append("  join order (smallest first): ").append(order).append('\n');

        Operator plan = baseOps.get(order.get(0));
        for (int i = 1; i < order.size(); i++) {
            String rt = order.get(i);
            Operator right = baseOps.get(rt);
            java.util.function.BiPredicate<Operator.Row, Operator.Row> cond =
                    buildJoinCond(joins);
            plan = new Operator.NestedLoopJoin(plan, right, cond);
        }

        // --- projection on top ---
        return new Operator.Project(plan, s.projections);
    }

    /** Decide table scan vs index scan for one table given its local predicates. */
    private Operator chooseAccessPath(String table, List<Ast.Predicate> preds) {
        Table tbl = catalog.table(table);
        // look for an equality predicate on an indexed integer column
        for (Ast.Predicate p : preds) {
            if (p.op.equals("=") && p.rhsValue instanceof Integer) {
                BPlusTree idx = catalog.index(table, p.leftCol);
                if (idx != null) {
                    double sel = selectivity(p, table);
                    double scanCost = rowCount(table);
                    double idxCost = Math.log(Math.max(2, rowCount(table))) / Math.log(2) + 1;
                    explain.append(String.format(
                        "  access[%s]: INDEX SCAN on %s=%s (sel=%.3f, idxCost=%.1f < scanCost=%.1f)%n",
                        table, p.leftCol, p.rhsValue, sel, idxCost, scanCost));
                    return new Operator.IndexScan(tbl, table, idx, (Integer) p.rhsValue);
                }
            }
        }
        explain.append(String.format("  access[%s]: SEQ SCAN (no usable index)%n", table));
        return new Operator.SeqScan(tbl, table);
    }

    /** Wrap remaining single-table predicates not already satisfied by an index. */
    private Operator wrapSingleFilters(Operator op, String table, List<Ast.Predicate> preds) {
        boolean indexUsed = op instanceof Operator.IndexScan;
        List<Ast.Predicate> remaining = new ArrayList<>();
        boolean skippedIndexEq = false;
        for (Ast.Predicate p : preds) {
            if (indexUsed && !skippedIndexEq && p.op.equals("=") && p.rhsValue instanceof Integer
                    && catalog.index(table, p.leftCol) != null) {
                skippedIndexEq = true; // this one is handled by the index scan
                continue;
            }
            remaining.add(p);
        }
        if (remaining.isEmpty()) return op;
        return new Operator.Filter(op, row -> {
            for (Ast.Predicate p : remaining) {
                String key = table + "." + p.leftCol;
                Object v = row.get(key);
                if (!compare(v, p.op, p.rhsValue)) return false;
            }
            return true;
        });
    }

    private java.util.function.BiPredicate<Operator.Row, Operator.Row> buildJoinCond(
            List<Ast.Predicate> joins) {
        if (joins.isEmpty()) return null;
        return (a, b) -> {
            for (Ast.Predicate p : joins) {
                Object lv = lookup(a, b, p.leftTable, p.leftCol);
                Object rv = lookup(a, b, p.rightTable, p.rightCol);
                if (!compare(lv, p.op, rv)) return false;
            }
            return true;
        };
    }

    private Object lookup(Operator.Row a, Operator.Row b, String table, String col) {
        String key = table + "." + col;
        if (a.cols.containsKey(key)) return a.get(key);
        if (b.cols.containsKey(key)) return b.get(key);
        // unqualified fallback
        for (Operator.Row r : new Operator.Row[]{a, b})
            for (Map.Entry<String,Object> e : r.cols.entrySet())
                if (e.getKey().endsWith("." + col)) return e.getValue();
        return null;
    }

    private String resolveTable(Ast.Predicate p, List<String> tables) {
        if (p.leftTable != null) return p.leftTable;
        // find which table has this column
        for (String t : tables)
            if (catalog.schema(t).indexOf(p.leftCol) >= 0) return t;
        return tables.get(0);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    static boolean compare(Object a, String op, Object b) {
        if (a == null || b == null) return false;
        int cmp;
        if (a instanceof Integer && b instanceof Integer) cmp = ((Integer) a).compareTo((Integer) b);
        else cmp = a.toString().compareTo(b.toString());
        switch (op) {
            case "=": case "==": return cmp == 0;
            case "!=": case "<>": return cmp != 0;
            case "<": return cmp < 0;
            case ">": return cmp > 0;
            case "<=": return cmp <= 0;
            case ">=": return cmp >= 0;
            default: return false;
        }
    }
}
