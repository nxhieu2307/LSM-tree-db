# LSM-Tree Key-Value Storage Engine: Design & Implementation Specification

## 1. Executive Summary & System Overview

`lsm-tree-db` is a high-performance, embedded persistent key-value storage engine implemented in modern C++17 based on the **Log-Structured Merge-tree (LSM-Tree)** architecture.

The engine is engineered for write-heavy workloads, fast point lookups, ordered range scans, and crash-consistent durability. It serves as an embedded storage foundation with predictable latency, sub-millisecond lookups, and crash resilience.

---

## 2. Global Architecture Diagram

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

## 3. Core Engine Components

### 3.1. In-Memory Index (`SkipList`)
- **File**: `src/core/skiplist.hpp`, `src/core/skiplist.cpp`
- **Role**: Probabilistic sorted index with $O(\log N)$ average insertion, deletion, and lookup time.
- **Key Characteristics**:
  - Maximum height `kMaxHeight = 16`, branch probability $p = 0.5$.
  - Stores `MemTableEntry` containing value string and `ValueType` (`kTypeValue` or `kTypeDeletion`).
  - Provides bi-directional ordered iteration via internal cursor nodes.

### 3.2. Write-Ahead Log (`WAL`)
- **File**: `src/core/wal.hpp`, `src/core/wal.cpp`
- **Role**: Sequential append-only write log providing immediate durability before modifying in-memory buffers.
- **Binary Framing**:
  ```
  +--------------------+--------------------+--------------------+--------------------+--------------------+
  | Key Len (4B uint32)| Val Len (4B uint32)| Type (1B uint8)    | Key + Value Data   | CRC32 (4B uint32)  |
  +--------------------+--------------------+--------------------+--------------------+--------------------+
  ```
- **Crash Recovery**: Reads and validates CRC32 checksums sequentially to replay unflushed mutations into RAM upon startup.

### 3.3. Write Buffer (`MemTable`)
- **File**: `src/core/memtable.hpp`, `src/core/memtable.cpp`
- **Role**: Thread-safe write coordinator uniting `SkipList` and `WAL`.
- **Dynamic Accounting**: Tracks byte usage ($K + V + \text{overhead}$). When usage exceeds `write_buffer_size_`, triggers immutability transition (`MarkImmutable()`).

### 3.4. On-Disk SSTable (`SSTableBuilder` & `SSTableReader`)
- **File**: `src/core/sstable_builder.hpp`, `src/core/sstable_reader.hpp`, `src/core/sstable_format.hpp`
- **Role**: Immutable on-disk sorted tables divided into 4 sequential blocks:
  1. **Data Block**: Sequential records `[key_len][val_len][key][val][is_deleted]`.
  2. **Sparse Index Block**: Sampled key offsets every 16 entries (`kSparseIndexInterval = 16`).
  3. **Bloom Filter Block**: Double-hashing bitset for fast negative lookup short-circuiting.
  4. **Footer (40 Bytes)**: Fixed-size trailer with block offsets, entry counts, and magic constant `0x4C534D5452454531` (`LSMTREE1`).

### 3.5. Sequential Streaming Iterator (`SSTableIterator`)
- **File**: `src/core/sstable_iterator.hpp`, `src/core/sstable_iterator.cpp`
- **Role**: Forward streaming cursor over SSTable data blocks. Reads records lazily without loading the whole file into RAM.

### 3.6. Double-Hashing Bloom Filter (`BloomFilter`)
- **File**: `src/core/bloom_filter.hpp`, `src/core/bloom_filter.cpp`
- **Role**: Probabilistic filter computing $k$ hash values using double hashing: $g_i(x) = (h_1(x) + i \cdot h_2(x)) \pmod m$.
- Prevents expensive disk reads when a requested key does not exist in the SSTable.

### 3.7. $k$-Way Merge Compactor (`Compactor`)
- **File**: `src/core/compactor.hpp`, `src/core/compactor.cpp`
- **Role**: Merges $k$ active `SSTableIterator` streams using a min-heap priority queue (`std::priority_queue<HeapNode>`).
- **Deduplication**: Retains only the newest version of duplicate keys (tie-broken by `file_id`).
- **Tombstone Purging**: Discards deletion tombstones during merge when `purge_tombstones = true`.

### 3.8. Metadata Log (`Manifest`)
- **File**: `src/core/manifest.hpp`, `src/core/manifest.cpp`
- **Role**: Crash-consistent tracker for active SSTable files. Supports append logging (`AddSSTable`) and atomic replacements (`ReplaceSSTables`) via temporary file swap and atomic rename.

### 3.9. Top-Level Coordinator (`StorageEngine`)
- **File**: `src/core/engine.hpp`, `src/core/engine.cpp`
- **Role**: Orchestrates reads across active/immutable MemTables and on-disk SSTables, controls background MemTable flushes, auto-compaction triggering, and startup recovery.

---

## 4. End-to-End Operational Workflows

### 4.1. Write Workflow (`Put` / `Delete`)
1. Client issues `Put(key, val)` or `Delete(key)`.
2. `StorageEngine` acquires concurrency mutex `std::lock_guard<std::mutex>`.
3. `MemTable::Put` / `MemTable::Delete`:
   - Serializes record and appends to `wal.log` with computed CRC32.
   - Inserts entry (or tombstone) into `SkipList`.
   - Increments dynamic memory footprint.
