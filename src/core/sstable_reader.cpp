#include "sstable_reader.hpp"
#include <algorithm>
#include <stdexcept>

namespace lsm {

SSTableReader::SSTableReader(const std::string &filepath)
    : filepath_(filepath), file_(filepath, std::ios::binary) {
  if (!file_.is_open()) {
    throw std::runtime_error("Failed to open SSTable file: " + filepath);
  }

  // Determine total file size
  file_.seekg(0, std::ios::end);
  std::streamsize file_size = file_.tellg();

  if (file_size < static_cast<std::streamsize>(sizeof(SSTableFooter))) {
    throw std::runtime_error("SSTable file is too small to contain footer: " + filepath);
  }

  // Seek to end minus sizeof(SSTableFooter) to read and parse the packed footer
  file_.seekg(file_size - static_cast<std::streamsize>(sizeof(SSTableFooter)), std::ios::beg);
  file_.read(reinterpret_cast<char *>(&footer_), sizeof(SSTableFooter));

  if (file_.fail() || footer_.magic_number != kSSTableMagicNumber) {
    throw std::runtime_error("Invalid SSTable footer magic number in file: " + filepath);
  }

  // Seek to footer.filter_offset and deserialize the Bloom Filter
  file_.seekg(footer_.filter_offset, std::ios::beg);
  bloom_filter_.Deserialize(file_);

  // Seek to footer.index_offset and deserialize the Sparse Index
  file_.seekg(footer_.index_offset, std::ios::beg);
  uint64_t bytes_read = 0;

  while (bytes_read < footer_.index_size && file_.good()) {
    uint32_t key_len = 0;
    file_.read(reinterpret_cast<char *>(&key_len), sizeof(key_len));
    if (file_.fail()) break;

    std::string key(key_len, '\0');
    if (key_len > 0) {
      file_.read(&key[0], key_len);
    }

    uint64_t offset = 0;
    file_.read(reinterpret_cast<char *>(&offset), sizeof(offset));

    if (file_.fail()) break;

    bytes_read += sizeof(key_len) + key_len + sizeof(offset);
    sparse_index_.emplace_back(std::move(key), offset);
  }
}

SSTableReader::~SSTableReader() {
  if (file_.is_open()) {
    file_.close();
  }
}

bool SSTableReader::Get(const std::string &key, std::string *value, bool *is_deleted) {
  if (!bloom_filter_.MayContain(key)) {
    return false;
  }

  if (sparse_index_.empty()) {
    return false;
  }

  // Perform binary search (std::lower_bound) on sparse_index to find candidate offset
  auto it = std::lower_bound(
      sparse_index_.begin(), sparse_index_.end(), key,
      [](const std::pair<std::string, uint64_t> &entry, const std::string &k) {
        return entry.first < k;
      });

  uint64_t start_offset = 0;
  if (it == sparse_index_.end()) {
    start_offset = sparse_index_.back().second;
  } else if (it->first == key) {
    start_offset = it->second;
  } else if (it == sparse_index_.begin()) {
    start_offset = it->second;
  } else {
    --it;
    start_offset = it->second;
  }

  // Seek the file stream directly to data block offset
  file_.clear();
  file_.seekg(start_offset, std::ios::beg);

  // Scan forward sequentially parsing records [key_len][val_len][key][val][is_deleted]
  while (file_.good()) {
    uint64_t current_pos = static_cast<uint64_t>(file_.tellg());
    if (current_pos >= footer_.index_offset) {
      break; // Reached sparse index block
    }

    uint32_t key_len = 0;
    uint32_t val_len = 0;

    file_.read(reinterpret_cast<char *>(&key_len), sizeof(key_len));
    if (file_.fail()) break;

    file_.read(reinterpret_cast<char *>(&val_len), sizeof(val_len));
    if (file_.fail()) break;

    std::string rec_key(key_len, '\0');
    if (key_len > 0) {
      file_.read(&rec_key[0], key_len);
    }

    std::string rec_val(val_len, '\0');
    if (val_len > 0) {
      file_.read(&rec_val[0], val_len);
    }

    uint8_t del_byte = 0;
    file_.read(reinterpret_cast<char *>(&del_byte), sizeof(del_byte));

    if (file_.fail()) break;

    if (rec_key == key) {
      if (value) *value = rec_val;
      if (is_deleted) *is_deleted = (del_byte != 0);
      return true;
    }

    if (rec_key > key) {
      return false; // Key not found in this SSTable (data is strictly sorted)
    }
  }

  return false;
}

} // namespace lsm
