# LSM-Tree Key-Value Store - Implementation Plan & Roadmap

## 1. Overview
This project is a high-performance, persistent Key-Value storage engine based on the **Log-Structured Merge-tree (LSM-Tree)** architecture in C++. The design mirrors core principles from production-grade LSM engines (such as LevelDB and RocksDB), emphasizing fast sequential writes, crash consistency, write-ahead logging, and multi-level SSTable storage.

---

## 2. Current Status (Implemented Components)

### ✅ Phase 1: Core In-Memory Data Structures & Durability Layer

#### 1. SkipList (MemTable Underlying Storage)
- **Files**: `src/core/skiplist.hpp`, `src/core/skiplist.cpp`, `tests/skiplist_test.cpp`
- **Features Implemented**:
  - Probabilistic height allocation using dynamic trailing array allocation (`Node* forward[1]`).
  - Concurrent-friendly memory layout using raw `::operator new` and placement `new`.
  - Operations: `Insert` (Put / Delete Tombstones), `Find`, `Clear`, `Empty`.
  - Bidirectional logic via `Iterator` with `SeekToFirst`, `Seek(target)`, `Next`, `Valid`, `key()`, `entry()`.
  - Value types support (`ValueType::kTypeValue` and `ValueType::kTypeDeletion`).
  - Unit tests covering sorted ordering, seeks, updates, and large-scale random operations.

#### 2. Write-Ahead Log (WAL)
- **Files**: `src/core/wal.hpp`, `src/core/wal.cpp`, `tests/wal_test.cpp`
- **Features Implemented**:
  - Persistent, append-only log engine for crash recovery.
  - JSON-serialized log entries with custom escaping/unescaping logic.
  - Nanosecond timestamp recording for conflict resolution / ordering.
  - Flush-to-disk consistency via `fsync`.
  - Recovery parser capable of scanning and replaying historical logs.
  - Thread safety using `std::mutex` guard.
  - Comprehensive unit test suite.

---

## 3. Roadmap & Next Implementation Steps

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

### 🚧 Phase 2: MemTable Abstraction & Engine Recovery
- [ ] **MemTable Class (`src/core/memtable.hpp`, `src/core/memtable.cpp`)**:
  - Wrap `SkipList` and `WAL` into a unified `MemTable` component.
  - Enforce atomic mutations (writing to WAL first, then inserting into SkipList).
  - Track byte size of in-memory entries to trigger flushing when exceeding capacity limit (e.g., 4MB).
- [ ] **Immutable MemTable Manager**:
  - Maintain active and immutable MemTables.
  - Freeze active MemTable into read-only state during flush operations while opening a new active MemTable & WAL.
- [ ] **Recovery Flow**:
  - On DB startup, detect non-empty WAL files and replay operations into MemTable.

---

### 🚧 Phase 3: SSTable (Sorted String Table) Format & I/O
- [ ] **SSTable File Format Design**:
  ```
  +------------------+------------------+-------------------+-----------------+----------------+
  | Data Block 0     | Data Block 1     | ... Data Block N  | Index Block     | Footer         |
  +------------------+------------------+-------------------+-----------------+----------------+
  ```
- [ ] **SSTable Builder (`src/sstable/builder.hpp`, `src/sstable/builder.cpp`)**:
  - Sort and freeze key-value entries from Immutable MemTable into sequential SSTable files.
  - Block creation (e.g., 4KB blocks) with prefix compression or restart points.
  - Index block construction mapping file offsets and key ranges for binary search.
- [ ] **SSTable Reader (`src/sstable/reader.hpp`, `src/sstable/reader.cpp`)**:
  - Binary search using index block for quick key lookup.
  - Block decoding and point-query execution.
- [ ] **SSTable Iterator**:
  - Two-level iterator (Index block -> Data block) supporting Range Scans.

---

### 🚧 Phase 4: Integrated Storage Engine (`DB` Interface & Multi-Level Read Path)
- [ ] **DB Engine Public API (`include/lsm/db.hpp`, `src/db/db_impl.cpp`)**:
  - `Put(key, value)`
  - `Get(key, value*)`
  - `Delete(key)`
  - `NewIterator(ReadOptions)`
- [ ] **Multi-Level Query Order**:
  1. Check **Active MemTable**
  2. Check **Immutable MemTable(s)**
  3. Check **Level 0 SSTables** (Newest to Oldest)
  4. Check **Level 1..N SSTables** (Binary search within non-overlapping level files)
- [ ] **Manifest & Version Management**:
  - Track active SSTable files per level across database restarts via a `MANIFEST` file.

---

### 🚧 Phase 5: Compaction Engine
- [ ] **Minor Compaction**:
  - Flush Immutable MemTable to disk as Level 0 SSTable file.
  - Delete old WAL file upon successful flush.
- [ ] **Major Compaction**:
  - Level-based background merge-sorting to eliminate duplicate key versions and deleted tombstones.
  - Promote merged SSTables to Level $L+1$.
- [ ] **Thread Pool / Background Worker**:
  - Asynchronous background tasks for minor/major compaction without blocking client writes.

---

### 🚧 Phase 6: Performance Optimizations
- [ ] **Bloom Filters**:
  - Generate per-SSTable Bloom Filter to avoid reading disk blocks for non-existent keys.
- [ ] **Block Cache**:
  - LRU Cache for uncompressed Data Blocks in memory to lower disk read latency.
- [ ] **Binary Format WAL**:
  - Transition from JSON logging to compact binary encoding (length-prefixed records) for lower disk footprint and higher throughput.

---

### 🚧 Phase 7: Build System, Benchmarks & CLI
- [ ] **CMake Integration (`CMakeLists.txt`)**:
  - Modular build configuration for library, unit tests, and benchmarks.
- [ ] **Benchmarking Suite (`benchmarks/db_bench.cpp`)**:
  - Throughput (ops/sec) and latency measuring for random/sequential operations.
- [ ] **CLI Utility**:
  - Interactive REPL for database interaction.

---

## 4. Suggested Immediate Next Steps
1. **Create `CMakeLists.txt`** to unify compilation of existing code and tests.
2. **Implement `MemTable` wrapper** uniting `SkipList` and `WAL`.
3. **Design SSTable file format & builder**.
