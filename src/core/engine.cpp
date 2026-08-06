#include "engine.hpp"
#include "sstable_builder.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace lsm {

StorageEngine::StorageEngine(size_t write_buffer_size,
                             const std::string &wal_path,
                             const std::string &db_dir)
    : write_buffer_size_(write_buffer_size), wal_path_(wal_path),
      db_dir_(db_dir) {
  active_memtable_ = std::make_unique<MemTable>(wal_path_);
}

StorageEngine::~StorageEngine() = default;

std::string StorageEngine::NextSSTablePath() {
  uint64_t id = sstable_id_counter_++;
  if (db_dir_.empty() || db_dir_ == ".") {
    return "data_" + std::to_string(id) + ".sst";
  }
  return db_dir_ + "/data_" + std::to_string(id) + ".sst";
}

bool StorageEngine::Put(const std::string &key, const std::string &value) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!active_memtable_->Put(key, value)) {
    return false;
  }

  if (active_memtable_->ApproximateMemoryUsage() >= write_buffer_size_) {
    FlushMemTableInternal();
  }

  return true;
}

bool StorageEngine::Delete(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!active_memtable_->Delete(key)) {
    return false;
  }

  if (active_memtable_->ApproximateMemoryUsage() >= write_buffer_size_) {
    FlushMemTableInternal();
  }

  return true;
}

bool StorageEngine::Get(const std::string &key, std::string *value,
                        bool *is_deleted) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::string val;
  bool deleted = false;

  // Check 1: Search active_memtable
  if (active_memtable_) {
    if (active_memtable_->Get(key, &val, &deleted)) {
      if (value) *value = val;
      if (is_deleted) *is_deleted = false;
      return true;
    } else if (deleted) {
      if (value) *value = "";
      if (is_deleted) *is_deleted = true;
      return true;
    }
  }

  // Check 2: Search immutable_memtable (if present)
  if (immutable_memtable_) {
    if (immutable_memtable_->Get(key, &val, &deleted)) {
      if (value) *value = val;
      if (is_deleted) *is_deleted = false;
      return true;
    } else if (deleted) {
      if (value) *value = "";
      if (is_deleted) *is_deleted = true;
      return true;
    }
  }

  // Check 3: Search SSTables sequentially from newest to oldest
  for (const auto &sstable : sstables_) {
    if (sstable->Get(key, value, is_deleted)) {
      return true;
    }
  }

  if (is_deleted) *is_deleted = false;
  return false;
}

void StorageEngine::FlushMemTable() {
  std::lock_guard<std::mutex> lock(mutex_);
  FlushMemTableInternal();
}

void StorageEngine::FlushMemTableInternal() {
  if (!active_memtable_ || active_memtable_->Empty()) {
    return;
  }

  // Step 1: Mark active_memtable as immutable and move it to immutable_memtable
  active_memtable_->MarkImmutable();
  immutable_memtable_ = std::move(active_memtable_);

  // Step 2: Instantiate SSTableBuilder with a new file path data_<id>.sst
  std::string sstable_path = NextSSTablePath();
  SSTableBuilder builder(sstable_path);

  // Step 3: Stream all sorted entries from immutable_memtable into SSTableBuilder
  auto iter = immutable_memtable_->NewIterator();
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    builder.Add(iter->key(), iter->value(), iter->IsDeleted());
  }

  // Step 4: Finish writing SSTable
  if (!builder.Finish()) {
    throw std::runtime_error("Failed to finish SSTable writing to " + sstable_path);
  }

  // Step 5: Instantiate a new SSTableReader and prepend to sstables (newest first)
  auto reader = std::make_shared<SSTableReader>(sstable_path);
  sstables_.insert(sstables_.begin(), reader);

  // Step 6: Reset immutable_memtable and clear/truncate the active WAL file
  immutable_memtable_.reset();

  if (!wal_path_.empty()) {
    std::ofstream wal_file(wal_path_, std::ios::out | std::ios::trunc);
    wal_file.close();
  }

  // Step 7: Create a fresh active_memtable with the truncated WAL log file
  active_memtable_ = std::make_unique<MemTable>(wal_path_);
}

size_t StorageEngine::sstable_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sstables_.size();
}

std::vector<std::shared_ptr<SSTableReader>> StorageEngine::sstables() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sstables_;
}

} // namespace lsm
