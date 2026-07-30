#pragma once

#include <cstddef>
#include <cstdint>

namespace lsm {

// Binary file constants
constexpr uint32_t kSSTableMagicNumber = 0x4C534D00; // "LSM\0"
constexpr size_t kSparseIndexInterval = 16;          // Index entry recorded every 16 entries

// Packed struct for the SSTable footer written at the end of the file.
#pragma pack(push, 1)
struct SSTableFooter {
  uint64_t index_offset{0};
  uint64_t index_size{0};
  uint64_t entry_count{0};
  uint32_t magic_number{kSSTableMagicNumber};
};
#pragma pack(pop)

static_assert(sizeof(SSTableFooter) == 28, "SSTableFooter size must be exactly 28 bytes");

} // namespace lsm
