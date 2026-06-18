# Lab 7 — SQL Query Parsing & Shunting-Yard (C++17)

**Name:** Piyush Pawan Kumar  
**Roll Number:** 24BCS10296  
**Course:** Advanced DBMS — Scaler School of Technology

Two small C++17 programs that build a SQL query parser from first principles:

1. **`query-parsing/`** — a hand-written lexer plus a recursive-descent parser
   that turns `SELECT name FROM employees WHERE id >= 3` into an executable
   query over an in-memory table.
2. **`shunting-yard/`** — Dijkstra's shunting-yard algorithm applied to a
   boolean `WHERE` predicate (`AND`/`OR` with operator precedence), converting
   the infix clause to RPN and evaluating it per row.

---

## Files

```
Lab-7/
├── query-parsing/
│   ├── main.cpp      # lexer + recursive-descent SELECT parser & evaluator
│   └── README.md     # notes on tokenization and parsing
├── shunting-yard/
│   ├── main.cpp      # shunting-yard precedence parser for WHERE predicates
│   └── README.md     # notes on the shunting-yard algorithm
└── README.md         # this file
```

## Build & run

```bash
# query parser
g++ -std=c++17 query-parsing/main.cpp -o query_parser && ./query_parser

# shunting-yard
g++ -std=c++17 shunting-yard/main.cpp -o shunting_yard && ./shunting_yard
```

Tested on Linux x86_64 with g++ 13.
