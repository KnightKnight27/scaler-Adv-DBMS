# Minimal SQL SELECT Parser using vector<Row>

## Objective

Implement a lightweight SQL SELECT parser in C++ that operates on an in-memory table stored as a vector<Row>.

The parser demonstrates the basic concepts behind query execution without using a database engine.

---

## Concepts Covered

- Query Parsing
- Row Storage
- In-Memory Database Concepts
- Table Scan
- Column Projection
- WHERE Clause Filtering

---

## Data Model

The table is represented using:

```cpp
struct Row {
    int id;
    string name;
    int salary;
};
```