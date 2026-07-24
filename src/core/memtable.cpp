#include "memtable.hpp"

namespace lsm {

// Static helper to estimate memory footprint of a single SkipList node entry
static size_t EstimateNodeMemory(const std::string &key, const std::string &value) {
  // Base Node size + string buffers heap storage + trailing pointer array allocation average (~2 forward pointers)
  return sizeof(SkipList::Node) + key.size() + value.size() + 32;
}

// MemTable::Iterator Implementation
MemTable::Iterator::Iterator(const SkipList &list) : iter_(list) {}

bool MemTable::Iterator::Valid() const { return iter_.Valid(); }

void MemTable::Iterator::SeekToFirst() { iter_.SeekToFirst(); }

void MemTable::Iterator::Seek(const std::string &target) { iter_.Seek(target); }

void MemTable::Iterator::Next() { iter_.Next(); }

std::string MemTable::Iterator::key() const { return iter_.key(); }

std::string MemTable::Iterator::value() const { return iter_.entry().value; }

ValueType MemTable::Iterator::type() const { return iter_.entry().type; }

MemTableEntry MemTable::Iterator::entry() const { return iter_.entry(); }

bool MemTable::Iterator::IsDeleted() const {
  return iter_.entry().type == ValueType::kTypeDeletion;
}

// MemTable Implementation
MemTable::MemTable(const std::string &wal_path) : wal_path_(wal_path) {
  if (!wal_path_.empty()) {
    wal_ = std::make_unique<WAL>(wal_path_);
    RecoverFromWAL();
  }
}

void MemTable::RecoverFromWAL() {
  if (!wal_) return;

  std::vector<LogEntry> entries;
  if (!wal_->Recover(entries)) return;

  for (const auto &log_entry : entries) {
    MemTableEntry existing;
    bool found = list_.Find(log_entry.key, &existing);

    if (log_entry.operation == "PUT") {
      if (found) {
        if (log_entry.value.size() >= existing.value.size()) {
          approx_memory_usage_ += (log_entry.value.size() - existing.value.size());
        } else {
          approx_memory_usage_ -= (existing.value.size() - log_entry.value.size());
        }
      } else {
        approx_memory_usage_ += EstimateNodeMemory(log_entry.key, log_entry.value);
        num_entries_++;
      }
      list_.Insert(log_entry.key, MemTableEntry{log_entry.value, ValueType::kTypeValue});
    } else if (log_entry.operation == "DELETE") {
      if (found) {
        if (existing.value.size() > 0) {
          approx_memory_usage_ -= existing.value.size();
        }
      } else {
        approx_memory_usage_ += EstimateNodeMemory(log_entry.key, "");
        num_entries_++;
      }
      list_.Insert(log_entry.key, MemTableEntry{"", ValueType::kTypeDeletion});
    }
  }
}

bool MemTable::Put(const std::string &key, const std::string &value) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (is_immutable_) {
    return false;
  }

  if (wal_) {
    if (!wal_->Append("PUT", key, value)) {
      return false;
    }
  }

  MemTableEntry existing;
  bool found = list_.Find(key, &existing);

  if (found) {
    if (value.size() >= existing.value.size()) {
      approx_memory_usage_ += (value.size() - existing.value.size());
    } else {
      approx_memory_usage_ -= (existing.value.size() - value.size());
    }
  } else {
    approx_memory_usage_ += EstimateNodeMemory(key, value);
    num_entries_++;
  }

  MemTableEntry new_entry{value, ValueType::kTypeValue};
  list_.Insert(key, std::move(new_entry));
  return true;
}

bool MemTable::Delete(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (is_immutable_) {
    return false;
  }

  if (wal_) {
    if (!wal_->Append("DELETE", key, "")) {
      return false;
    }
  }

  MemTableEntry existing;
  bool found = list_.Find(key, &existing);

  if (found) {
    if (existing.value.size() > 0) {
      approx_memory_usage_ -= existing.value.size();
    }
  } else {
    approx_memory_usage_ += EstimateNodeMemory(key, "");
    num_entries_++;
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

std::unique_ptr<MemTable::Iterator> MemTable::NewIterator() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::make_unique<Iterator>(list_);
}

size_t MemTable::ApproximateMemoryUsage() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return approx_memory_usage_;
}

size_t MemTable::Count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return num_entries_;
}

bool MemTable::Empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return list_.Empty();
}

void MemTable::MarkImmutable() {
  std::lock_guard<std::mutex> lock(mutex_);
  is_immutable_ = true;
}

bool MemTable::IsImmutable() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return is_immutable_;
}

std::string MemTable::GetWALPath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return wal_path_;
}

WAL *MemTable::GetWAL() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return wal_.get();
}

void MemTable::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  list_.Clear();
  approx_memory_usage_ = 0;
  num_entries_ = 0;
  is_immutable_ = false;
}

} // namespace lsm
