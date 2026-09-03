#include "engine.hpp"
#include "compactor.hpp"
#include "db_iterator.hpp"
#include "sstable_builder.hpp"
#include "sstable_iterator.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace lsm {

namespace {

uint64_t ExtractFileId(const std::string &filename) {
  size_t last_slash = filename.find_last_of("/\\");
  std::string base = (last_slash == std::string::npos) ? filename : filename.substr(last_slash + 1);

  size_t pos = base.rfind("data_");
  size_t prefix_len = 5;
  if (pos == std::string::npos) {
    pos = base.rfind("sstable_");
    prefix_len = 8;
  }
  if (pos == std::string::npos) {
    pos = base.rfind("compacted_");
    prefix_len = 10;
  }
  if (pos != std::string::npos) {
    size_t start = pos + prefix_len;
    size_t end = base.find(".sst", start);
    if (end != std::string::npos) {
      try {
        return std::stoull(base.substr(start, end - start));
      } catch (...) {
      }
    }
  }
  try {
    return std::stoull(base);
  } catch (...) {
  }
  return 0;
}

} // anonymous namespace

StorageEngine::StorageEngine(size_t write_buffer_size,
                             const std::string &wal_path,
                             const std::string &db_dir,
                             size_t compaction_threshold)
    : write_buffer_size_(write_buffer_size), wal_path_(wal_path),
      db_dir_(db_dir), compaction_threshold_(compaction_threshold) {
  std::string manifest_path = (db_dir_.empty() || db_dir_ == ".")
                                  ? "MANIFEST"
                                  : db_dir_ + "/MANIFEST";
  manifest_ = std::make_unique<Manifest>(manifest_path);

  // Phase 1: SSTable Recovery from MANIFEST
  std::vector<std::string> sstable_files = manifest_->LoadSSTables();
  uint64_t max_id = 0;
  for (const auto &filename : sstable_files) {
    std::ifstream test_file(filename);
    if (test_file.good()) {
      test_file.close();
      auto reader = std::make_shared<SSTableReader>(filename);
      sstables_.insert(sstables_.begin(), reader);

      uint64_t id = ExtractFileId(filename);
      if (id >= max_id) {
        max_id = id + 1;
      }
    }
  }
  sstable_id_counter_ = max_id;

  // Phase 2: WAL Replay / Active MemTable Initialization
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

  // Step 5b: Persist SSTable filename in MANIFEST
  manifest_->AddSSTable(sstable_path);

  // Step 6: Reset immutable_memtable and clear/truncate the active WAL file
  immutable_memtable_.reset();

  if (!wal_path_.empty()) {
    std::ofstream wal_file(wal_path_, std::ios::out | std::ios::trunc);
    wal_file.close();
  }

  // Step 7: Create a fresh active_memtable with the truncated WAL log file
  active_memtable_ = std::make_unique<MemTable>(wal_path_);

  // Step 8: Check if auto-compaction threshold is reached
  MaybeTriggerCompactionInternal();
}

void StorageEngine::MaybeTriggerCompaction() {
  std::lock_guard<std::mutex> lock(mutex_);
  MaybeTriggerCompactionInternal();
}

void StorageEngine::MaybeTriggerCompactionInternal() {
  if (sstables_.size() >= compaction_threshold_) {
    TriggerCompactionInternal();
  }
}

bool StorageEngine::TriggerCompaction() {
  std::lock_guard<std::mutex> lock(mutex_);
  return TriggerCompactionInternal();
}

bool StorageEngine::TriggerCompactionInternal() {
  if (sstables_.size() < compaction_threshold_) {
    return true;
  }

  std::vector<CompactorInput> inputs;
  std::vector<std::string> old_files;
  std::vector<uint64_t> old_file_ids;

  inputs.reserve(sstables_.size());
  old_files.reserve(sstables_.size());
  old_file_ids.reserve(sstables_.size());

  for (size_t i = 0; i < sstables_.size(); ++i) {
    const auto &reader = sstables_[i];
    std::string fpath = reader->filepath();
    uint64_t fid = ExtractFileId(fpath);
    if (fid == 0) {
      fid = sstables_.size() - i;
    }
    old_files.push_back(fpath);
    old_file_ids.push_back(fid);

    auto iterator = std::make_shared<SSTableIterator>(fpath);
    inputs.push_back(CompactorInput{fid, iterator});
  }

  uint64_t new_file_id = sstable_id_counter_++;
  std::string new_sstable_path;
  if (db_dir_.empty() || db_dir_ == ".") {
    new_sstable_path = "sstable_" + std::to_string(new_file_id) + ".sst";
  } else {
    new_sstable_path = db_dir_ + "/sstable_" + std::to_string(new_file_id) + ".sst";
  }

  // Step 1: Execute k-way merge compaction with tombstone purging
  if (!Compactor::Compact(inputs, new_sstable_path, 4096, /*purge_tombstones=*/true)) {
    return false;
  }

  // Step 2: Atomically update MANIFEST log
  if (!manifest_->ReplaceSSTables(old_files, new_sstable_path)) {
    if (!manifest_->ReplaceSSTables(old_file_ids, new_file_id)) {
      return false;
    }
  }

  // Step 3: Instantiate reader for newly compacted SSTable
  std::shared_ptr<SSTableReader> new_reader;
  try {
    new_reader = std::make_shared<SSTableReader>(new_sstable_path);
  } catch (const std::exception &e) {
    std::cerr << "Warning: Failed to open newly compacted SSTable " << new_sstable_path
              << ": " << e.what() << std::endl;
    return false;
  }

  // Step 4: Unpin all active iterators and old SSTable readers to close file descriptors
  inputs.clear();
  sstables_.clear();

  // Step 5: Update active in-memory SSTable collection with newly compacted reader
  sstables_.push_back(new_reader);

  // Step 6: Physically delete obsolete SSTable files from disk
  for (const auto &old_f : old_files) {
    std::error_code ec;
    if (!std::filesystem::remove(old_f, ec) && ec) {
      std::cerr << "Warning: Failed to delete obsolete SSTable " << old_f
                << ": " << ec.message() << std::endl;
    }
  }

  return true;
}

size_t StorageEngine::sstable_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sstables_.size();
}

std::vector<std::shared_ptr<SSTableReader>> StorageEngine::sstables() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sstables_;
}

std::unique_ptr<DBIterator> StorageEngine::NewIterator(
    const std::string &start_key,
    const std::string &end_key) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::shared_ptr<MemTable::Iterator> active_iter = nullptr;
  if (active_memtable_) {
    active_iter = active_memtable_->NewIterator();
  }

  std::shared_ptr<MemTable::Iterator> imm_iter = nullptr;
  if (immutable_memtable_) {
    imm_iter = immutable_memtable_->NewIterator();
  }

  std::vector<CompactorInput> sst_inputs;
  sst_inputs.reserve(sstables_.size());
  for (size_t i = 0; i < sstables_.size(); ++i) {
    const auto &reader = sstables_[i];
    std::string fpath = reader->filepath();
    uint64_t fid = ExtractFileId(fpath);
    if (fid == 0) {
      fid = sstables_.size() - i;
    }
    auto it = std::make_shared<SSTableIterator>(fpath);
    sst_inputs.push_back(CompactorInput{fid, it});
  }

  return std::make_unique<DBIterator>(active_iter, imm_iter, sst_inputs, start_key, end_key);
}

} // namespace lsm
