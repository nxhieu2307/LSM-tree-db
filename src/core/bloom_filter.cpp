#include "bloom_filter.hpp"
#include <cmath>
#include <functional>

namespace lsm {

BloomFilter::BloomFilter(size_t expected_items, double fp_rate)
    : expected_items_(expected_items), fp_rate_(fp_rate) {
  if (expected_items_ == 0) {
    expected_items_ = 1;
  }
  if (fp_rate_ <= 0.0 || fp_rate_ >= 1.0) {
    fp_rate_ = 0.01;
  }

  // Bit array size: m = - (N * ln(P)) / (ln(2))^2
  double num = -static_cast<double>(expected_items_) * std::log(fp_rate_);
  double denom = std::log(2.0) * std::log(2.0);
  double m_double = num / denom;

  bit_count_ = static_cast<size_t>(std::ceil(m_double));
  if (bit_count_ == 0) {
    bit_count_ = 1;
  }

  // Hash count: k = (m / N) * ln(2)
  double k_double = (static_cast<double>(bit_count_) / static_cast<double>(expected_items_)) * std::log(2.0);
  hash_count_ = static_cast<size_t>(std::round(k_double));
  if (hash_count_ == 0) {
    hash_count_ = 1;
  }

  size_t byte_count = (bit_count_ + 7) / 8;
  bits_.resize(byte_count, 0);
}

void BloomFilter::GetHashes(const std::string &key, uint64_t &h1, uint64_t &h2) const {
  h1 = static_cast<uint64_t>(std::hash<std::string>{}(key));
  h2 = static_cast<uint64_t>(std::hash<std::string>{}(key + "\x01"));
  if (h2 == 0) {
    h2 = 1;
  }
}

void BloomFilter::Add(const std::string &key) {
  if (bit_count_ == 0) return;

  uint64_t h1 = 0, h2 = 0;
  GetHashes(key, h1, h2);

  for (size_t i = 0; i < hash_count_; ++i) {
    uint64_t bit_idx = (static_cast<uint64_t>(i) * h2 + h1) % bit_count_;
    size_t byte_idx = static_cast<size_t>(bit_idx / 8);
    uint8_t bit_mask = static_cast<uint8_t>(1u << (bit_idx % 8));
    bits_[byte_idx] |= bit_mask;
  }
}

bool BloomFilter::MayContain(const std::string &key) const {
  if (bit_count_ == 0) return false;

  uint64_t h1 = 0, h2 = 0;
  GetHashes(key, h1, h2);

  for (size_t i = 0; i < hash_count_; ++i) {
    uint64_t bit_idx = (static_cast<uint64_t>(i) * h2 + h1) % bit_count_;
    size_t byte_idx = static_cast<size_t>(bit_idx / 8);
    uint8_t bit_mask = static_cast<uint8_t>(1u << (bit_idx % 8));
    if ((bits_[byte_idx] & bit_mask) == 0) {
      return false;
    }
  }
  return true;
}

void BloomFilter::Serialize(std::ostream &out) const {
  uint64_t exp_items = static_cast<uint64_t>(expected_items_);
  double fp_rate = fp_rate_;
  uint64_t bit_cnt = static_cast<uint64_t>(bit_count_);
  uint64_t hash_cnt = static_cast<uint64_t>(hash_count_);
  uint64_t byte_cnt = static_cast<uint64_t>(bits_.size());

  out.write(reinterpret_cast<const char *>(&exp_items), sizeof(exp_items));
  out.write(reinterpret_cast<const char *>(&fp_rate), sizeof(fp_rate));
  out.write(reinterpret_cast<const char *>(&bit_cnt), sizeof(bit_cnt));
  out.write(reinterpret_cast<const char *>(&hash_cnt), sizeof(hash_cnt));
  out.write(reinterpret_cast<const char *>(&byte_cnt), sizeof(byte_cnt));
  if (byte_cnt > 0) {
    out.write(reinterpret_cast<const char *>(bits_.data()), static_cast<std::streamsize>(byte_cnt));
  }
}

void BloomFilter::Deserialize(std::istream &in) {
  uint64_t exp_items = 0;
  double fp_rate = 0.0;
  uint64_t bit_cnt = 0;
  uint64_t hash_cnt = 0;
  uint64_t byte_cnt = 0;

  in.read(reinterpret_cast<char *>(&exp_items), sizeof(exp_items));
  in.read(reinterpret_cast<char *>(&fp_rate), sizeof(fp_rate));
  in.read(reinterpret_cast<char *>(&bit_cnt), sizeof(bit_cnt));
  in.read(reinterpret_cast<char *>(&hash_cnt), sizeof(hash_cnt));
  in.read(reinterpret_cast<char *>(&byte_cnt), sizeof(byte_cnt));

  expected_items_ = static_cast<size_t>(exp_items);
  fp_rate_ = fp_rate;
  bit_count_ = static_cast<size_t>(bit_cnt);
  hash_count_ = static_cast<size_t>(hash_cnt);

  bits_.resize(static_cast<size_t>(byte_cnt));
  if (byte_cnt > 0) {
    in.read(reinterpret_cast<char *>(bits_.data()), static_cast<std::streamsize>(byte_cnt));
  }
}

} // namespace lsm
