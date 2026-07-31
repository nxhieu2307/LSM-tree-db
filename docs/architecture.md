# Architecture

## Overview

**`lsm-tree-db`** is a persistent key-value storage engine implemented in C++17 based on the **Log-Structured Merge-tree (LSM-tree)** architecture.

The current implementation provides the core storage engine components: an ordered in-memory index (`SkipList`), persistent durability logging (`WAL`), a thread-safe write buffer (`MemTable`), and an initial `SSTable Builder` component.

---

## High-Level Architecture

```
           User Application
                  |
          Put / Get / Delete
                  |
          +---------------+
          |   MemTable    |
          +---------------+
           |             |
      Append WAL    Query/Update
           |             |
           v             v
      +----------+  +----------+
      |   WAL    |  | SkipList |
      +----------+  +----------+
                         |
                Freeze (MarkImmutable)
                         v
                [ SSTable Builder ]
```

The `MemTable` serves as the entry point, coordinating logging with the `WAL` and in-memory index operations with the `SkipList`.

---

## Core Components

### Active MemTable

The Active MemTable is the mutable in-memory write buffer. It stores recent updates in sorted order using a SkipList and serves as the primary lookup location for read operations. Once its memory threshold is reached, it becomes immutable and is prepared for serialization.

### Write-Ahead Log (WAL)

The Write-Ahead Log is an append-only disk log providing durability and crash recovery. Every mutation is written to the log before modifying in-memory structures, ensuring records are durably persisted before acknowledging successful writes. Upon startup, the WAL is replayed sequentially to recover state.

### Immutable MemTable

The Immutable MemTable is a frozen, read-only snapshot of a full MemTable. It continues serving read queries while preventing further mutations, ensuring point-in-time state isolation.

### SSTable Builder & Format

The SSTable component defines the on-disk representation of immutable sorted data. It specifies the file format and provides the builder interface responsible for serializing MemTables into SSTables. The current implementation establishes the format definition and builder interface; full serialization is still in progress.

---

## Write Flow

```
User Write Request
       |
       v
[ 1. Append to WAL ]
       |
       v
[ 2. Insert to SkipList ]
       |
       v
[ 3. Check Memory Threshold ] ---> (If Exceeded) ---> [ Mark MemTable Immutable ]
       |
       v
 Return Success
```

1. Append the operation to the WAL.
2. Update the SkipList index.
3. Update dynamic memory usage tracking.
4. Freeze the MemTable (`MarkImmutable`) if the memory threshold is exceeded.

---

## Read Flow

```
Search SkipList
      |
    Found?
      |
Yes ----> Return value or deletion marker
No -----> Key not found
```

1. Search the in-memory SkipList index for the requested key.
2. If found, return the associated value or deletion marker.
3. If absent, return key not found.

---

## Data Lifecycle

Data progresses through the implemented components in the following stages:

```
Incoming Write
      │
      ▼
Append to WAL
      │
      ▼
Update MemTable
      │
      ▼
Memory Threshold Reached
      │
      ▼
Mark MemTable Immutable
      │
      ▼
Ready for SSTable Serialization
```
