# SQL Pipeline

## Stages

```mermaid
graph LR
    SQL["SQL String"] --> Tokenizer --> Tokens["Token[]"] --> Parser --> AST --> Planner --> Plan["Executor Plan"] --> Result
```

## Tokenizer

- Lexer over input string
- Recognizes keywords (SELECT, FROM, WHERE, INSERT, etc.)
- Identifiers, integer literals, string literals, operators (=, <, >, +, -, etc.)
- Punctuation: `, ( ) ; *`

## Parser

- Recursive descent
- Produces polymorphic `Stmt` and `Expr` (Clone() for safe transforms)
- Stmt types: SELECT, INSERT, CREATE, DROP, UPDATE, DELETE
- Expr types: ColumnRef, Literal, BinaryOp

## Planner

- Maps Stmt → Executor tree
- SELECT → SeqScan → Filter → Project
- INSERT → InsertExecutor
- DELETE → DeleteExecutor
- UPDATE → UpdateExecutor

## Executor Tree

```mermaid
graph TD
    Exec["AbstractExecutor <br> (Init/Next/GetOutputSchema)"]
    Exec --> SeqScan["SeqScanExecutor <br> (Iterate heap file)"]
    Exec --> Filter["FilterExecutor <br> (Predicate eval)"]
    Exec --> Project["ProjectExecutor <br> (Column subset)"]
    Exec --> Insert["InsertExecutor <br> (Insert row, sync index)"]
    Exec --> Delete["DeleteExecutor <br> (Tombstone row, sync index)"]
    Exec --> Update["UpdateExecutor <br> (Rewrite tuple, sync index)"]
```

## Evaluator

- Walks Expr tree against row + Schema context
- ColumnRef resolves via `Schema::GetColumnIndex(name)`
- BinaryOp: arithmetic (+,-,*,/), comparison (=,<,>), logical (AND,OR)
- Type-safe: switch on TypeId for proper accessor

## Supported SQL Subset

- CREATE TABLE / DROP TABLE
- INSERT INTO ... VALUES
- SELECT [cols] FROM table [WHERE expr]
- DELETE FROM table [WHERE expr]
- UPDATE table SET col=expr [WHERE expr]
- Expressions: literals, column refs, binary ops (no subqueries, no joins)