# Query Optimizer Design

## Objective

The optimizer selects an efficient execution strategy for SQL queries.

---

## Supported Decisions

### Table Scan vs Index Scan

The optimizer evaluates:

* Predicate existence
* Selectivity estimate
* Index availability

---

## Selectivity Estimation

Formula:

```
Selectivity =
MatchingRows / TotalRows
```

Example:

```
100 matching rows
1000 total rows

Selectivity = 0.1
```

Low selectivity favors index scans.

---

## Cost Model

### Table Scan Cost

```
Cost = NumberOfPages
```

### Index Scan Cost

```
Cost = TreeHeight
```

---

## Join Order Selection

MiniDB chooses the smallest relation first.

Example:

```
Table A = 100 rows
Table B = 500 rows

Join Order:

A JOIN B
```

This reduces intermediate result size.

---

## Query Plan Types

### TABLE_SCAN

Used when:

* No predicate exists
* Selectivity is high

### INDEX_SCAN

Used when:

* Predicate exists
* Matching rows are small
* Index is available

---

## Complexity

| Operation    | Complexity |
| ------------ | ---------- |
| Table Scan   | O(N)       |
| Index Lookup | O(log N)   |
