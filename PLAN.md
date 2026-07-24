# LSM-Tree Key-Value Store - Status & Architecture

## 1. Overview
This project is a high-performance, persistent Key-Value storage engine based on the **Log-Structured Merge-tree (LSM-Tree)** architecture in C++. The design mirrors core principles from production-grade LSM engines (such as LevelDB and RocksDB), emphasizing fast sequential writes, crash consistency, write-ahead logging, and multi-level SSTable storage.

---

## 2. System Architecture

```
[ Active MemTable (SkipList) ] ---> (Threshold Full) ---> [ Immutable MemTable ]
            |                                                      |
    [ Write-Ahead Log ]                                 (Minor Compaction / Flush)
                                                                   v
                                                        [ SSTable Level 0 ]
                                                                   |
                                                        (Major Compaction / Merge)
                                                                   v
                                                        [ SSTable Level 1..N ]
```

---

## 3. Implemented Components

### 1. SkipList (MemTable Underlying Storage)
- **Files**: [`src/core/skiplist.hpp`](file:///home/hieunx2307/lsm-tree-db/src/core/skiplist.hpp), [`src/core/skiplist.cpp`](file:///home/hieunx2307/lsm-tree-db/src/core/skiplist.cpp), [`tests/skiplist_test.cpp`](file:///home/hieunx2307/lsm-tree-db/tests/skiplist_test.cpp)
- **Features Implemented**:
  - Probabilistic height allocation using dynamic trailing array allocation (`Node* forward[1]`).
  - Concurrent-friendly memory layout using raw `::operator new` and placement `new`.
  - Operations: `Insert` (Put / Delete Tombstones), `Find`, `Clear`, `Empty`.
  - Bidirectional logic via `Iterator` with `SeekToFirst`, `Seek(target)`, `Next`, `Valid`, `key()`, `entry()`.
  - Value types support (`ValueType::kTypeValue` and `ValueType::kTypeDeletion`).
  - Unit tests covering sorted ordering, seeks, updates, and large-scale random operations.

### 2. Write-Ahead Log (WAL)
- **Files**: [`src/core/wal.hpp`](file:///home/hieunx2307/lsm-tree-db/src/core/wal.hpp), [`src/core/wal.cpp`](file:///home/hieunx2307/lsm-tree-db/src/core/wal.cpp), [`tests/wal_test.cpp`](file:///home/hieunx2307/lsm-tree-db/tests/wal_test.cpp)
- **Features Implemented**:
  - Persistent, append-only log engine for crash recovery.
  - JSON-serialized log entries with custom escaping/unescaping logic.
  - Nanosecond timestamp recording for conflict resolution / ordering.
  - Flush-to-disk consistency via `fsync`.
  - Recovery parser capable of scanning and replaying historical logs.
  - Thread safety using `std::mutex` guard.
  - Comprehensive unit test suite.

### 3. Complete MemTable Wrapper Component
- **Files**: [`src/core/memtable.hpp`](file:///home/hieunx2307/lsm-tree-db/src/core/memtable.hpp), [`src/core/memtable.cpp`](file:///home/hieunx2307/lsm-tree-db/src/core/memtable.cpp), [`tests/memtable_test.cpp`](file:///home/hieunx2307/lsm-tree-db/tests/memtable_test.cpp)
- **Features Implemented**:
  - Unified `MemTable` component encapsulating `SkipList` and `WAL`.
  - Atomic write & delete operations (`Put`, `Delete`) with WAL write-before-insert semantics.
  - Automatic WAL Crash Recovery on initialization replaying historical operations into memory.
  - Memory usage tracking (`ApproximateMemoryUsage()`) calculating key, value, and node memory overhead for flush triggering.
  - Key count & emptiness tracking (`Count()`, `Empty()`).
  - Immutable state enforcement (`MarkImmutable()`, `IsImmutable()`) disabling mutations during flushes.
  - `MemTable::Iterator` supporting range scans, lower-bound seeking, and tombstone checks.
  - Thread-safe operations via `std::mutex` RAII locks.

### 4. CMake Build System
- **Files**: [`CMakeLists.txt`](file:///home/hieunx2307/lsm-tree-db/CMakeLists.txt)
- **Features Implemented**:
  - Standard C++17 build setup compiling static library target `lsm_core`.
  - Automatic target definitions and CTest integration for `skiplist_test`, `wal_test`, and `memtable_test`.
