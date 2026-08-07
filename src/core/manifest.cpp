#include "manifest.hpp"
#include <fstream>

namespace lsm {

Manifest::Manifest(const std::string &manifest_path)
    : manifest_path_(manifest_path) {}

bool Manifest::AddSSTable(const std::string &filename) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ofstream out(manifest_path_, std::ios::out | std::ios::app);
  if (!out.is_open()) {
    return false;
  }

  out << filename << "\n";
  out.flush();
  return !out.fail();
}

std::vector<std::string> Manifest::LoadSSTables() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<std::string> filenames;
  std::ifstream in(manifest_path_);
  if (!in.is_open()) {
    return filenames;
  }

  std::string line;
  while (std::getline(in, line)) {
    // Strip trailing carriage return if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      filenames.push_back(line);
    }
  }

  return filenames;
}

} // namespace lsm
