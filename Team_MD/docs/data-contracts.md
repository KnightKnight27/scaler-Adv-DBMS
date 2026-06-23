# MiniDB Data Contracts

This document formalizes the core architectural decisions for MiniDB storage and indexing.

## 1. Page Format
- **Page Size**: Exactly 4096 bytes.
- **Layout**: Slotted-page layout.
- **Headers**: Each page will contain a header (metadata) followed by a slot directory that grows downwards, while data (rows) grows upwards.

## 2. Row Definition
- **Data Types**: Initially supporting basic fixed-width types (Integer, Long, etc.).
- **Serialization**: Fixed-width binary serialization (Phase 1).
- **Variable Length**: Not supported in early phases. String/VARCHAR support will be implemented as fixed-length buffers for simplicity initially.
- **Transaction Metadata**: Rows **do not** currently contain `xmin` or `xmax` fields. These are reserved for Phase 4 (MVCC/Transaction management).

## 3. Identification (RowId)
- **Structure**: `RowId = (pageId, slotNumber)`.
- **Immutability**: Once a row is assigned a `RowId`, it does not change even if its content is updated.
- **Index Pointers**: The Indexing module will use this `RowId` as the leaf pointer in the B+ Tree.

## 4. Stability
These contracts are considered stable for Phases 1-3. Any breaking changes to the binary format must be coordinated between the Storage and Indexing teams.
