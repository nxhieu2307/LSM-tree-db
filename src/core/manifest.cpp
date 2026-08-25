#include "manifest.hpp"
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace lsm {

Manifest::Manifest(const std::string &manifest_path)
    : manifest_path_(manifest_path) {
  LoadInternal();
}

void Manifest::LoadInternal() {
  sstable_files_.clear();
  std::ifstream in(manifest_path_);
  if (!in.is_open()) {
    return;
  }

  std::string line;
  while (std::getline(in, line)) {
    // Strip trailing carriage return if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      sstable_files_.push_back(line);
    }
  }
}

uint64_t Manifest::ExtractFileId(const std::string &filename) {
  size_t last_slash = filename.find_last_of("/\\");
  std::string base = (last_slash == std::string::npos) ? filename : filename.substr(last_slash + 1);

  size_t pos = base.rfind("data_");
  size_t prefix_len = 5;
  if (pos == std::string::npos) {
    pos = base.rfind("compacted_");
    prefix_len = 10;
  }
  if (pos != std::string::npos) {
    size_t start = pos + prefix_len;
    size_t end = base.find(".sst", start);
    if (end != std::string::npos) {
      try {
        return std::stoull(base.substr(start, end - start));
      } catch (...) {
      }
    }
  }
  try {
    return std::stoull(base);
  } catch (...) {
  }
  return 0;
}

bool Manifest::AddSSTable(const std::string &filename) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ofstream out(manifest_path_, std::ios::out | std::ios::app);
  if (!out.is_open()) {
    return false;
  }

  out << filename << "\n";
  out.flush();
  if (out.fail()) {
    return false;
  }

  sstable_files_.push_back(filename);
  return true;
}

bool Manifest::AddSSTable(uint64_t file_id) {
  return AddSSTable("data_" + std::to_string(file_id) + ".sst");
}

bool Manifest::ReplaceSSTables(const std::vector<uint64_t> &old_file_ids,
                               uint64_t new_file_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (old_file_ids.empty()) {
    return false;
  }

  // Map existing files to their IDs
  std::unordered_set<uint64_t> active_id_set;
  std::string dir_prefix;
  for (const auto &f : sstable_files_) {
    active_id_set.insert(ExtractFileId(f));
    if (dir_prefix.empty()) {
      size_t last_slash = f.find_last_of("/\\");
      if (last_slash != std::string::npos) {
        dir_prefix = f.substr(0, last_slash + 1);
      }
    }
  }

  // Validate that all old_file_ids exist in active tracking set
  for (uint64_t old_id : old_file_ids) {
    if (active_id_set.find(old_id) == active_id_set.end()) {
      return false;
    }
  }

  std::unordered_set<uint64_t> old_id_set(old_file_ids.begin(), old_file_ids.end());
  std::vector<std::string> updated_files;
  bool inserted_new = false;
  std::string new_filename = dir_prefix + "compacted_" + std::to_string(new_file_id) + ".sst";

  for (const auto &f : sstable_files_) {
    uint64_t fid = ExtractFileId(f);
    if (old_id_set.find(fid) != old_id_set.end()) {
      if (!inserted_new) {
        updated_files.push_back(new_filename);
        inserted_new = true;
      }
    } else {
      updated_files.push_back(f);
    }
  }

  if (!inserted_new) {
    updated_files.push_back(new_filename);
  }

  // Atomically persist to temporary manifest file and rename
  std::string tmp_manifest = manifest_path_ + ".tmp";
  {
    std::ofstream out(tmp_manifest, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }
    for (const auto &f : updated_files) {
      out << f << "\n";
    }
    out.flush();
    if (out.fail()) {
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::rename(tmp_manifest, manifest_path_, ec);
  if (ec) {
    std::filesystem::remove(manifest_path_, ec);
    std::filesystem::rename(tmp_manifest, manifest_path_, ec);
    if (ec) {
      return false;
    }
  }

  sstable_files_ = std::move(updated_files);
  return true;
}

bool Manifest::ReplaceSSTables(const std::vector<std::string> &old_files,
                               const std::string &new_file) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (old_files.empty()) {
    return false;
  }

  // Validate that all old_files exist in active tracking set
  std::unordered_set<std::string> active_set(sstable_files_.begin(), sstable_files_.end());
  for (const auto &old_f : old_files) {
    if (active_set.find(old_f) == active_set.end()) {
      return false;
    }
  }

  std::unordered_set<std::string> old_set(old_files.begin(), old_files.end());
  std::vector<std::string> updated_files;
  bool inserted_new = false;

  for (const auto &f : sstable_files_) {
    if (old_set.find(f) != old_set.end()) {
      if (!inserted_new && !new_file.empty()) {
        updated_files.push_back(new_file);
        inserted_new = true;
      }
    } else {
      updated_files.push_back(f);
    }
  }

  if (!inserted_new && !new_file.empty()) {
    updated_files.push_back(new_file);
  }

  // Atomically persist to temporary manifest file and rename
  std::string tmp_manifest = manifest_path_ + ".tmp";
  {
    std::ofstream out(tmp_manifest, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }
    for (const auto &f : updated_files) {
      out << f << "\n";
    }
    out.flush();
    if (out.fail()) {
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::rename(tmp_manifest, manifest_path_, ec);
  if (ec) {
    std::filesystem::remove(manifest_path_, ec);
    std::filesystem::rename(tmp_manifest, manifest_path_, ec);
    if (ec) {
      return false;
    }
  }

  sstable_files_ = std::move(updated_files);
  return true;
}

std::vector<std::string> Manifest::LoadSSTables() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sstable_files_;
}

std::vector<std::string> Manifest::Recover() const {
  return LoadSSTables();
}

std::vector<uint64_t> Manifest::LoadSSTableIDs() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<uint64_t> ids;
  ids.reserve(sstable_files_.size());
  for (const auto &f : sstable_files_) {
    ids.push_back(ExtractFileId(f));
  }
  return ids;
}

} // namespace lsm
