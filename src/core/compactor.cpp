#include "compactor.hpp"
#include "sstable_builder.hpp"
#include "sstable_iterator.hpp"
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace lsm {

namespace {

struct HeapNode {
  std::string key;
  std::string value;
  bool is_deleted{false};
  uint64_t file_id{0};
  size_t input_idx{0};

  // Comparator for std::priority_queue with std::greater<HeapNode> (Min-Heap):
  // - Primary: ascending key order (smallest key pops first).
  // - Secondary (Tie-breaker for identical keys): larger file_id pops first (newest SSTable).
  bool operator>(const HeapNode &other) const {
    if (key != other.key) {
      return key > other.key;
    }
    return file_id < other.file_id;
  }
};

} // anonymous namespace

bool Compactor::Compact(const std::vector<CompactorInput> &inputs,
                        const std::string &output_path,
                        uint32_t block_size,
                        bool purge_tombstones) {
  (void)block_size;

  if (inputs.empty()) {
    return true;
  }

  if (output_path.empty()) {
    return false;
  }

  // Priority queue (min-heap) ordered by HeapNode comparator
  std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> min_heap;

  for (size_t i = 0; i < inputs.size(); ++i) {
    const auto &input = inputs[i];
    if (!input.iterator) {
      continue;
    }

    input.iterator->SeekToFirst();
    if (input.iterator->Valid()) {
      min_heap.push(HeapNode{
          input.iterator->Key(),
          input.iterator->Value(),
          input.iterator->IsDeleted(),
          input.file_id,
          i
      });
    }
  }

  SSTableBuilder builder(output_path);

  while (!min_heap.empty()) {
    HeapNode winner = min_heap.top();
    min_heap.pop();

    // Advance the winner's underlying iterator
    auto &winner_iter = inputs[winner.input_idx].iterator;
    winner_iter->Next();
    if (winner_iter->Valid()) {
      min_heap.push(HeapNode{
          winner_iter->Key(),
          winner_iter->Value(),
          winner_iter->IsDeleted(),
          winner.file_id,
          winner.input_idx
      });
    }

    // Drain and advance all duplicate iterators matching the winner's key
    while (!min_heap.empty() && min_heap.top().key == winner.key) {
      HeapNode duplicate = min_heap.top();
      min_heap.pop();

      auto &dup_iter = inputs[duplicate.input_idx].iterator;
      dup_iter->Next();
      if (dup_iter->Valid()) {
        min_heap.push(HeapNode{
            dup_iter->Key(),
            dup_iter->Value(),
            dup_iter->IsDeleted(),
            duplicate.file_id,
            duplicate.input_idx
        });
      }
    }

    // Emit entry to output SSTable
    if (winner.is_deleted) {
      if (!purge_tombstones) {
        if (!builder.Add(winner.key, "", /*is_deleted=*/true)) {
          return false;
        }
      }
    } else {
      if (!builder.Add(winner.key, winner.value, /*is_deleted=*/false)) {
        return false;
      }
    }
  }

  return builder.Finish();
}

} // namespace lsm
