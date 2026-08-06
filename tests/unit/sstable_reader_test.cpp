#include "../../src/core/sstable_builder.hpp"
#include "../../src/core/sstable_reader.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace lsm;

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

void remove_file_if_exists(const std::string &filename) {
  std::remove(filename.c_str());
}

void test_sstable_reader_point_lookups() {
  const std::string filename = "sstable_reader_test.sst";
  remove_file_if_exists(filename);

  const int TOTAL_ENTRIES = 30;

  // Step 1: Write 30 sorted keys using SSTableBuilder
  {
    SSTableBuilder builder(filename);
    for (int i = 0; i < TOTAL_ENTRIES; ++i) {
      std::string key = "key_" + (i < 10 ? "0" + std::to_string(i) : std::to_string(i));
      std::string value = "value_" + std::to_string(i * 100);
      bool is_deleted = (i == 5 || i == 20); // Tombstones at key_05 and key_20
      assert(builder.Add(key, value, is_deleted));
    }
    assert(builder.Finish());
  }

  // Step 2: Initialize SSTableReader and test point lookups
  {
    SSTableReader reader(filename);
    assert(reader.footer().magic_number == kSSTableMagicNumber);
    assert(reader.footer().entry_count == static_cast<uint64_t>(TOTAL_ENTRIES));

    // Test active existing keys
    for (int i = 0; i < TOTAL_ENTRIES; ++i) {
      if (i == 5 || i == 20) continue; // Skip tombstones

      std::string key = "key_" + (i < 10 ? "0" + std::to_string(i) : std::to_string(i));
      std::string expected_value = "value_" + std::to_string(i * 100);

      std::string val;
      bool is_deleted = false;
      bool found = reader.Get(key, &val, &is_deleted);

      assert(found == true);
      assert(is_deleted == false);
      assert(val == expected_value);
    }

    // Test tombstone deletion entries (key_05, key_20)
    {
      std::string val;
      bool is_deleted = false;
      bool found = reader.Get("key_05", &val, &is_deleted);
      assert(found == true);
      assert(is_deleted == true);
    }

    {
      std::string val;
      bool is_deleted = false;
      bool found = reader.Get("key_20", &val, &is_deleted);
      assert(found == true);
      assert(is_deleted == true);
    }

    // Test non-existent keys
    {
      std::string val;
      bool is_deleted = false;
      assert(reader.Get("key_000", &val, &is_deleted) == false);
      assert(reader.Get("key_99", &val, &is_deleted) == false);
      assert(reader.Get("aaa", &val, &is_deleted) == false);
      assert(reader.Get("zzz", &val, &is_deleted) == false);
      assert(reader.Get("key_04_missing", &val, &is_deleted) == false);
    }
  }

  // Step 3: Test invalid file exception handling
  {
    bool threw_exception = false;
    try {
      SSTableReader reader("non_existent_file.sst");
    } catch (const std::runtime_error &) {
      threw_exception = true;
    }
    assert(threw_exception == true);
  }

  remove_file_if_exists(filename);
}

int main() {
  std::cout << "Starting SSTableReader unit tests..." << std::endl;

  run_test("SSTableReader Point Lookups & Tombstones", test_sstable_reader_point_lookups);

  std::cout << "All SSTableReader tests passed successfully!" << std::endl;
  return 0;
}
