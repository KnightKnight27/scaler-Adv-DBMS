package sql

import "testing"

func TestParseCreate(t *testing.T) {
	st, err := Parse("CREATE TABLE users (id INT PRIMARY KEY, name TEXT)")
	if err != nil {
		t.Fatal(err)
	}
	ct, ok := st.(*CreateTable)
	if !ok {
		t.Fatalf("got %T", st)
	}
	if ct.Table != "users" || len(ct.Columns) != 2 || !ct.Columns[0].PrimaryKey {
		t.Fatalf("bad create: %+v", ct)
	}
}

func TestParseCreateRequiresPK(t *testing.T) {
	if _, err := Parse("CREATE TABLE t (a INT, b TEXT)"); err == nil {
		t.Fatal("expected error for missing primary key")
	}
}

func TestParseInsertMultiRow(t *testing.T) {
	st, err := Parse("INSERT INTO t (a, b) VALUES (1, 'x'), (2, 'y')")
	if err != nil {
		t.Fatal(err)
	}
	ins := st.(*Insert)
	if len(ins.Rows) != 2 || len(ins.Columns) != 2 {
		t.Fatalf("bad insert: %+v", ins)
	}
}

func TestParseSelectJoin(t *testing.T) {
	st, err := Parse("SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.uid WHERE o.total > 100")
	if err != nil {
		t.Fatal(err)
	}
	sel := st.(*Select)
	if len(sel.Tables) != 2 {
		t.Fatalf("expected 2 tables, got %d", len(sel.Tables))
	}
	if sel.Tables[0].Alias != "u" || sel.Tables[1].Alias != "o" {
		t.Fatalf("aliases: %+v", sel.Tables)
	}
	// one ON predicate + one WHERE predicate
	if len(sel.Predicates) != 2 {
		t.Fatalf("expected 2 predicates, got %d", len(sel.Predicates))
	}
}

func TestParseAggGroupBy(t *testing.T) {
	st, err := Parse("SELECT dept, COUNT(*) FROM emp GROUP BY dept")
	if err != nil {
		t.Fatal(err)
	}
	sel := st.(*Select)
	if len(sel.GroupBy) != 1 {
		t.Fatalf("expected group by, got %+v", sel.GroupBy)
	}
	agg, ok := sel.Items[1].Expr.(*AggCall)
	if !ok || agg.Func != "COUNT" || !agg.Star {
		t.Fatalf("expected COUNT(*), got %+v", sel.Items[1].Expr)
	}
}

func TestParseWhereAndChain(t *testing.T) {
	st, err := Parse("SELECT * FROM t WHERE a = 1 AND b > 2 AND c <= 3")
	if err != nil {
		t.Fatal(err)
	}
	sel := st.(*Select)
	if len(sel.Predicates) != 3 {
		t.Fatalf("AND chain should split into 3 predicates, got %d", len(sel.Predicates))
	}
}

func TestParseErrors(t *testing.T) {
	bad := []string{
		"SELECT FROM t",
		"INSERT INTO t VALUES",
		"CREATE TABLE",
		"SELCT * FROM t",
	}
	for _, q := range bad {
		if _, err := Parse(q); err == nil {
			t.Fatalf("expected parse error for %q", q)
		}
	}
}
