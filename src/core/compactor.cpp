#include "compactor.hpp"
#include "sstable_builder.hpp"
#include <memory>
#include <queue>
#include <vector>

namespace lsm {

namespace {

struct HeapItem {
  std::string key;
  uint64_t file_id; // Generation/sequence ID (higher = newer)
  size_t input_idx; // Index in inputs vector

  // Comparison for min-heap with std::greater<HeapItem>:
  // - Primary key: lexicographical key ascending.
  // - Tie-breaker: larger file_id (newer version) comes first.
  bool operator>(const HeapItem &other) const {
    if (key != other.key) {
      return key > other.key;
    }
    return file_id < other.file_id;
  }
};

} // anonymous namespace

bool Compactor::Compact(const std::vector<CompactorInput> &inputs,
                        const std::string &output_path,
                        uint32_t /*block_size*/,
                        bool purge_tombstones) {
  if (inputs.empty() || output_path.empty()) {
    return false;
  }

  std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<HeapItem>> min_heap;

  for (size_t i = 0; i < inputs.size(); ++i) {
    const auto &input = inputs[i];
    if (input.iterator && input.iterator->Valid()) {
      min_heap.push(HeapItem{input.iterator->Key(), input.file_id, i});
    }
  }

  SSTableBuilder builder(output_path);

  while (!min_heap.empty()) {
    std::string current_key = min_heap.top().key;
    std::string newest_value;
    bool is_deleted = false;
    bool first = true;

    // Collect and advance all iterators that match current_key
    while (!min_heap.empty() && min_heap.top().key == current_key) {
      HeapItem item = min_heap.top();
      min_heap.pop();

      auto &iter = inputs[item.input_idx].iterator;

      if (first) {
        newest_value = iter->Value();
        is_deleted = iter->IsDeleted();
        first = false;
      }

      iter->Next();
      if (iter->Valid()) {
        min_heap.push(HeapItem{iter->Key(), item.file_id, item.input_idx});
      }
    }

    // Discard tombstone if purge_tombstones is enabled
    if (is_deleted && purge_tombstones) {
      continue;
    }

    if (!builder.Add(current_key, newest_value, is_deleted)) {
      return false;
    }
  }

  return builder.Finish();
}

} // namespace lsm
