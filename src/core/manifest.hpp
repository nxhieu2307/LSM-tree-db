#pragma once

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

  // Reads MANIFEST sequentially line-by-line and returns list of SSTable filenames
  // in chronological order (oldest to newest)
  std::vector<std::string> LoadSSTables() const;

  std::string GetManifestPath() const { return manifest_path_; }

private:
  std::string manifest_path_;
  mutable std::mutex mutex_;
};

} // namespace lsm
