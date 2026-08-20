#include "compactor.hpp"
#include "sstable_builder.hpp"
#include "sstable_iterator.hpp"
#include <memory>
#include <queue>
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
  (void)purge_tombstones;

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
    if (input.iterator && input.iterator->Valid()) {
      min_heap.push(HeapNode{
          input.iterator->Key(),
          input.iterator->Value(),
          input.iterator->IsDeleted(),
          input.file_id,
          i
      });
    }
  }

  // Compaction merge loop will be implemented in subsequent sub-steps
  return false;
}

} // namespace lsm
