#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace lsm {

class BloomFilter {
public:
  // Constructor accepting expected item count N (default 1000) and false positive rate P (default 0.01).
  explicit BloomFilter(size_t expected_items = 1000, double fp_rate = 0.01);
  ~BloomFilter() = default;

  BloomFilter(const BloomFilter &) = default;
  BloomFilter &operator=(const BloomFilter &) = default;
  BloomFilter(BloomFilter &&) noexcept = default;
  BloomFilter &operator=(BloomFilter &&) noexcept = default;

  // Add a key to the Bloom Filter (sets target bits to true).
  void Add(const std::string &key);

  // Check if key may be in the filter. Returns false if any bit is false, else true.
  bool MayContain(const std::string &key) const;

  // Write filter metadata and bit array to binary streams.
  void Serialize(std::ostream &out) const;

  // Read filter metadata and bit array from binary streams.
  void Deserialize(std::istream &in);

  // Accessors
  size_t GetExpectedItems() const { return expected_items_; }
  double GetFPRate() const { return fp_rate_; }
  size_t GetBitCount() const { return bit_count_; }
  size_t GetHashCount() const { return hash_count_; }
  const std::vector<uint8_t> &GetBits() const { return bits_; }

private:
  void GetHashes(const std::string &key, uint64_t &h1, uint64_t &h2) const;

  size_t expected_items_{1000};
  double fp_rate_{0.01};
  size_t bit_count_{0};  // m
  size_t hash_count_{0}; // k
  std::vector<uint8_t> bits_; // Bit array packed as bytes
};

} // namespace lsm
