#include "sstable_builder.hpp"
#include "sstable_format.hpp"
#include <cstdio>

namespace lsm {

SSTableBuilder::SSTableBuilder(const std::string &file_path)
    : file_path_(file_path),
      file_(file_path, std::ios::binary | std::ios::out | std::ios::trunc),
      entry_count_(0), current_offset_(0), finished_(false) {}

SSTableBuilder::~SSTableBuilder() {
  if (!finished_) {
    if (file_.is_open()) {
      file_.close();
    }
    std::remove(file_path_.c_str());
  }
}

bool SSTableBuilder::Add(const std::string &key, const std::string &value,
                         bool is_deleted) {
  if (finished_ || !file_.is_open() || file_.fail()) {
    return false;
  }

  uint64_t offset = static_cast<uint64_t>(file_.tellp());

  uint32_t key_len = static_cast<uint32_t>(key.size());
  uint32_t val_len = static_cast<uint32_t>(value.size());
  uint8_t del_byte = is_deleted ? 1 : 0;

  file_.write(reinterpret_cast<const char *>(&key_len), sizeof(key_len));
  file_.write(reinterpret_cast<const char *>(&val_len), sizeof(val_len));
  if (key_len > 0) {
    file_.write(key.data(), key_len);
  }
  if (val_len > 0) {
    file_.write(value.data(), val_len);
  }
  file_.write(reinterpret_cast<const char *>(&del_byte), sizeof(del_byte));

  if (file_.fail()) {
    return false;
  }

  if (entry_count_ % kSparseIndexInterval == 0) {
    index_entries_.push_back(IndexEntry{key, offset});
  }

  bloom_filter_.Add(key);

  entry_count_++;
  current_offset_ = static_cast<uint64_t>(file_.tellp());
  return true;
}

bool SSTableBuilder::Finish() {
  if (finished_ || !file_.is_open() || file_.fail()) {
    return false;
  }

  uint64_t index_offset = static_cast<uint64_t>(file_.tellp());

  for (const auto &idx_entry : index_entries_) {
    uint32_t key_len = static_cast<uint32_t>(idx_entry.key.size());
    uint64_t offset = idx_entry.offset;

    file_.write(reinterpret_cast<const char *>(&key_len), sizeof(key_len));
    if (key_len > 0) {
      file_.write(idx_entry.key.data(), key_len);
    }
    file_.write(reinterpret_cast<const char *>(&offset), sizeof(offset));
  }

  uint64_t index_end = static_cast<uint64_t>(file_.tellp());
  uint64_t index_size = index_end - index_offset;

  uint64_t filter_offset = static_cast<uint64_t>(file_.tellp());
  bloom_filter_.Serialize(file_);

  uint64_t filter_end = static_cast<uint64_t>(file_.tellp());
  uint64_t filter_size = filter_end - filter_offset;

  SSTableFooter footer;
  footer.index_offset = index_offset;
  footer.index_size = index_size;
  footer.filter_offset = filter_offset;
  footer.filter_size = filter_size;
  footer.entry_count = static_cast<uint32_t>(entry_count_);
  footer.magic_number = kSSTableMagicNumber;

  file_.write(reinterpret_cast<const char *>(&footer), sizeof(footer));

  file_.flush();
  file_.close();

  if (file_.fail()) {
    return false;
  }

  finished_ = true;
  return true;
}

} // namespace lsm
