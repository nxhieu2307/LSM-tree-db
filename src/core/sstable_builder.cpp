#include "sstable_builder.hpp"

namespace lsm {

SSTableBuilder::SSTableBuilder(const std::string &file_path)
    : file_path_(file_path), fd_(-1), entry_count_(0), current_offset_(0),
      finished_(false) {}

SSTableBuilder::~SSTableBuilder() {
  if (!finished_) {
    // Stub cleanup if Finish() was not called
  }
}

bool SSTableBuilder::Add(const std::string &key, const std::string &value,
                         bool is_deleted) {
  (void)key;
  (void)value;
  (void)is_deleted;
  return true;
}

bool SSTableBuilder::Finish() {
  finished_ = true;
  return true;
}

} // namespace lsm
