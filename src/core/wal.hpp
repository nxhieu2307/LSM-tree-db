#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace lsm {

struct LogEntry {
  std::string operation; // "PUT" or "DELETE"
  std::string key;
  std::string value;
  int64_t timestamp;
};

class WAL {
public:
  // Opens or creates a WAL file.
  explicit WAL(const std::string &filename);
  ~WAL();

  // Disallow copy/move to avoid thread safety/file handle issues
  WAL(const WAL &) = delete;
  WAL &operator=(const WAL &) = delete;
  WAL(WAL &&) = delete;
  WAL &operator=(WAL &&) = delete;

  // Append an operation to the WAL
  // Returns true on success, false on failure
  bool Append(const std::string &operation, const std::string &key,
              const std::string &value);

  // Read and replay all log entries from the WAL file
  // Returns true on success, false on failure
  bool Recover(std::vector<LogEntry> &entries);

private:
  std::string filename_;
  int fd_;
  std::mutex mutex_;

  // Helper functions for JSON encoding/decoding without external dependencies
  static std::string EscapeJSON(const std::string &s);
  static std::string UnescapeJSON(const std::string &s);
  static bool ParseLogEntry(const std::string &line, LogEntry &entry);
};

} // namespace lsm
