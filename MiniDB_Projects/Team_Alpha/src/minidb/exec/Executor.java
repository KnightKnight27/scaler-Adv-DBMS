package minidb.exec;

import minidb.common.Types.*;
import minidb.sql.*;
import minidb.storage.Table;
import minidb.txn.*;
import java.util.*;

/**
 * Executor turns a parsed Ast.Stmt into actual work against the storage engine,
 * the catalog, the optimizer, and the transaction/lock managers.
 *
 * It maintains the "current transaction". DML statements run inside an explicit
 * BEGIN..COMMIT, or in auto-commit mode (each statement is its own transaction)
 * when no BEGIN is active. Locks are taken under 2PL: shared locks for reads,
 * exclusive locks for writes, held until commit.
 */
public final class Executor {
    private final Catalog catalog;
    private final TransactionManager txnManager;
    private final Optimizer optimizer;
    public boolean showPlan = false;

    private Transaction current = null; // explicit transaction, if any

    public Executor(Catalog catalog, TransactionManager txnManager) {
        this.catalog = catalog;
        this.txnManager = txnManager;
        this.optimizer = new Optimizer(catalog);
    }

    public String execute(String sql) {
        Ast.Stmt stmt = new Parser(sql).parse();

        if (stmt instanceof Ast.Begin) {
            if (current != null) return "Already in a transaction.";
            current = txnManager.begin();
            return "BEGIN (txn #" + current.id + ")";
        }
        if (stmt instanceof Ast.Commit) {
            if (current == null) return "No active transaction.";
            long id = current.id;
            txnManager.commit(current);
            current = null;
            return "COMMIT (txn #" + id + ")";
        }
        if (stmt instanceof Ast.Abort) {
            if (current == null) return "No active transaction.";
            long id = current.id;
            txnManager.abort(current);
            current = null;
            return "ABORT (txn #" + id + ")";
        }

        // DDL/DML: run inside current txn, or auto-commit
        boolean autoCommit = (current == null);
        Transaction txn = autoCommit ? txnManager.begin() : current;
        try {
            String result = run(stmt, txn);
            if (autoCommit) txnManager.commit(txn);
            return result;
        } catch (LockManager.DeadlockException de) {
            txnManager.abort(txn);
            if (!autoCommit) current = null;
            return "ABORTED (deadlock): " + de.getMessage();
        } catch (RuntimeException e) {
            if (autoCommit) txnManager.abort(txn);
            throw e;
        }
    }

    private String run(Ast.Stmt stmt, Transaction txn) {
        if (stmt instanceof Ast.CreateTable) return doCreateTable((Ast.CreateTable) stmt);
        if (stmt instanceof Ast.CreateIndex) return doCreateIndex((Ast.CreateIndex) stmt);
        if (stmt instanceof Ast.Insert)      return doInsert((Ast.Insert) stmt, txn);
        if (stmt instanceof Ast.Delete)      return doDelete((Ast.Delete) stmt, txn);
        if (stmt instanceof Ast.Select)      return doSelect((Ast.Select) stmt, txn);
        return "Unsupported statement.";
    }

    private String doCreateTable(Ast.CreateTable ct) {
        catalog.createTable(ct.table, new Schema(ct.columns), ct.primaryKey);
        return "Table '" + ct.table + "' created"
                + (ct.primaryKey != null ? " (PK=" + ct.primaryKey + ", indexed)" : "") + ".";
    }

    private String doCreateIndex(Ast.CreateIndex ci) {
        catalog.createIndex(ci.table, ci.column);
        return "Index created on " + ci.table + "(" + ci.column + ").";
    }

    private String doInsert(Ast.Insert ins, Transaction txn) {
        Table t = catalog.table(ins.table);
        if (t == null) throw new RuntimeException("No such table: " + ins.table);
        // exclusive (write) lock at table granularity for simplicity of 2PL demo
        txnManager.locks().acquire(txn, "T:" + ins.table, LockManager.Mode.EXCLUSIVE);

        Object[] vals = ins.values.toArray();
        Tuple tup = new Tuple(vals);
        tup.beginTs = txn.id;
        RID rid = t.insert(tup, txn.id);
        catalog.indexInsert(ins.table, tup);
        // register undo: on abort, delete the inserted row
        final RID frid = rid;
        txn.undoActions.add(() -> t.delete(frid, 0));
        catalog.persist();
        return "1 row inserted (" + rid + ").";
    }

    private String doDelete(Ast.Delete d, Transaction txn) {
        Table t = catalog.table(d.table);
        if (t == null) throw new RuntimeException("No such table: " + d.table);
        txnManager.locks().acquire(txn, "T:" + d.table, LockManager.Mode.EXCLUSIVE);
        int count = 0;
        Schema sc = t.schema;
        for (Tuple tup : t.scan()) {
            if (tup.deleted) continue;
            if (d.where == null || matchesSingle(d.where, sc, tup, d.table)) {
                final Tuple before = tup.copy();
                t.delete(tup.rid, txn.id);
                catalog.indexDelete(d.table, tup);
                txn.undoActions.add(() -> t.insert(before, 0));
                count++;
            }
        }
        catalog.persist();
        return count + " row(s) deleted.";
    }

    private boolean matchesSingle(Ast.Predicate p, Schema sc, Tuple t, String table) {
        int ci = sc.indexOf(p.leftCol);
        if (ci < 0) return false;
        return Optimizer.compare(t.get(ci), p.op, p.rhsValue);
    }

    private String doSelect(Ast.Select s, Transaction txn) {
        // shared (read) locks under 2PL
        for (String tbl : s.fromTables) {
            if (catalog.table(tbl) == null) throw new RuntimeException("No such table: " + tbl);
            txnManager.locks().acquire(txn, "T:" + tbl, LockManager.Mode.SHARED);
        }
        Operator plan = optimizer.buildPlan(s);
        StringBuilder out = new StringBuilder();
        if (showPlan) out.append("-- PLAN --\n").append(optimizer.explain).append("----------\n");

        plan.open();
        Operator.Row row;
        List<Operator.Row> rows = new ArrayList<>();
        while ((row = plan.next()) != null) rows.add(row);
        plan.close();

        if (rows.isEmpty()) { out.append("(0 rows)"); return out.toString(); }
        // header
        List<String> headers = new ArrayList<>(rows.get(0).cols.keySet());
        out.append(String.join(" | ", headers)).append('\n');
        out.append("-".repeat(Math.max(3, String.join(" | ", headers).length()))).append('\n');
        for (Operator.Row r : rows) {
            StringJoiner sj = new StringJoiner(" | ");
            for (String h : headers) sj.add(String.valueOf(r.get(h)));
            out.append(sj).append('\n');
        }
        out.append("(").append(rows.size()).append(" rows)");
        return out.toString();
    }
}
