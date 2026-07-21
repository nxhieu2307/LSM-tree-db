#include "memtable.hpp"

namespace lsm {

MemTable::MemTable(const std::string &wal_path) {
  if (!wal_path.empty()) {
    wal_ = std::make_unique<WAL>(wal_path);
  }
}

bool MemTable::Put(const std::string &key, const std::string &value) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (wal_) {
    if (!wal_->Append("PUT", key, value)) {
      return false;
    }
  }

  MemTableEntry new_entry{value, ValueType::kTypeValue};
  list_.Insert(key, std::move(new_entry));
  return true;
}

bool MemTable::Delete(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (wal_) {
    if (!wal_->Append("DELETE", key, "")) {
      return false;
    }
  }

  MemTableEntry new_entry{"", ValueType::kTypeDeletion};
  list_.Insert(key, std::move(new_entry));
  return true;
}

bool MemTable::Get(const std::string &key, std::string *value, bool *is_deleted) const {
  std::lock_guard<std::mutex> lock(mutex_);

  MemTableEntry entry;
  if (!list_.Find(key, &entry)) {
    if (is_deleted) {
      *is_deleted = false;
    }
    return false;
  }

  if (entry.type == ValueType::kTypeDeletion) {
    if (is_deleted) {
      *is_deleted = true;
    }
    return false;
  }

  if (value) {
    *value = entry.value;
  }
  if (is_deleted) {
    *is_deleted = false;
  }
  return true;
}

} // namespace lsm
