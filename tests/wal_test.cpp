#include "../src/core/wal.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

std::string escape_string_for_print(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else if (c == '\b')
      out += "\\b";
    else if (c == '\f')
      out += "\\f";
    else if (c == '\\')
      out += "\\\\";
    else if (c == '"')
      out += "\\\"";
    else if (static_cast<unsigned char>(c) < 32) {
      char buf[10];
      std::snprintf(buf, sizeof(buf), "\\u%02x", static_cast<unsigned char>(c));
      out += buf;
    } else {
      out += c;
    }
  }
  return out;
}

void print_entry(const lsm::LogEntry &entry) {
  std::cout << "    Entry -> Op: \"" << escape_string_for_print(entry.operation)
            << "\", Key: \"" << escape_string_for_print(entry.key)
            << "\", Value: \"" << escape_string_for_print(entry.value)
            << "\", Timestamp: " << entry.timestamp << std::endl;
}

void run_test(const std::string &test_name, void (*test_func)()) {
  std::cout << "[RUNNING] " << test_name << "..." << std::endl;
  try {
    test_func();
    std::cout << "[ PASSED] " << test_name << "\n" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[ FAILED] " << test_name << ": " << e.what() << std::endl;
    std::exit(1);
  } catch (...) {
    std::cerr << "[ FAILED] " << test_name << ": Unknown error" << std::endl;
    std::exit(1);
  }
}

// Test that we can append entries and recover them correctly
void test_basic_append_recover() {
  const std::string filename = "test_basic_append_recover.wal";
  std::cout << "  1. Cleaning up and opening WAL file: " << filename
            << std::endl;
  std::remove(filename.c_str()); // Clean up any pre-existing file

  {
    lsm::WAL wal(filename);
    std::cout << "  2. Appending entries:" << std::endl;
    std::cout << "    - Appending (PUT, key1, value1)" << std::endl;
    assert(wal.Append("PUT", "key1", "value1"));
    std::cout << "    - Appending (DELETE, key2, )" << std::endl;
    assert(wal.Append("DELETE", "key2", ""));
    std::cout << "    - Appending (PUT, key3, value3_longer_string_to_check)"
              << std::endl;
    assert(wal.Append("PUT", "key3", "value3_longer_string_to_check"));
  }

  // Recover in a new instance
  {
    std::cout << "  3. Closing WAL and opening new instance for recovery..."
              << std::endl;
    lsm::WAL wal(filename);
    std::vector<lsm::LogEntry> entries;
    assert(wal.Recover(entries));

    std::cout << "  4. Recovered " << entries.size()
              << " entries:" << std::endl;
    for (const auto &entry : entries) {
      print_entry(entry);
    }

    assert(entries.size() == 3);

    assert(entries[0].operation == "PUT");
    assert(entries[0].key == "key1");
    assert(entries[0].value == "value1");
    assert(entries[0].timestamp > 0);

    assert(entries[1].operation == "DELETE");
    assert(entries[1].key == "key2");
    assert(entries[1].value == "");
    assert(entries[1].timestamp >= entries[0].timestamp);

    assert(entries[2].operation == "PUT");
    assert(entries[2].key == "key3");
    assert(entries[2].value == "value3_longer_string_to_check");
    assert(entries[2].timestamp >= entries[1].timestamp);
    std::cout << "  5. All recovered entries verified successfully."
              << std::endl;
  }

  std::cout << "  6. Cleaning up WAL file." << std::endl;
  std::remove(filename.c_str()); // Clean up
}

// Test JSON escaping and unescaping logic for complex characters
void test_json_escaping() {
  const std::string filename = "test_json_escaping.wal";
  std::cout << "  1. Cleaning up and opening WAL file: " << filename
            << std::endl;
  std::remove(filename.c_str());

  // Test values that contain quotes, slashes, backslashes, newlines, tabs, etc.
  std::string complex_key = "key\\with\\backslashes\\and\"quotes\"";
  std::string complex_value =
      "value\nwith\nnewlines\tand\ttricky\bbackspaces\f\r";

  std::cout << "  2. Appending entry with complex characters:" << std::endl;
  std::cout << "    - Key:   \"" << escape_string_for_print(complex_key) << "\""
            << std::endl;
  std::cout << "    - Value: \"" << escape_string_for_print(complex_value)
            << "\"" << std::endl;

  {
    lsm::WAL wal(filename);
    assert(wal.Append("PUT", complex_key, complex_value));
  }

  {
    std::cout << "  3. Closing WAL and opening new instance for recovery..."
              << std::endl;
    lsm::WAL wal(filename);
    std::vector<lsm::LogEntry> entries;
    assert(wal.Recover(entries));

    std::cout << "  4. Recovered " << entries.size()
              << " entries:" << std::endl;
    for (const auto &entry : entries) {
      print_entry(entry);
    }

    assert(entries.size() == 1);
    assert(entries[0].operation == "PUT");
    assert(entries[0].key == complex_key);
    assert(entries[0].value == complex_value);
    std::cout << "  5. Escaped/Unescaped characters verified successfully."
              << std::endl;
  }

  std::cout << "  6. Cleaning up WAL file." << std::endl;
  std::remove(filename.c_str());
}

// Test behavior with a non-existent file
void test_nonexistent_file() {
  const std::string filename = "non_existent_file.wal";
  std::cout << "  1. Cleaning up to ensure " << filename << " does not exist."
            << std::endl;
  std::remove(filename.c_str());

  std::cout << "  2. Instantiating WAL and attempting recovery..." << std::endl;
  lsm::WAL wal(filename);
  std::vector<lsm::LogEntry> entries;
  // Recovering from a non-existent file should return true with empty entries
  // list
  assert(wal.Recover(entries));
  std::cout << "  3. Recover call returned true. Recovered entry count: "
            << entries.size() << std::endl;
  assert(entries.empty());

  std::cout << "  4. Cleaning up WAL file." << std::endl;
  std::remove(filename.c_str());
}

int main() {
  std::cout << "Starting WAL component tests..." << std::endl;

  run_test("Basic Append and Recover", test_basic_append_recover);
  run_test("JSON Escaping / Unescaping Verification", test_json_escaping);
  run_test("Nonexistent File Recovery", test_nonexistent_file);

  std::cout << "All WAL tests passed successfully!" << std::endl;
  return 0;
}