4. If `ApproximateMemoryUsage() >= write_buffer_size_`:
   - Flushes `immutable_memtable_` to `data_<id>.sst`.
   - Registers filename in `MANIFEST`.
   - Truncates `wal.log`.
   - Allocates a fresh active `MemTable`.

### 4.2. Read Workflow (`Get`)
1. Client requests `Get(key)`.
2. Check **Active MemTable**: If found, return value (or report deleted if tombstone).
3. Check **Immutable MemTable** (if currently flushing): If found, return value / tombstone.
4. Scan **On-Disk SSTables** (newest to oldest):
   - Evaluate in-memory **Bloom Filter**: If negative, skip SSTable entirely ($O(1)$ RAM).
   - Perform binary search on in-memory **Sparse Index** (`std::lower_bound`) to find candidate block byte offset.
   - Seek file stream to offset and scan sequential records (at most 16 entries).
   - If key matches, return record value / tombstone status.
5. If absent from all layers, return `false` (key not found).

### 4.3. Compaction Workflow
1. Storage engine collects $k$ active SSTables as `CompactorInput` instances (`file_id` + `SSTableIterator`).
2. Iterators seek to the beginning and push initial records into a min-heap priority queue.
3. Top record (`winner`) is extracted (smallest key; ties broken by highest `file_id`).
4. Winner iterator is advanced with `Next()`.
5. Duplicate iterators matching `winner.key` are drained and advanced.
6. If `winner.is_deleted` and `purge_tombstones == true`, record is dropped; otherwise written to `SSTableBuilder`.
7. Atomic MANIFEST swap commits new compacted SSTable metadata and releases obsolete files.

---

## 5. Storage Formats

### 5.1. SSTable Binary File Layout
```
+-------------------------------------------------------------------+
| DATA BLOCK                                                        |
|   Record 0: [key_len: 4B][val_len: 4B][key][val][is_deleted: 1B]  |
|   Record 1: [key_len: 4B][val_len: 4B][key][val][is_deleted: 1B]  |
|   ...                                                             |
+-------------------------------------------------------------------+
| SPARSE INDEX BLOCK                                                |
|   Entry 0:  [key_len: 4B][key][offset: 8B] (Sampled every 16 keys)|
|   Entry 1:  [key_len: 4B][key][offset: 8B]                        |
|   ...                                                             |
+-------------------------------------------------------------------+
| BLOOM FILTER BLOCK                                                |
|   [k_hashes: 4B][num_bytes: 4B][raw_bitset_bytes...]              |
+-------------------------------------------------------------------+
| METADATA FOOTER (Fixed 40 Bytes)                                  |
|   [index_offset:  8B uint64]                                      |
|   [index_size:    8B uint64]                                      |
|   [filter_offset: 8B uint64]                                      |
|   [filter_size:   8B uint64]                                      |
|   [entry_count:   4B uint32]                                      |
|   [magic_number:  8B uint64 = 0x4C534D5452454531 ("LSMTREE1")]    |
+-------------------------------------------------------------------+
```

---

## 6. Algorithmic Complexity Analysis

| Operation | Component | Time Complexity (Average) | Time Complexity (Worst) | Space Complexity |
|---|---|---|---|---|
| **Write (`Put`/`Delete`)** | `MemTable` | $O(\log N)$ | $O(N)$ | $O(1)$ amortized |
| **Write (`WAL Append`)** | `WAL` | $O(K + V)$ | $O(K + V)$ | $O(1)$ disk I/O |
| **MemTable Flush** | `SSTableBuilder` | $O(N \log N)$ | $O(N \log N)$ | $O(N / 16)$ RAM |
| **Point Lookup (Hit RAM)**| `SkipList` | $O(\log N)$ | $O(N)$ | $O(1)$ |
| **Point Lookup (Disk)** | `SSTableReader` | $O(1)_{\text{Bloom}} + O(\log M) + O(16)_{\text{Scan}}$ | $O(\log M + 16)$ | $O(M)$ where $M = N/16$ |
| **Compaction ($k$-Way)** | `Compactor` | $O(N_{\text{total}} \log k)$ | $O(N_{\text{total}} \log k)$ | $O(k)$ RAM streaming |
| **Manifest Replacement**| `Manifest` | $O(F)$ ($F$ = file count) | $O(F)$ | $O(F)$ RAM |

---

## 7. Crash Recovery & Durability Guarantees

1. **Write Durability**: Every write is synchronously flushed to `wal.log` with a CRC32 checksum before in-memory index updates are confirmed.
2. **Metadata Durability**: MANIFEST updates are written to `MANIFEST.tmp` and swapped via `std::filesystem::rename`, providing POSIX atomicity against abrupt power loss.
3. **SSTable Immutability**: Flushed SSTables are append-only and never modified in place. Crashed writes produce incomplete files that are automatically deleted during error teardown.
4. **Startup Replay**:
   - `Manifest::LoadSSTables()` recovers all active SSTable files.
   - `WAL::Recover()` validates CRC32 frames and restores unflushed mutations into the active `MemTable`.
