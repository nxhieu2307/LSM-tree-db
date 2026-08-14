#include "sstable_iterator.hpp"
#include "sstable_format.hpp"
#include <stdexcept>

namespace lsm {

SSTableIterator::SSTableIterator(const std::string &filepath)
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

  // Read packed footer from last 40 bytes
  SSTableFooter footer;
  file_.seekg(file_size - static_cast<std::streamsize>(sizeof(SSTableFooter)), std::ios::beg);
  file_.read(reinterpret_cast<char *>(&footer), sizeof(SSTableFooter));

  if (file_.fail() || footer.magic_number != kSSTableMagicNumber) {
    throw std::runtime_error("Invalid SSTable footer magic number in file: " + filepath);
  }

  data_end_offset_ = footer.index_offset;

  SeekToFirst();
}

SSTableIterator::SSTableIterator(const SSTableReader &reader)
    : SSTableIterator(reader.filepath()) {}

SSTableIterator::~SSTableIterator() {
  if (file_.is_open()) {
    file_.close();
  }
}

void SSTableIterator::SeekToFirst() {
  if (!file_.is_open()) {
    valid_ = false;
    return;
  }
  file_.clear();
  file_.seekg(0, std::ios::beg);
  current_offset_ = 0;
  ReadNextRecord();
}

bool SSTableIterator::Valid() const {
  return valid_;
}

void SSTableIterator::Next() {
  if (!valid_) {
    return;
  }
  ReadNextRecord();
}

std::string SSTableIterator::Key() const {
  return current_record_.key;
}

std::string SSTableIterator::Value() const {
  return current_record_.value;
}

bool SSTableIterator::IsDeleted() const {
  return current_record_.is_deleted;
}

void SSTableIterator::ReadNextRecord() {
  if (!file_.is_open()) {
    valid_ = false;
    return;
  }

  uint64_t start_pos = static_cast<uint64_t>(file_.tellg());
  if (start_pos >= data_end_offset_) {
    valid_ = false;
    return;
  }

  uint32_t key_len = 0;
  uint32_t val_len = 0;

  file_.read(reinterpret_cast<char *>(&key_len), sizeof(key_len));
  if (file_.fail()) {
    valid_ = false;
    return;
  }

  file_.read(reinterpret_cast<char *>(&val_len), sizeof(val_len));
  if (file_.fail()) {
    valid_ = false;
    return;
  }

  std::string key(key_len, '\0');
  if (key_len > 0) {
    file_.read(&key[0], key_len);
    if (file_.fail()) {
      valid_ = false;
      return;
    }
  }

  std::string val(val_len, '\0');
  if (val_len > 0) {
    file_.read(&val[0], val_len);
    if (file_.fail()) {
      valid_ = false;
      return;
    }
  }

  uint8_t del_byte = 0;
  file_.read(reinterpret_cast<char *>(&del_byte), sizeof(del_byte));
  if (file_.fail()) {
    valid_ = false;
    return;
  }

  uint64_t end_pos = static_cast<uint64_t>(file_.tellg());
  if (end_pos > data_end_offset_) {
    valid_ = false;
    return;
  }

  current_record_.key = std::move(key);
  current_record_.value = std::move(val);
  current_record_.is_deleted = (del_byte != 0);
  current_offset_ = end_pos;
  valid_ = true;
}

} // namespace lsm
