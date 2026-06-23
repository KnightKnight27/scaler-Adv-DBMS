// Command minidb is the command-line front end for the MiniDB engine. With no
// flags it starts an interactive SQL REPL; with --demo it runs one of the
// scripted demonstrations used in the project viva (crash recovery, deadlock
// detection, or MVCC snapshot isolation).
package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"strings"
	"text/tabwriter"

	"minidb/internal/engine"
	"minidb/internal/txn"
)

func main() {
	dir := flag.String("dir", "./minidb-data", "data directory for heap files, catalog and WAL")
	mode := flag.String("mode", "2pl", "concurrency control: 2pl or mvcc")
	demo := flag.String("demo", "", "run a scripted demo: crash | deadlock | mvcc")
	flag.Parse()

	if *demo != "" {
		runDemo(*demo)
		return
	}

	db, err := engine.Open(engine.Options{Dir: *dir, Mode: parseMode(*mode)})
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
	defer db.Close()

	repl(db)
}

func parseMode(s string) txn.Mode {
	if strings.EqualFold(s, "mvcc") {
		return txn.ModeMVCC
	}
	return txn.Mode2PL
}

// repl runs the interactive read-eval-print loop, buffering input until a
// semicolon terminates a statement.
func repl(db *engine.DB) {
	session := db.NewSession()
	fmt.Printf("MiniDB (Team XO) - concurrency: %s\n", db.Mode())
	fmt.Println("Enter SQL statements terminated by ';'. Type \\q to quit, \\help for help.")

	scanner := bufio.NewScanner(os.Stdin)
	scanner.Buffer(make([]byte, 1024*1024), 1024*1024)
	var buf strings.Builder
	prompt(session)

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		switch {
		case line == "\\q" || line == "\\quit":
			return
		case line == "\\help":
			printHelp()
			prompt(session)
			continue
		case line == "\\tables":
			fmt.Println(strings.Join(db.TableNames(), "  "))
			prompt(session)
			continue
		case line == "":
			prompt(session)
			continue
		}

		buf.WriteString(line)
		buf.WriteByte(' ')
		if !strings.Contains(line, ";") {
			fmt.Print("  ...> ")
			continue
		}

		stmt := strings.TrimSpace(buf.String())
		buf.Reset()
		runStatement(session, strings.TrimSuffix(stmt, ";"))
		prompt(session)
	}
}

func prompt(s *engine.Session) {
	if s.InTransaction() {
		fmt.Print("minidb*> ")
	} else {
		fmt.Print("minidb> ")
	}
}

func runStatement(s *engine.Session, stmt string) {
	if strings.TrimSpace(stmt) == "" {
		return
	}
	res, err := s.Execute(stmt)
	if err != nil {
		fmt.Println("error:", err)
		return
	}
	printResult(res)
}

func printResult(res *engine.Result) {
	if !res.IsQuery {
		if res.Message != "" {
			fmt.Println(res.Message)
		}
		return
	}
	if len(res.Rows) == 0 {
		fmt.Println("(0 rows)")
		return
	}
	w := tabwriter.NewWriter(os.Stdout, 0, 2, 2, ' ', 0)
	fmt.Fprintln(w, strings.Join(res.Columns, "\t"))
	seps := make([]string, len(res.Columns))
	for i, c := range res.Columns {
		seps[i] = strings.Repeat("-", len(c))
	}
	fmt.Fprintln(w, strings.Join(seps, "\t"))
	for _, row := range res.Rows {
		cells := make([]string, len(row))
		for i, v := range row {
			cells[i] = v.String()
		}
		fmt.Fprintln(w, strings.Join(cells, "\t"))
	}
	w.Flush()
	fmt.Printf("(%d row(s))\n", len(res.Rows))
}

func printHelp() {
	fmt.Println(`Supported SQL:
  CREATE TABLE t (col TYPE [PRIMARY KEY], ...)   types: INT, TEXT, BOOL
  CREATE INDEX ON t (col)
  INSERT INTO t [(cols)] VALUES (...), (...)
  SELECT col, ... | * | COUNT(*) FROM t [JOIN u ON t.a = u.b] [WHERE ...]
  DELETE FROM t [WHERE ...]
  EXPLAIN SELECT ...
  BEGIN | COMMIT | ROLLBACK
Meta commands: \tables  \help  \q`)
}
