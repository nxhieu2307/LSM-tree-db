#pragma once

#include "skiplist.hpp"
#include "wal.hpp"
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

namespace lsm {

class MemTable {
public:
  // Iterator over MemTable contents wrapping SkipList::Iterator
  class Iterator {
  public:
    explicit Iterator(const SkipList &list);
    ~Iterator() = default;

    bool Valid() const;
    void SeekToFirst();
    void Seek(const std::string &target);
    void Next();

    std::string key() const;
    std::string value() const;
    ValueType type() const;
    MemTableEntry entry() const;
    bool IsDeleted() const;

  private:
    SkipList::Iterator iter_;
  };

  // Constructs a MemTable instance.
  // If wal_path is non-empty, creates/opens a Write-Ahead Log at wal_path
  // and replays existing log entries into memory.
  explicit MemTable(const std::string &wal_path = "");
  ~MemTable() = default;

  // Disallow copy/move to maintain unique ownership of SkipList and WAL
  MemTable(const MemTable &) = delete;
  MemTable &operator=(const MemTable &) = delete;
  MemTable(MemTable &&) = delete;
  MemTable &operator=(MemTable &&) = delete;

  // Atomic write operation: Appends entry to WAL first, then updates SkipList.
  // Fails if MemTable is marked as immutable or WAL append fails.
  bool Put(const std::string &key, const std::string &value);

  // Atomic delete operation: Appends tombstone to WAL first, then inserts deletion entry.
  // Fails if MemTable is marked as immutable or WAL append fails.
  bool Delete(const std::string &key);

  // Lookup operation:
  // Returns true if key exists and is not deleted (populates *value if non-null).
  // Returns false if key does not exist or has been deleted with a tombstone (populates *is_deleted if non-null).
  bool Get(const std::string &key, std::string *value, bool *is_deleted = nullptr) const;

  // Creates a new iterator over the MemTable snapshot.
  std::unique_ptr<Iterator> NewIterator() const;

  // Returns approximate memory usage in bytes consumed by entries.
  size_t ApproximateMemoryUsage() const;

  // Returns total number of key entries in the MemTable.
  size_t Count() const;

  // Returns true if the MemTable contains no entries.
  bool Empty() const;

  // Mark the MemTable as immutable (read-only for flushes).
  void MarkImmutable();

  // Returns true if the MemTable is marked immutable.
  bool IsImmutable() const;

  // Returns the WAL file path associated with this MemTable.
  std::string GetWALPath() const;

  // Returns pointer to underlying WAL.
  WAL *GetWAL() const;

  // Clear all entries and reset memory tracking.
  void Clear();

private:
  // Replays entries from log file into SkipList during initialization.
  void RecoverFromWAL();

  SkipList list_;
  std::unique_ptr<WAL> wal_;
  std::string wal_path_;
  mutable std::mutex mutex_;
  size_t approx_memory_usage_{0};
  size_t num_entries_{0};
  bool is_immutable_{false};
};

} // namespace lsm
