#pragma once

#include "skiplist.hpp"
#include "wal.hpp"
#include <memory>
#include <mutex>
#include <string>

namespace lsm {

class MemTable {
public:
  // Constructs a MemTable instance.
  // If wal_path is non-empty, creates/opens a Write-Ahead Log at wal_path.
  explicit MemTable(const std::string &wal_path = "");
  ~MemTable() = default;

  // Disallow copy/move to maintain unique ownership of SkipList and WAL
  MemTable(const MemTable &) = delete;
  MemTable &operator=(const MemTable &) = delete;
  MemTable(MemTable &&) = delete;
  MemTable &operator=(MemTable &&) = delete;

  // Atomic write operation: Appends entry to WAL first, then updates SkipList.
  // Returns true on success, false if WAL append fails.
  bool Put(const std::string &key, const std::string &value);

  // Atomic delete operation: Appends tombstone to WAL first, then inserts deletion entry into SkipList.
  // Returns true on success, false if WAL append fails.
  bool Delete(const std::string &key);

  // Lookup operation:
  // Returns true if key exists and is not deleted (populates *value if non-null).
  // Returns false if key does not exist or has been deleted with a tombstone (populates *is_deleted if non-null).
  bool Get(const std::string &key, std::string *value, bool *is_deleted = nullptr) const;

private:
  SkipList list_;
  std::unique_ptr<WAL> wal_;
  mutable std::mutex mutex_;
};

} // namespace lsm
