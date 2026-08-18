#pragma once

#include "sstable_iterator.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lsm {

struct CompactorInput {
  uint64_t file_id;
  std::shared_ptr<SSTableIterator> iterator;
};

class Compactor {
public:
  // Performs a k-way merge of sorted SSTableIterator streams into a single compacted SSTable.
  // inputs: vector of CompactorInput structs containing file_id (generation) and iterator.
  // output_path: destination file path for the compacted SSTable.
  // block_size: SSTable block size (default: 4096).
  // purge_tombstones: if true, deleted records (tombstones) are omitted from the output.
  // Returns true on success, false on failure.
  static bool Compact(
      const std::vector<CompactorInput> &inputs,
      const std::string &output_path,
      uint32_t block_size = 4096,
      bool purge_tombstones = true
  );
};

} // namespace lsm
