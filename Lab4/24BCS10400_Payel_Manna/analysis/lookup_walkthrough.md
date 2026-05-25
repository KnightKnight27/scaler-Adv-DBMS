# SQLite Lookup Walkthrough

## Root Pages Identified

The root pages of the tables were identified using:

```sql
SELECT name, rootpage FROM sqlite_master;
```

Output:

| Table | Root Page |
|---|---|
| users | 2 |
| products | 3 |

Using page size 4096 bytes:

| Page | File Offset |
|---|---|
| Page 1 | 0 |
| Page 2 | 4096 |
| Page 3 | 8192 |

Offset formula:

```text
offset = (page_number - 1) * page_size
```

Therefore:

```text
users table root offset
= (2 - 1) * 4096
= 4096 bytes
```

```text
products table root offset
= (3 - 1) * 4096
= 8192 bytes
```

---

## Query Analyzed

```sql
SELECT * FROM users WHERE id = 250;
```

SQLite begins traversal from the root page of the users table.

Root page:
Page 2

Root page file offset:
4096 bytes

---

## B-Tree Lookup Traversal

### Step 1 — Start at Root Page

SQLite loads the B-Tree root page for the users table.

The root page contains:
- child page pointers
- separator keys
- B-Tree metadata

---

### Step 2 — Compare Search Key

SQLite compares the search key:

```text
id = 250
```

against keys stored in the B-Tree node.

---

### Step 3 — Follow Child Pointer

Based on key comparison,
SQLite follows the appropriate child page pointer.

Interior pages do not store actual row data.

They only store:
- keys
- child page references

---

### Step 4 — Reach Leaf Page

Traversal continues until SQLite reaches a leaf page.

Leaf pages contain:
- actual table records
- cell pointer arrays
- record payloads

Leaf pages are identified using page type:

```text
0x0D
```

---

### Step 5 — Scan Cell Pointer Array

SQLite scans the cell pointer array inside the leaf page.

Example structure:

```text
0fa0
0f80
0f60
```

Each pointer references a record offset inside the page.

---

### Step 6 — Locate Matching Row

SQLite locates the row where:

```text
id = 250
```

The matching record is decoded from the page.

---

### Step 7 — Decode Record

SQLite decodes:
- payload size
- rowid
- serial type codes
- column values

The final row is returned to the query engine.

---

## Time Complexity

B-Tree traversal reduces page reads significantly.

Search complexity:

```text
O(log n)
```

Advantages:
- efficient disk access
- balanced tree structure
- reduced lookup cost
- scalable storage mechanism

---

## Summary

SQLite uses B-Tree traversal for efficient row lookup.

The lookup process involves:
1. locating the root page
2. traversing interior nodes
3. following child pointers
4. reaching leaf pages
5. decoding matching records

This demonstrates how SQL queries are resolved internally
through low-level page navigation mechanisms.
```