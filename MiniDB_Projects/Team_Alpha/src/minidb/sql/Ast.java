package minidb.sql;

import minidb.common.Types.*;
import java.util.*;

/** Abstract syntax tree node types produced by the Parser. */
public final class Ast {

    public interface Stmt {}

    public static final class CreateTable implements Stmt {
        public String table;
        public List<Column> columns = new ArrayList<>();
        public String primaryKey;
    }

    public static final class CreateIndex implements Stmt {
        public String table, column;
    }

    public static final class Insert implements Stmt {
        public String table;
        public List<Object> values = new ArrayList<>();
    }

    public static final class Delete implements Stmt {
        public String table;
        public Predicate where; // may be null
    }

    /** A simple comparison predicate: column OP value, or column OP column (joins). */
    public static final class Predicate {
        public String leftTable, leftCol;   // qualified column on the left
        public String op;                    // = < > <= >= !=
        public boolean rhsIsColumn;
        public Object rhsValue;              // literal (if rhsIsColumn == false)
        public String rightTable, rightCol;  // column (if rhsIsColumn == true)
    }

    public static final class Select implements Stmt {
        public List<String> projections = new ArrayList<>(); // "*" or qualified cols
        public List<String> fromTables = new ArrayList<>();
        public List<Predicate> wheres = new ArrayList<>();   // ANDed together
    }

    public static final class Begin implements Stmt {}
    public static final class Commit implements Stmt {}
    public static final class Abort implements Stmt {}
}
