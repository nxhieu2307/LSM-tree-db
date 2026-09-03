#include "db_iterator.hpp"

namespace lsm {

// DBIteratorSource Implementation
bool DBIteratorSource::Valid() const {
  if (mem_iter) return mem_iter->Valid();
  if (sst_iter) return sst_iter->Valid();
  return false;
}

void DBIteratorSource::SeekToFirst() {
  if (mem_iter) mem_iter->SeekToFirst();
  if (sst_iter) sst_iter->SeekToFirst();
}

void DBIteratorSource::Seek(const std::string &target) {
  if (mem_iter) {
    mem_iter->Seek(target);
  } else if (sst_iter) {
    sst_iter->SeekToFirst();
    while (sst_iter->Valid() && sst_iter->Key() < target) {
      sst_iter->Next();
    }
  }
}

void DBIteratorSource::Next() {
  if (mem_iter) mem_iter->Next();
  if (sst_iter) sst_iter->Next();
}

std::string DBIteratorSource::Key() const {
  if (mem_iter) return mem_iter->key();
  if (sst_iter) return sst_iter->Key();
  return "";
}

std::string DBIteratorSource::Value() const {
  if (mem_iter) return mem_iter->value();
  if (sst_iter) return sst_iter->Value();
  return "";
}

bool DBIteratorSource::IsDeleted() const {
  if (mem_iter) return mem_iter->IsDeleted();
  if (sst_iter) return sst_iter->IsDeleted();
  return false;
}

// DBIterator Implementation
DBIterator::DBIterator(
    std::shared_ptr<MemTable::Iterator> memtable_iter,
    const std::vector<CompactorInput> &sstable_inputs,
    const std::string &start_key,
    const std::string &end_key)
    : start_key_(start_key), end_key_(end_key) {
  InitSources(memtable_iter, nullptr, sstable_inputs);
  SeekToFirst();
}

DBIterator::DBIterator(
    std::unique_ptr<MemTable::Iterator> memtable_iter,
    const std::vector<CompactorInput> &sstable_inputs,
    const std::string &start_key,
    const std::string &end_key)
    : start_key_(start_key), end_key_(end_key) {
  std::shared_ptr<MemTable::Iterator> shared_mem = std::move(memtable_iter);
  InitSources(shared_mem, nullptr, sstable_inputs);
  SeekToFirst();
}

DBIterator::DBIterator(
    std::shared_ptr<MemTable::Iterator> active_mem_iter,
    std::shared_ptr<MemTable::Iterator> immutable_mem_iter,
    const std::vector<CompactorInput> &sstable_inputs,
    const std::string &start_key,
    const std::string &end_key)
    : start_key_(start_key), end_key_(end_key) {
  InitSources(active_mem_iter, immutable_mem_iter, sstable_inputs);
  SeekToFirst();
}

void DBIterator::InitSources(
    std::shared_ptr<MemTable::Iterator> active_mem_iter,
    std::shared_ptr<MemTable::Iterator> immutable_mem_iter,
    const std::vector<CompactorInput> &sstable_inputs) {
  sources_.clear();

  if (active_mem_iter) {
    DBIteratorSource src;
    src.generation_id = UINT64_MAX;
    src.mem_iter = active_mem_iter;
    sources_.push_back(std::move(src));
  }

  if (immutable_mem_iter) {
    DBIteratorSource src;
    src.generation_id = UINT64_MAX - 1;
    src.mem_iter = immutable_mem_iter;
    sources_.push_back(std::move(src));
  }

  for (const auto &input : sstable_inputs) {
    if (input.iterator) {
      DBIteratorSource src;
      src.generation_id = input.file_id;
      src.sst_iter = input.iterator;
      sources_.push_back(std::move(src));
    }
  }
}

void DBIterator::SeekToFirst() {
  while (!min_heap_.empty()) {
    min_heap_.pop();
  }

  for (size_t i = 0; i < sources_.size(); ++i) {
    if (!start_key_.empty()) {
      sources_[i].Seek(start_key_);
    } else {
      sources_[i].SeekToFirst();
    }

    if (sources_[i].Valid()) {
      min_heap_.push(HeapNode{sources_[i].Key(), sources_[i].generation_id, i});
    }
  }

  FindNextAliveKey();
}

void DBIterator::Seek(const std::string &target_key) {
  while (!min_heap_.empty()) {
    min_heap_.pop();
  }

  std::string seek_target = target_key;
  if (!start_key_.empty() && seek_target < start_key_) {
    seek_target = start_key_;
  }

  if (!end_key_.empty() && seek_target > end_key_) {
    valid_ = false;
    current_key_.clear();
    current_value_.clear();
    return;
  }

  for (size_t i = 0; i < sources_.size(); ++i) {
    sources_[i].Seek(seek_target);
    if (sources_[i].Valid()) {
      min_heap_.push(HeapNode{sources_[i].Key(), sources_[i].generation_id, i});
    }
  }

  FindNextAliveKey();
}

bool DBIterator::Valid() const {
  return valid_;
}

void DBIterator::Next() {
  if (!valid_) {
    return;
  }
  FindNextAliveKey();
}

std::string DBIterator::Key() const {
  return current_key_;
}

std::string DBIterator::Value() const {
  return current_value_;
}

void DBIterator::FindNextAliveKey() {
  while (!min_heap_.empty()) {
    HeapNode top_node = min_heap_.top();
    min_heap_.pop();

    size_t winner_idx = top_node.source_idx;
    auto &winner_source = sources_[winner_idx];

    std::string candidate_key = top_node.key;
    std::string candidate_value = winner_source.Value();
    bool is_deleted = winner_source.IsDeleted();

    // Advance winning iterator
    winner_source.Next();
    if (winner_source.Valid()) {
      min_heap_.push(HeapNode{winner_source.Key(), winner_source.generation_id, winner_idx});
    }

    // Drain duplicate older iterators matching candidate_key
    while (!min_heap_.empty() && min_heap_.top().key == candidate_key) {
      HeapNode dup_node = min_heap_.top();
      min_heap_.pop();

      auto &dup_source = sources_[dup_node.source_idx];
      dup_source.Next();
      if (dup_source.Valid()) {
        min_heap_.push(HeapNode{dup_source.Key(), dup_source.generation_id, dup_node.source_idx});
      }
    }

    // Check end_key boundary
    if (!end_key_.empty() && candidate_key > end_key_) {
      valid_ = false;
      current_key_.clear();
      current_value_.clear();
      return;
    }

    // If winner is NOT deleted, found next live record!
    if (!is_deleted) {
      current_key_ = std::move(candidate_key);
      current_value_ = std::move(candidate_value);
      valid_ = true;
      return;
    }
  }

  valid_ = false;
  current_key_.clear();
  current_value_.clear();
}

} // namespace lsm
