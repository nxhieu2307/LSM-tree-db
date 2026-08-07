#include "engine.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>

namespace {

// Trim leading and trailing whitespace from string
std::string Trim(const std::string &str) {
  size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return "";
  size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, (last - first + 1));
}

// Convert string to uppercase
std::string ToUpper(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return str;
}

} // namespace

int main() {
  std::cout << "=================================================="
            << std::endl;
  std::cout << "  LSM-Tree Key-Value Database CLI REPL Shell      "
            << std::endl;
  std::cout << "=================================================="
            << std::endl;
  std::cout << "Commands available:" << std::endl;
  std::cout << "  PUT <key> <val>  : Insert or update a key-value pair"
            << std::endl;
  std::cout << "  GET <key>        : Retrieve value for a given key"
            << std::endl;
  std::cout << "  DEL <key>        : Delete a key (tombstone write)"
            << std::endl;
  std::cout << "  EXIT             : Exit the database CLI shell" << std::endl;
  std::cout << "=================================================="
            << std::endl;

  // Initialize StorageEngine coordinator (loads MANIFEST and replays WAL)
  lsm::StorageEngine db;

  std::string line;
  while (true) {
    std::cout << "lsm-db> ";
    if (!std::getline(std::cin, line)) {
      std::cout << "\nBye!" << std::endl;
      break;
    }

    std::string trimmed = Trim(line);
    if (trimmed.empty()) {
      continue;
    }

    std::stringstream ss(trimmed);
    std::string cmd;
    ss >> cmd;
    std::string upper_cmd = ToUpper(cmd);

    if (upper_cmd == "EXIT" || upper_cmd == "QUIT") {
      std::cout << "Bye!" << std::endl;
      break;
    } else if (upper_cmd == "PUT") {
      std::string key;
      ss >> key;
      if (key.empty()) {
        std::cout << "(error) Usage: PUT <key> <val>" << std::endl;
        continue;
      }

      std::string val;
      std::getline(ss, val);
      val = Trim(val);
      if (val.empty()) {
        std::cout << "(error) Usage: PUT <key> <val>" << std::endl;
        continue;
      }

      if (db.Put(key, val)) {
        std::cout << "OK" << std::endl;
      } else {
        std::cout << "(error) Failed to execute PUT operation" << std::endl;
      }
    } else if (upper_cmd == "GET") {
      std::string key;
      ss >> key;
      if (key.empty()) {
        std::cout << "(error) Usage: GET <key>" << std::endl;
        continue;
      }

      std::string val;
      bool is_deleted = false;
      if (db.Get(key, &val, &is_deleted)) {
        if (!is_deleted) {
          std::cout << "\"" << val << "\"" << std::endl;
        } else {
          std::cout << "(error) Key marked as deleted" << std::endl;
        }
      } else {
        std::cout << "(nil)" << std::endl;
      }
    } else if (upper_cmd == "DEL" || upper_cmd == "DELETE") {
      std::string key;
      ss >> key;
      if (key.empty()) {
        std::cout << "(error) Usage: DEL <key>" << std::endl;
        continue;
      }

      if (db.Delete(key)) {
        std::cout << "OK" << std::endl;
      } else {
        std::cout << "(error) Failed to execute DEL operation" << std::endl;
      }
    } else {
      std::cout << "(error) Unknown command: " << cmd << std::endl;
    }
  }

  return 0;
}
