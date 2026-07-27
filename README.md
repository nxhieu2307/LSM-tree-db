# LSM Tree Storage Engine

This project is an educational implementation of a persistent LSM-tree based key-value storage engine written in C++17.
## Features

Current implementation includes:

- SkipList-based MemTable
- Write-Ahead Log (WAL)
- Crash recovery from WAL
- Tombstone-based deletion
- Ordered iteration
- Thread-safe MemTable
- Unit tests with CTest

## Architecture

```
                Write
                  |
                  v
          +---------------+
          |      WAL      |
          +---------------+
                  |
                  v
          +---------------+
          |   MemTable    |
          +---------------+
                  |
          Flush (future)
                  |
                  v
          +---------------+
          | SSTable (L0)  |
          +---------------+
                  |
             Compaction
                  |
                  v
         Lower SSTable Levels
```

Every write is first appended to the WAL to provide durability, then inserted into the in-memory MemTable. The MemTable uses a SkipList to keep keys sorted, allowing efficient lookups and ordered iteration. Deleted keys are represented using tombstones and will be removed during future compaction.

## Project Structure

```
lsm-tree-db/
├── CMakeLists.txt          # Build configuration
├── README.md               # Project overview
├── PLAN.md                 # Design notes and roadmap
│
├── src/
│   └── core/
│       ├── skiplist.hpp
│       ├── skiplist.cpp
│       ├── wal.hpp
│       ├── wal.cpp
│       ├── memtable.hpp
│       └── memtable.cpp
│
├── tests/
│   ├── skiplist_test.cpp
│   ├── wal_test.cpp
│   └── memtable_test.cpp
│
└── build/                  # Build output (generated)
```

## Components

### SkipList

An ordered in-memory index that stores key/value pairs and supports efficient lookups, inserts, and iteration.

### WAL

An append-only log that records every write before it is applied to the MemTable, enabling crash recovery.

### MemTable

The write buffer of the database. It combines the SkipList and WAL into a single thread-safe interface for reads and writes.

## Build

### Prerequisites
- C++17 compatible compiler (GCC 8+, Clang 7+, or MSVC)
- CMake 3.14+

### Build Instructions
```bash
cmake -B build
cmake --build build
```

## Run Tests

```bash
ctest --test-dir build --output-on-failure
```