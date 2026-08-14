#pragma once

#include "sstable_reader.hpp"
#include <cstdint>
#include <fstream>
#include <string>

namespace lsm {

struct SSTableRecord {
  std::string key;
  std::string value;
  bool is_deleted{false};
};

class SSTableIterator {
public:
  explicit SSTableIterator(const std::string &filepath);
  explicit SSTableIterator(const SSTableReader &reader);
  ~SSTableIterator();

  // Disallow copy/move to maintain unique file stream ownership
  SSTableIterator(const SSTableIterator &) = delete;
  SSTableIterator &operator=(const SSTableIterator &) = delete;
  SSTableIterator(SSTableIterator &&) = delete;
  SSTableIterator &operator=(SSTableIterator &&) = delete;

  // Rewinds file position to byte 0 and reads the first entry.
  void SeekToFirst();

  // Returns true if current cursor position is within the Data Block boundary and hasn't hit EOF.
  bool Valid() const;

  // Advances the cursor and parses the next sequential record [key_len][val_len][key][val][is_deleted].
  void Next();

  // Returns current record key.
  std::string Key() const;

  // Returns current record value.
  std::string Value() const;

  // Returns current record tombstone flag.
  bool IsDeleted() const;

private:
  void ReadNextRecord();

  std::string filepath_;
  mutable std::ifstream file_;
  uint64_t data_end_offset_{0};
  uint64_t current_offset_{0};
  SSTableRecord current_record_;
  bool valid_{false};
};

} // namespace lsm
