#pragma once

#include "bloom_filter.hpp"
#include "sstable_format.hpp"
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace lsm {

class SSTableReader {
public:
  explicit SSTableReader(const std::string &filepath);
  ~SSTableReader();

  // Disallow copy/move to maintain unique file stream ownership
  SSTableReader(const SSTableReader &) = delete;
  SSTableReader &operator=(const SSTableReader &) = delete;
  SSTableReader(SSTableReader &&) = delete;
  SSTableReader &operator=(SSTableReader &&) = delete;

  // Perform point lookup for a key in the SSTable.
  // Returns true if key is found (populating value and is_deleted).
  // Returns false if key is not found.
  bool Get(const std::string &key, std::string *value, bool *is_deleted);

  // Metadata accessors
  const SSTableFooter &footer() const { return footer_; }
  const std::vector<std::pair<std::string, uint64_t>> &sparse_index() const {
    return sparse_index_;
  }
  const BloomFilter &bloom_filter() const { return bloom_filter_; }
  const std::string &filepath() const { return filepath_; }

private:
  std::string filepath_;
  mutable std::ifstream file_;
  SSTableFooter footer_;
  std::vector<std::pair<std::string, uint64_t>> sparse_index_;
  BloomFilter bloom_filter_;
};

} // namespace lsm
