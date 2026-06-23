package minidb.sql;

import minidb.common.Types.*;
import java.util.*;

/**
 * A hand-written recursive-descent parser for MiniDB's SQL subset.
 *
 * Supported grammar (case-insensitive keywords):
 *   CREATE TABLE t (col TYPE [PRIMARY KEY], ...)
 *   CREATE INDEX ON t (col)
 *   INSERT INTO t VALUES (v1, v2, ...)
 *   SELECT cols FROM t1 [, t2] [WHERE pred [AND pred ...]]
 *   DELETE FROM t [WHERE pred]
 *   BEGIN | COMMIT | ABORT
 *
 * A predicate is: <col> <op> <literal|col>, where col may be qualified t.col.
 *
 * The parser turns text into an Ast.Stmt; it does no semantic checking — that
 * happens in the executor against the catalog.
 */
public final class Parser {
    private final List<String> toks;
    private int pos = 0;

    public Parser(String sql) { this.toks = tokenize(sql); }

    public Ast.Stmt parse() {
        String kw = peek().toUpperCase();
        switch (kw) {
            case "CREATE": return parseCreate();
            case "INSERT": return parseInsert();
            case "SELECT": return parseSelect();
            case "DELETE": return parseDelete();
            case "BEGIN":  next(); return new Ast.Begin();
            case "COMMIT": next(); return new Ast.Commit();
            case "ABORT": case "ROLLBACK": next(); return new Ast.Abort();
            default: throw err("Unknown statement: " + kw);
        }
    }

    // ---- CREATE TABLE / CREATE INDEX ----
    private Ast.Stmt parseCreate() {
        expect("CREATE");
        if (peek().equalsIgnoreCase("INDEX")) {
            next(); expect("ON");
            Ast.CreateIndex ci = new Ast.CreateIndex();
            ci.table = next();
            expect("(");
            ci.column = next();
            expect(")");
            return ci;
        }
        expect("TABLE");
        Ast.CreateTable ct = new Ast.CreateTable();
        ct.table = next();
        expect("(");
        while (true) {
            String col = next();
            String type = next().toUpperCase();
            ColType ct2 = type.startsWith("INT") ? ColType.INT : ColType.STRING;
            ct.columns.add(new Column(col, ct2));
            if (peek().equalsIgnoreCase("PRIMARY")) {
                next(); expect("KEY");
                ct.primaryKey = col;
            }
            if (peek().equals(",")) { next(); continue; }
            break;
        }
        expect(")");
        return ct;
    }

    // ---- INSERT ----
    private Ast.Stmt parseInsert() {
        expect("INSERT"); expect("INTO");
        Ast.Insert ins = new Ast.Insert();
        ins.table = next();
        expect("VALUES"); expect("(");
        while (true) {
            ins.values.add(literal(next()));
            if (peek().equals(",")) { next(); continue; }
            break;
        }
        expect(")");
        return ins;
    }

    // ---- DELETE ----
    private Ast.Stmt parseDelete() {
        expect("DELETE"); expect("FROM");
        Ast.Delete d = new Ast.Delete();
        d.table = next();
        if (!atEnd() && peek().equalsIgnoreCase("WHERE")) {
            next();
            d.where = parsePredicate();
        }
        return d;
    }

    // ---- SELECT ----
    private Ast.Stmt parseSelect() {
        expect("SELECT");
        Ast.Select s = new Ast.Select();
        if (peek().equals("*")) { next(); s.projections.add("*"); }
        else {
            while (true) {
                s.projections.add(next());
                if (peek().equals(",")) { next(); continue; }
                break;
            }
        }
        expect("FROM");
        while (true) {
            s.fromTables.add(next());
            if (peek().equals(",")) { next(); continue; }
            break;
        }
        if (!atEnd() && peek().equalsIgnoreCase("WHERE")) {
            next();
            while (true) {
                s.wheres.add(parsePredicate());
                if (!atEnd() && peek().equalsIgnoreCase("AND")) { next(); continue; }
                break;
            }
        }
        return s;
    }

    private Ast.Predicate parsePredicate() {
        Ast.Predicate p = new Ast.Predicate();
        String left = next();
        String[] lq = qualify(left);
        p.leftTable = lq[0]; p.leftCol = lq[1];
        p.op = next();
        String right = next();
        if (isLiteralToken(right)) {
            p.rhsIsColumn = false;
            p.rhsValue = literal(right);
        } else {
            p.rhsIsColumn = true;
            String[] rq = qualify(right);
            p.rightTable = rq[0]; p.rightCol = rq[1];
        }
        return p;
    }

    private String[] qualify(String tok) {
        int dot = tok.indexOf('.');
        if (dot >= 0) return new String[]{ tok.substring(0, dot), tok.substring(dot + 1) };
        return new String[]{ null, tok };
    }

    private boolean isLiteralToken(String t) {
        if (t.startsWith("'")) return true;
        if (t.isEmpty()) return false;
        char c = t.charAt(0);
        return Character.isDigit(c) || c == '-';
    }

    private Object literal(String t) {
        if (t.startsWith("'")) return t.substring(1, t.length() - 1); // strip quotes
        try { return Integer.parseInt(t); } catch (NumberFormatException e) { return t; }
    }

    // ---- tokenizer ----
    private static List<String> tokenize(String sql) {
        List<String> out = new ArrayList<>();
        int i = 0, n = sql.length();
        while (i < n) {
            char c = sql.charAt(i);
            if (Character.isWhitespace(c)) { i++; continue; }
            if (c == ';') { i++; continue; }
            if (c == '\'') { // string literal
                int j = i + 1;
                StringBuilder sb = new StringBuilder("'");
                while (j < n && sql.charAt(j) != '\'') sb.append(sql.charAt(j++));
                sb.append('\'');
                out.add(sb.toString());
                i = j + 1;
                continue;
            }
            if ("(),*".indexOf(c) >= 0) { out.add(String.valueOf(c)); i++; continue; }
            if ("<>=!".indexOf(c) >= 0) { // operators, possibly two-char
                int j = i + 1;
                if (j < n && "<>=!".indexOf(sql.charAt(j)) >= 0) { out.add(sql.substring(i, j + 1)); i = j + 1; }
                else { out.add(String.valueOf(c)); i++; }
                continue;
            }
            int j = i; // identifier / number / keyword
            while (j < n && !Character.isWhitespace(sql.charAt(j))
                    && "(),*<>=!;'".indexOf(sql.charAt(j)) < 0) j++;
            out.add(sql.substring(i, j));
            i = j;
        }
        return out;
    }

    // ---- token cursor helpers ----
    private String peek() { return atEnd() ? "" : toks.get(pos); }
    private String next() { return toks.get(pos++); }
    private boolean atEnd() { return pos >= toks.size(); }
    private void expect(String kw) {
        String t = next();
        if (!t.equalsIgnoreCase(kw)) throw err("Expected '" + kw + "' but got '" + t + "'");
    }
    private RuntimeException err(String m) { return new RuntimeException("Parse error: " + m); }
}
