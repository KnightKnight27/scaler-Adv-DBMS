# Concurrency Control: 2-Phase Locking & Deadlock Detection

This document provides a concise and detailed overview of two critical mechanisms used in Database Management Systems (DBMS) to manage concurrent transactions: the **Two-Phase Locking (2PL) Protocol** and **Deadlock Detection**.

---

## 📖 Table of Contents
1. [Introduction](#introduction)
2. [Two-Phase Locking (2PL)](#two-phase-locking-2pl)
    - [The Two Phases](#the-two-phases)
    - [Variations of 2PL](#variations-of-2pl)
3. [Deadlock Detection Mechanism](#deadlock-detection-mechanism)
    - [Wait-For Graph (WFG)](#wait-for-graph-wfg)
    - [Deadlock Recovery](#deadlock-recovery)
4. [Summary](#summary)

---

## 1. Introduction
When multiple transactions execute concurrently in a database, it is essential to ensure data consistency and isolation. **Concurrency control** protocols prevent conflicts (like dirty reads or lost updates) by managing how transactions access shared data. The most common protocol used to guarantee *serializability* (ensuring concurrent transactions yield the same result as if executed sequentially) is 2-Phase Locking. 

---

## 2. Two-Phase Locking (2PL)

**Two-Phase Locking (2PL)** is a concurrency control method that requires every transaction to issue lock and unlock requests in two distinct, non-overlapping phases. 

### The Two Phases

1. **Growing Phase (Phase 1):**
   - The transaction can **acquire** new locks on data items (Shared/Read locks or Exclusive/Write locks).
   - The transaction **cannot release** any locks.
   - Once a transaction releases its first lock, the growing phase ends.

2. **Shrinking Phase (Phase 2):**
   - The transaction can **release** existing locks.
   - The transaction **cannot acquire** any new locks.

> **Key Takeaway:** 2PL guarantees conflict serializability. If all transactions follow 2PL, the database will remain in a consistent state. However, basic 2PL does *not* prevent deadlocks or cascading rollbacks.

### Variations of 2PL
To solve issues like cascading rollbacks, strict variations of 2PL are often implemented:
* **Strict 2PL:** A transaction holds all its *Exclusive (Write) locks* until it commits or aborts. (Prevents cascading rollbacks).
* **Rigorous 2PL:** A transaction holds *all locks* (both Read and Write) until it commits or aborts. (Easier to implement, but reduces concurrency).
* **Conservative 2PL:** A transaction must acquire all the locks it will ever need before it begins execution. (Prevents deadlocks, but highly impractical).

---

## 3. Deadlock Detection Mechanism

A **Deadlock** occurs when two or more transactions are waiting indefinitely for one another to release locks. Because 2PL (except Conservative 2PL) allows transactions to hold locks while requesting new ones, deadlocks are a natural side-effect.

Instead of preventing deadlocks entirely (which limits performance), many database systems allow them to happen and use a **Deadlock Detection and Recovery** mechanism.

### Wait-For Graph (WFG)
The DBMS maintains a directed graph called a **Wait-For Graph** to detect deadlocks in the background.

* **Nodes:** Represent active transactions ($T_1, T_2, \dots, T_n$).
* **Directed Edges ($T_i \rightarrow T_j$):** Represent that transaction $T_i$ is waiting for a lock currently held by transaction $T_j$.

**Detection Logic:**
The system periodically checks the Wait-For Graph. If the graph contains a **cycle** (e.g., $T_1 \rightarrow T_2 \rightarrow T_3 \rightarrow T_1$), a deadlock exists. 

### Deadlock Recovery
Once a deadlock is detected, the DBMS must break the cycle by aborting one or more transactions. 

1. **Victim Selection:** The system chooses a transaction to abort (the "victim") based on minimizing cost. Criteria for selecting a victim include:
   - *Age:* Abort the newest transaction (it has done the least work).
   - *Progress:* Abort the transaction with the fewest data updates.
   - *Locks held:* Abort the transaction holding the fewest locks.
   
2. **Rollback:**
   - *Total Rollback:* Abort the victim completely and restart it from the beginning.
   - *Partial Rollback:* Roll back the victim only as far as necessary to break the deadlock cycle (more complex to implement).

3. **Starvation Prevention:**
   The system must ensure the same transaction isn't repeatedly chosen as the victim, which would cause *starvation*. This is usually handled by increasing a transaction's priority the longer it stays in the system.

---

## 4. Summary
* **2-Phase Locking (2PL)** ensures data consistency by dividing lock acquisition and release into two strict phases, guaranteeing serializability.
* **Deadlocks** are a known risk of 2PL, occurring when transactions get stuck waiting for each other's locks.
* **Deadlock Detection** uses a **Wait-For Graph** to identify cycles of waiting transactions.
* **Deadlock Recovery** resolves the issue by intelligently selecting a victim transaction to roll back, freeing up locks for the remaining transactions to complete. 