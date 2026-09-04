# LSM-Tree Key-Value Storage Engine

A high-performance, embedded persistent key-value storage engine implemented in modern C++17 based on the **Log-Structured Merge-tree (LSM-Tree)** architecture.

---

## Features

- **In-Memory Write Buffer (`MemTable`)**: Thread-safe sorted index backed by a concurrent-ready `SkipList`.
- **Write-Ahead Log (`WAL`)**: Append-only log with CRC32 checksums for immediate durability and crash replay.
- **Immutable On-Disk SSTables**: Binary format featuring:
  - Sequential Data Blocks
  - In-Memory Sparse Index (sampled every 16 keys) with binary search lookup
  - Double-Hashing Bloom Filter ($< 1\%$ false positive rate) to eliminate redundant disk reads
  - Fixed 40-byte Metadata Footer (`0x4C534D5452454531`)
- **Streaming Iterator (`SSTableIterator`)**: Bounded memory, sequential forward-only record scanning.
- **$k$-Way Merge Compactor (`Compactor`)**: Min-heap streaming merge across multiple SSTable streams with duplicate key resolution and tombstone purging.
- **Transactional Manifest (`Manifest`)**: Atomic SSTable additions and replacements via temporary file swap and atomic rename.
- **Unified Engine (`StorageEngine`)**: Multi-tier read hierarchy, threshold-based flushes, and interactive CLI REPL (`lsm_db`).
- **Comprehensive Test Suite**: 12 automated unit and integration test suites orchestrated via CTest.

---

## Architecture Overview

```
                                +--------------------------------------------+
                                |             Client Application             |
                                |     Put(k, v)   |   Get(k)   |  Delete(k)  |
                                +----------------------+---------------------+
                                                       |
                             +-------------------------+-------------------------+
                             | (Write Path)                                      | (Read Path)
                             v                                                   v
                    +-----------------+                                 +-----------------+
                    | 1. Append WAL   | (CRC32 Durability)              | 1. Active RAM   | -> Found? Return
                    +--------+--------+                                 +--------+--------+
                             |                                                   | Absent
                             v                                                   v
                    +-----------------+                                 +-----------------+
                    | 2. SkipList RAM | (MemTable Buffer)               | 2. Immutable RAM| -> Found? Return
                    +--------+--------+                                 +--------+--------+
                             | (Size >= WriteBuffer Limit)                       | Absent
                             v                                                   v
                    +-----------------+                                 +-----------------+
                    | 3. SSTable L0   | (Disk Serialization)            | 3. Bloom Filter | -> Negative? Short-Circuit
                    +--------+--------+                                 +--------+--------+
                             |                                                   | Maybe Present
                             v                                                   v
                    +-----------------+                                 +-----------------+
                    | 4. MANIFEST Log | (Atomic Metadata Swap)          | 4. Sparse Index | -> Binary Search Offset
                    +--------+--------+                                 +--------+--------+
                             | (Count >= Threshold)                              |
                             v                                                   v
                    +-----------------+                                 +-----------------+
                    | 5. Compactor    | (k-Way Min-Heap Merge)          | 5. Sequential   | -> Scan Data Block
                    +-----------------+                                 |    Data Scan    |
                                                                        +-----------------+
```

---

## Project Structure

```text
lsm-tree-db/
├── CMakeLists.txt              # CMake build configuration
├── README.md                   # Project overview & quickstart
├── docs/
│   └── architecture.md         # Detailed architectural & implementation specification
├── notes/                      # In-depth component design notes
│   ├── skiplist.md
│   ├── wal.md
│   ├── memtable.md
│   ├── sstable.md
│   ├── bloom_filter.md
│   ├── sstable_iterator.md
│   ├── compactor.md
│   ├── manifest.md
│   ├── engine.md
│   └── testing.md
├── src/
│   ├── main.cpp                # CLI REPL shell entry point (lsm_db)
│   └── core/                   # Core storage engine implementation
│       ├── skiplist.hpp / .cpp
│       ├── wal.hpp / .cpp
│       ├── memtable.hpp / .cpp
│       ├── sstable_format.hpp
│       ├── sstable_builder.hpp / .cpp
│       ├── sstable_reader.hpp / .cpp
│       ├── sstable_iterator.hpp / .cpp
│       ├── bloom_filter.hpp / .cpp
│       ├── compactor.hpp / .cpp
│       ├── manifest.hpp / .cpp
│       └── engine.hpp / .cpp
└── tests/
    ├── unit/                   # Unit test executables
    └── integration/            # Multi-component integration test executables
```

---

## Build & Usage

### Prerequisites
- C++17 compatible compiler (GCC 8+, Clang 7+, or MSVC)
- CMake 3.14+

### Build
```bash
cmake -B build -S .
cmake --build build
```

### Run CLI REPL Shell
```bash
./build/lsm_db
```

Example session:
```text
lsm-db> PUT user:1001 "Alice"
OK
lsm-db> GET user:1001
Alice
lsm-db> DEL user:1001
OK
lsm-db> GET user:1001
Key not found: user:1001
lsm-db> EXIT
Bye!
```

---

## Running Tests

Execute all 12 unit and integration test suites:
```bash
ctest --test-dir build --output-on-failure
```