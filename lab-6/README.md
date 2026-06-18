# Multi-Way Balanced B-Tree Subsystem in C++

**Course Module:** Advanced Database Management Systems  
**Academic Registration Details:** Roll No: 10287  
**Language Environment:** C++17

---

## General Overview

This project presents a native, independent implementation of a self-balancing B-Tree data model written entirely from scratch. B-Trees serve as the core transactional framework powering physical secondary storage management systems and enterprise relational databases (such as Oracle, PostgreSQL, and MySQL). 

By structuring block components to support up to $2t$ child nodes and keeping multiple data elements per internal node block, B-trees substantially mitigate costly hardware disk storage Read/Write head movements while guaranteeing a uniform, short leaf height profile.

The system features a clean terminal user console enabling real-time experimentation across all major storage actions: addition, searching, removal, and visual layout rendering.

---

## Technical Index
1. Architecture Mapping
2. Automated Compilation Framework
3. Interactive Console Simulation
4. Structural Integrity Rules
5. Upstream Insertion Guidelines
6. Structural Deletion Routines
7. Tree Navigation Routines
8. Algorithmic Bounds

---

## Architecture Mapping

| File Signature | Operational Target |
| :--- | :--- |
| `main.cpp` | Encapsulates structural element nodes (`CustomBTreeNode`), tree management controls (`CustomBTree`), and interactive command prompt switches. |
| `Makefile` | Script wrapper handling target builds (`make`), executions (`make run`), and cache cleanups (`make clean`). |
| `README.md` | General conceptual references and usage technical briefs. |

---

## Automated Compilation Framework

```bash
make        # Assembles source pathways into standard binary target ./btree
make run    # Automates build sequences and instantly launches interface sessions
make clean  # Purges all local compiled execution assets