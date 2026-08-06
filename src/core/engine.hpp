#pragma once

#include "memtable.hpp"
#include "sstable_reader.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lsm {

class StorageEngine {
public:
  explicit StorageEngine(size_t write_buffer_size = 4096,
                         const std::string &wal_path = "wal.log",
                         const std::string &db_dir = ".");
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

  // Metadata & inspection accessors
  size_t sstable_count() const;
  std::vector<std::shared_ptr<SSTableReader>> sstables() const;
  size_t write_buffer_size() const { return write_buffer_size_; }

private:
  // Helper to generate next SSTable file path (e.g. db_dir_/data_<id>.sst)
  std::string NextSSTablePath();
  void FlushMemTableInternal();

  size_t write_buffer_size_;
  std::string wal_path_;
  std::string db_dir_;

  std::unique_ptr<MemTable> active_memtable_;
  std::unique_ptr<MemTable> immutable_memtable_;
  std::vector<std::shared_ptr<SSTableReader>> sstables_;
  std::atomic<uint64_t> sstable_id_counter_{0};

  mutable std::mutex mutex_;
};

} // namespace lsm
