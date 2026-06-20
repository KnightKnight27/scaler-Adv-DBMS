// Command minidb is the CLI/REPL entry point for the MiniDB engine.
//
// It supports an interactive prompt and running .sql scripts piped on stdin:
//
//	go run ./cmd/minidb --data ./data --engine heap
//	go run ./cmd/minidb --engine lsm < demo/demo.sql
//
// Statements are terminated by ';'. Meta-commands: \q quit, \dt list tables.
package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"strings"

	"minidb/internal/db"
)

func main() {
	engine := flag.String("engine", "heap", "storage engine: heap | lsm")
	dataDir := flag.String("data", "./data", "directory for database files")
	flag.Parse()

	if *engine != "heap" && *engine != "lsm" {
		fmt.Fprintf(os.Stderr, "minidb: unknown engine %q (want heap or lsm)\n", *engine)
		os.Exit(2)
	}
	if err := os.MkdirAll(*dataDir, 0o755); err != nil {
		fmt.Fprintln(os.Stderr, "minidb:", err)
		os.Exit(1)
	}

	database, err := db.Open(*dataDir, *engine)
	if err != nil {
		fmt.Fprintln(os.Stderr, "minidb:", err)
		os.Exit(1)
	}
	defer database.Close()

	interactive := isTerminal(os.Stdin)
	if interactive {
		fmt.Printf("MiniDB (engine=%s, data=%s). \\q to quit; end statements with ';'.\n", *engine, *dataDir)
	}
	if err := repl(database, os.Stdin, os.Stdout, interactive); err != nil {
		fmt.Fprintln(os.Stderr, "minidb:", err)
		os.Exit(1)
	}
}

// repl reads ';'-terminated statements and executes them.
func repl(database *db.Database, in *os.File, out *os.File, interactive bool) error {
	sc := bufio.NewScanner(in)
	sc.Buffer(make([]byte, 0, 64*1024), 1<<20)

	var buf strings.Builder
	prompt := func() {
		if interactive {
			fmt.Fprint(out, "minidb> ")
		}
	}
	prompt()
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		switch {
		case line == "\\q" || line == "exit" || line == "quit":
			return nil
		case line == "":
			// skip
		case strings.HasPrefix(line, "\\"):
			runMeta(database, out, line)
		default:
			buf.WriteString(line)
			buf.WriteByte(' ')
			if strings.HasSuffix(line, ";") {
				stmt := strings.TrimSuffix(strings.TrimSpace(buf.String()), ";")
				buf.Reset()
				runStatement(database, out, stmt)
			}
		}
		prompt()
	}
	if interactive {
		fmt.Fprintln(out)
	}
	return sc.Err()
}

func runStatement(database *db.Database, out *os.File, stmt string) {
	if strings.TrimSpace(stmt) == "" {
		return
	}
	res, err := database.Execute(stmt)
	if err != nil {
		fmt.Fprintln(out, "ERROR:", err)
		return
	}
	printResult(out, res)
}

func runMeta(database *db.Database, out *os.File, line string) {
	switch strings.TrimSpace(line) {
	case "\\dt":
		for _, name := range database.Tables() {
			fmt.Fprintln(out, name)
		}
	default:
		fmt.Fprintln(out, "unknown meta-command:", line)
	}
}

func printResult(out *os.File, res db.Result) {
	if res.Columns == nil {
		fmt.Fprintln(out, res.Message)
		return
	}
	fmt.Fprintln(out, strings.Join(res.Columns, " | "))
	for _, row := range res.Rows {
		fmt.Fprintln(out, strings.Join(row, " | "))
	}
	fmt.Fprintf(out, "(%d row(s))\n", len(res.Rows))
}

func isTerminal(f *os.File) bool {
	info, err := f.Stat()
	if err != nil {
		return false
	}
	return info.Mode()&os.ModeCharDevice != 0
}
