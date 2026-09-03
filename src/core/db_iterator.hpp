#pragma once

#include "compactor.hpp"
#include "memtable.hpp"
#include "sstable_iterator.hpp"
#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace lsm {

struct DBIteratorSource {
  uint64_t generation_id{0};
  std::shared_ptr<MemTable::Iterator> mem_iter{nullptr};
  std::shared_ptr<SSTableIterator> sst_iter{nullptr};

  bool Valid() const;
  void SeekToFirst();
  void Seek(const std::string &target);
  void Next();
  std::string Key() const;
  std::string Value() const;
  bool IsDeleted() const;
};

class DBIterator {
public:
  // Constructor with single MemTable iterator and SSTable inputs
  explicit DBIterator(
      std::shared_ptr<MemTable::Iterator> memtable_iter,
      const std::vector<CompactorInput> &sstable_inputs,
      const std::string &start_key = "",
      const std::string &end_key = ""
  );

  // Constructor overload accepting std::unique_ptr<MemTable::Iterator>
  explicit DBIterator(
      std::unique_ptr<MemTable::Iterator> memtable_iter,
      const std::vector<CompactorInput> &sstable_inputs,
      const std::string &start_key = "",
      const std::string &end_key = ""
  );

  // Constructor with active MemTable, immutable MemTable, and SSTables
  explicit DBIterator(
      std::shared_ptr<MemTable::Iterator> active_mem_iter,
      std::shared_ptr<MemTable::Iterator> immutable_mem_iter,
      const std::vector<CompactorInput> &sstable_inputs,
      const std::string &start_key = "",
      const std::string &end_key = ""
  );

  ~DBIterator() = default;

  // Disallow copy/move to maintain unique iterator state
  DBIterator(const DBIterator &) = delete;
  DBIterator &operator=(const DBIterator &) = delete;
  DBIterator(DBIterator &&) = default;
  DBIterator &operator=(DBIterator &&) = default;

  // Repositions the iterator at the first valid key in the range
  void SeekToFirst();

  // Positions the iterator at the first key >= target_key
  void Seek(const std::string &target_key);

  // Returns true if the iterator is positioned at a valid non-deleted key
  bool Valid() const;

  // Advances the iterator to the next valid non-deleted key
  void Next();

  // Returns the current key
  std::string Key() const;

  // Returns the current value
  std::string Value() const;

private:
  struct HeapNode {
    std::string key;
    uint64_t generation_id;
    size_t source_idx;

    // Min-heap ordering:
    // 1. Primary: smallest key pops first (lexicographical ascending)
    // 2. Tie-breaker: largest generation_id pops first (newest data wins)
    bool operator>(const HeapNode &other) const {
      if (key != other.key) {
        return key > other.key;
      }
      return generation_id < other.generation_id;
    }
  };

  void InitSources(
      std::shared_ptr<MemTable::Iterator> active_mem_iter,
      std::shared_ptr<MemTable::Iterator> immutable_mem_iter,
      const std::vector<CompactorInput> &sstable_inputs
  );

  void FindNextAliveKey();

  std::vector<DBIteratorSource> sources_;
  std::string start_key_;
  std::string end_key_;

  std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> min_heap_;
  std::string current_key_;
  std::string current_value_;
  bool valid_{false};
};

} // namespace lsm
