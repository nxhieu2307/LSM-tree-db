#pragma once

#include "bloom_filter.hpp"
#include "sstable_format.hpp"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace lsm {

class SSTableBuilder {
public:
  explicit SSTableBuilder(const std::string &file_path);
  ~SSTableBuilder();

  // Disallow copy/move to maintain unique file handle ownership
  SSTableBuilder(const SSTableBuilder &) = delete;
  SSTableBuilder &operator=(const SSTableBuilder &) = delete;
  SSTableBuilder(SSTableBuilder &&) = delete;
  SSTableBuilder &operator=(SSTableBuilder &&) = delete;

  // Add a key-value entry (or deletion tombstone) to the SSTable.
  // Keys must be added in strictly non-decreasing (sorted) order.
  bool Add(const std::string &key, const std::string &value, bool is_deleted = false);

  // Finalize the SSTable file writing the index block and footer.
  // Returns true on success, false on failure.
  bool Finish();

private:
  std::string file_path_;
  std::ofstream file_;
  uint64_t entry_count_{0};
  uint64_t current_offset_{0};
  std::vector<IndexEntry> index_entries_;
  BloomFilter bloom_filter_;
  bool finished_{false};
};

} // namespace lsm
