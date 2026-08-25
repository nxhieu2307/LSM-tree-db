#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace lsm {

class Manifest {
public:
  explicit Manifest(const std::string &manifest_path = "MANIFEST");
  ~Manifest() = default;

  // Disallow copy/move to maintain unique file access ownership
  Manifest(const Manifest &) = delete;
  Manifest &operator=(const Manifest &) = delete;
  Manifest(Manifest &&) = delete;
  Manifest &operator=(Manifest &&) = delete;

  // Appends newly flushed SSTable filename to MANIFEST file
  bool AddSSTable(const std::string &filename);
  bool AddSSTable(uint64_t file_id);

  // Atomically replaces a list of obsolete SSTable file IDs with the new compacted SSTable file ID
  bool ReplaceSSTables(const std::vector<uint64_t> &old_file_ids, uint64_t new_file_id);

  // Atomically replaces a list of obsolete SSTable filenames with the new compacted SSTable filename
  bool ReplaceSSTables(const std::vector<std::string> &old_files, const std::string &new_file);

  // Reads MANIFEST sequentially line-by-line and returns list of SSTable filenames
  // in chronological order (oldest to newest)
  std::vector<std::string> LoadSSTables() const;

  // State loader / recovery accessor
  std::vector<std::string> Recover() const;

  // Returns list of active SSTable file IDs in chronological order
  std::vector<uint64_t> LoadSSTableIDs() const;

  std::string GetManifestPath() const { return manifest_path_; }

private:
  void LoadInternal();
  static uint64_t ExtractFileId(const std::string &filename);

  std::string manifest_path_;
  std::vector<std::string> sstable_files_;
  mutable std::mutex mutex_;
};

} // namespace lsm
