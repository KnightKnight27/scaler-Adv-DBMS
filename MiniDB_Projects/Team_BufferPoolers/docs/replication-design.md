# Distributed Replication Design

## Extension Track D

MiniDB implements a primary-replica architecture.

---

## Components

### PrimaryServer

Responsibilities:

* Accept writes
* Generate WAL records
* Assign LSNs
* Forward replication messages

### ReplicaServer

Responsibilities:

* Receive replication messages
* Apply log records
* Maintain consistent state

### ReplicationMessage

Contains:

* LSN
* Operation type
* Payload

Example:

```
LSN=15
INSERT
5,Bob
```

---

## Replication Workflow

Primary Write

```
INSERT
    ↓
WAL Record
    ↓
LSN Assigned
    ↓
Replication Message
    ↓
Replica Receives
    ↓
Log Replay
    ↓
Consistent State
```

---

## Read Consistency

The replica only applies records with increasing LSNs.

This prevents duplicate execution.

---

## Failure Scenario

1. Primary generates updates.
2. Replica disconnects.
3. Primary stores replication log.
4. Replica reconnects.
5. Catch-up replay occurs.
6. Replica becomes consistent.

---

## Experimental Results

| Operation            | Result         |
| -------------------- | -------------- |
| 1000 Replicated Rows | 48.64 ms       |
| Replica Consistency  | 1000/1000 Rows |

---

## Limitations

* Asynchronous replication
* Single replica
* No leader election
* No consensus protocol
