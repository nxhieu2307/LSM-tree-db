#include "wal.hpp"
#include <cctype>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace lsm {

namespace {

// Parses a string field from a JSON-formatted line, handling potential escaped
// quotes. Returns true and populates value_out if the field is found and parsed
// successfully.
bool ParseStringField(const std::string &line, const std::string &field_name,
                      std::string &value_out) {
  std::string target = "\"" + field_name + "\"";
  size_t pos = line.find(target);
  if (pos == std::string::npos)
    return false;

  // Find the colon after target
  pos = line.find(':', pos + target.length());
  if (pos == std::string::npos)
    return false;

  // Find the starting quote
  pos = line.find('"', pos);
  if (pos == std::string::npos)
    return false;

  size_t start = pos + 1;
  size_t end = start;
  // Scan for the ending quote, respecting escaped quotes
  while (end < line.length()) {
    if (line[end] == '"') {
      size_t slashes = 0;
      size_t p = end;
      while (p > start && line[p - 1] == '\\') {
        slashes++;
        p--;
      }
      if (slashes % 2 == 0) {
        break;
      }
    }
    end++;
  }

  if (end >= line.length())
    return false;
  value_out = line.substr(start, end - start);
  return true;
}

// Parses an integer field from a JSON-formatted line.
// Returns true and populates value_out if the field is found and parsed
// successfully.
bool ParseIntegerField(const std::string &line, const std::string &field_name,
                       int64_t &value_out) {
  std::string target = "\"" + field_name + "\"";
  size_t pos = line.find(target);
  if (pos == std::string::npos)
    return false;

  pos = line.find(':', pos + target.length());
  if (pos == std::string::npos)
    return false;

  // Skip spaces
  pos++;
  while (pos < line.length() && (line[pos] == ' ' || line[pos] == '\t')) {
    pos++;
  }

  if (pos >= line.length())
    return false;

  size_t end = pos;
  if (line[end] == '-')
    end++;
  while (end < line.length() && std::isdigit(static_cast<unsigned char>(line[end]))) {
    end++;
  }

  if (end == pos)
    return false;
  try {
    value_out = std::stoll(line.substr(pos, end - pos));
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace

// Use a member initializer list to initialize filename_ and fd_ instead of
// assignment with the operator= in the constructor body, which is more
// efficient and conforms to C++ best practices.
WAL::WAL(const std::string &filename) : filename_(filename), fd_(-1) {
  fd_ = open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd_ < 0) {
    throw std::runtime_error("Failed to open WAL file for writing: " +
                             filename + " (" + strerror(errno) + ")");
  }
}

WAL::~WAL() {
  if (fd_ >= 0) {
    close(fd_);
  }
}

bool WAL::Append(const std::string &operation, const std::string &key,
                 const std::string &value) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  int64_t timestamp =
      std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

  // Format log entry as a JSON line
  std::string json_line =
      "{\"operation\":\"" + EscapeJSON(operation) + "\",\"key\":\"" +
      EscapeJSON(key) + "\",\"value\":\"" + EscapeJSON(value) +
      "\",\"timestamp\":" + std::to_string(timestamp) + "}\n";

  size_t total_written = 0;
  const char *ptr = json_line.data();
  size_t len = json_line.size();
  while (total_written < len) {
    ssize_t n = write(fd_, ptr + total_written, len - total_written);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    total_written += n;
  }

  // Ensure data reaches persistent storage (synchronous writes)
  if (fsync(fd_) != 0) {
    return false;
  }

  return true;
}

bool WAL::Recover(std::vector<LogEntry> &entries) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ifstream file(filename_);
  if (!file.is_open()) {
    struct stat st;
    if (stat(filename_.c_str(), &st) != 0) {
      // File does not exist, which is fine
      return true;
    }
    return false;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty())
      continue;
    LogEntry entry;
    if (ParseLogEntry(line, entry)) {
      entries.push_back(entry);
    } else {
      // Encountered a corrupted/truncated trailing record from ungraceful crash.
      // Stop replaying at the corruption point, preserving all preceding valid entries.
      break;
    }
  }
  return true;
}

std::string WAL::EscapeJSON(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 32) {
        char buf[10];
        snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += c;
      }
    }
  }
  return out;
}

std::string WAL::UnescapeJSON(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.length(); ++i) {
    if (s[i] == '\\' && i + 1 < s.length()) {
      char next = s[++i];
      switch (next) {
      case '"':
        out += '"';
        break;
      case '\\':
        out += '\\';
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u': {
        if (i + 4 < s.length()) {
          std::string hex = s.substr(i + 1, 4);
          i += 4;
          try {
            int code = std::stoi(hex, nullptr, 16);
            if (code < 128) {
              out += static_cast<char>(code);
            } else {
              out += "?"; // Fallback for non-ASCII
            }
          } catch (...) {
            out += "\\u" + hex;
          }
        } else {
          out += "\\u";
        }
        break;
      }
      default:
        out += next;
      }
    } else {
      out += s[i];
    }
  }
  return out;
}

bool WAL::ParseLogEntry(const std::string &line, LogEntry &entry) {
  std::string op, key, val;
  int64_t ts;
  if (!ParseStringField(line, "operation", op))
    return false;
  if (!ParseStringField(line, "key", key))
    return false;
  if (!ParseStringField(line, "value", val))
    return false;
  if (!ParseIntegerField(line, "timestamp", ts))
    return false;

  entry.operation = UnescapeJSON(op);
  entry.key = UnescapeJSON(key);
  entry.value = UnescapeJSON(val);
  entry.timestamp = ts;
  return true;
}

} // namespace lsm
