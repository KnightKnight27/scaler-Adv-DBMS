// Command minidb is the CLI/REPL entry point for the MiniDB engine.
//
// It supports an interactive prompt and running .sql scripts piped on stdin:
//
//	go run ./cmd/minidb --data ./data --engine heap
//	go run ./cmd/minidb --engine lsm < demo/demo.sql
//
// This is currently a scaffold: it parses flags and runs a read-eval loop that
// echoes statements. The lexer/parser/planner/executor are wired in as those
// packages land (see CLAUDE.md for the build order).
package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"strings"
)

func main() {
	engine := flag.String("engine", "heap", "storage engine: heap | lsm")
	dataDir := flag.String("data", "./data", "directory for database files")
	flag.Parse()

	if *engine != "heap" && *engine != "lsm" {
		fmt.Fprintf(os.Stderr, "minidb: unknown engine %q (want heap or lsm)\n", *engine)
		os.Exit(2)
	}

	interactive := isTerminal(os.Stdin)
	if interactive {
		fmt.Printf("MiniDB (engine=%s, data=%s). Type \\q to quit; end statements with ';'.\n", *engine, *dataDir)
	}

	if err := repl(os.Stdin, os.Stdout, interactive); err != nil {
		fmt.Fprintln(os.Stderr, "minidb:", err)
		os.Exit(1)
	}
}

// repl reads SQL statements (terminated by ';') and dispatches them. For now it
// echoes; execution is added as the engine packages come online.
func repl(in *os.File, out *os.File, interactive bool) error {
	sc := bufio.NewScanner(in)
	sc.Buffer(make([]byte, 0, 64*1024), 1<<20)

	var buf strings.Builder
	if interactive {
		fmt.Fprint(out, "minidb> ")
	}
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		switch {
		case line == "\\q" || line == "exit" || line == "quit":
			return nil
		case line == "":
			// ignore blank lines
		default:
			buf.WriteString(line)
			buf.WriteByte(' ')
			if strings.HasSuffix(line, ";") {
				stmt := strings.TrimSpace(buf.String())
				buf.Reset()
				execute(out, strings.TrimSuffix(stmt, ";"))
			}
		}
		if interactive {
			fmt.Fprint(out, "minidb> ")
		}
	}
	if interactive {
		fmt.Fprintln(out)
	}
	return sc.Err()
}

// execute will hand the statement to the lexer/parser/planner/executor pipeline.
// Placeholder until those packages exist.
func execute(out *os.File, stmt string) {
	fmt.Fprintf(out, "[not yet implemented] %s\n", stmt)
}

func isTerminal(f *os.File) bool {
	info, err := f.Stat()
	if err != nil {
		return false
	}
	return info.Mode()&os.ModeCharDevice != 0
}
