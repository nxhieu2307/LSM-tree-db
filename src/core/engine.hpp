#pragma once

#include "manifest.hpp"
#include "memtable.hpp"
#include "sstable_reader.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace lsm {

using KVPair = std::pair<std::string, std::string>;

class DBIterator;

class StorageEngine {
public:
  static constexpr size_t kDefaultCompactionThreshold = 4;

  explicit StorageEngine(size_t write_buffer_size = 4096,
                         const std::string &wal_path = "wal.log",
                         const std::string &db_dir = ".",
                         size_t compaction_threshold = kDefaultCompactionThreshold);
  ~StorageEngine();

  // Disallow copy/move to manage background resources safely
  StorageEngine(const StorageEngine &) = delete;
  StorageEngine &operator=(const StorageEngine &) = delete;
  StorageEngine(StorageEngine &&) = delete;
  StorageEngine &operator=(StorageEngine &&) = delete;

  // Insert or update key-value entry in DB
  bool Put(const std::string &key, const std::string &value);

  // Delete key entry by inserting tombstone record
  bool Delete(const std::string &key);

  // Search active memtable, immutable memtable, and flushed sstables for key
  bool Get(const std::string &key, std::string *value, bool *is_deleted = nullptr) const;

  // Manually or automatically flush active memtable to disk as an SSTable file
  void FlushMemTable();

  // Trigger compaction manually or check threshold
  bool TriggerCompaction();
  void MaybeTriggerCompaction();

  // Returns an active iterator positioned at start_key (or first key if empty).
  std::unique_ptr<DBIterator> NewIterator(
      const std::string &start_key = "",
      const std::string &end_key = ""
  ) const;

  // Collects key-value pairs in the range [start_key, end_key] up to limit (0 = unlimited).
  std::vector<KVPair> Scan(
      const std::string &start_key,
      const std::string &end_key,
      size_t limit = 0
  ) const;

  // Collects all key-value pairs matching the given prefix up to limit (0 = unlimited).
  std::vector<KVPair> PrefixScan(
      const std::string &prefix,
      size_t limit = 0
  ) const;

  // Metadata & inspection accessors
  size_t sstable_count() const;
  std::vector<std::shared_ptr<SSTableReader>> sstables() const;
  size_t write_buffer_size() const { return write_buffer_size_; }
  size_t compaction_threshold() const { return compaction_threshold_; }

private:
  // Helper to generate next SSTable file path (e.g. db_dir_/data_<id>.sst)
  std::string NextSSTablePath();
  void FlushMemTableInternal();
  bool TriggerCompactionInternal();
  void MaybeTriggerCompactionInternal();

  size_t write_buffer_size_;
  std::string wal_path_;
  std::string db_dir_;
  size_t compaction_threshold_{kDefaultCompactionThreshold};

  std::unique_ptr<Manifest> manifest_;
  std::unique_ptr<MemTable> active_memtable_;
  std::unique_ptr<MemTable> immutable_memtable_;
  std::vector<std::shared_ptr<SSTableReader>> sstables_;
  std::atomic<uint64_t> sstable_id_counter_{0};

  mutable std::mutex mutex_;
};

} // namespace lsm
