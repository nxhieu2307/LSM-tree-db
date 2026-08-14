#include "../../src/core/sstable_builder.hpp"
#include "../../src/core/sstable_iterator.hpp"
#include "../../src/core/sstable_reader.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

void test_sstable_iterator_sequential() {
  const std::string filename = "sstable_iterator_test.sst";
  remove_file_if_exists(filename);

  const int TOTAL_ENTRIES = 15;

  // Step 1: Write 15 sorted entries (key_00 to key_14) using SSTableBuilder
  {
    SSTableBuilder builder(filename);
    for (int i = 0; i < TOTAL_ENTRIES; ++i) {
      std::string key = "key_" + (i < 10 ? "0" + std::to_string(i) : std::to_string(i));
      std::string value = "val_" + std::to_string(i * 100);
      bool is_deleted = (i == 3 || i == 7 || i == 11); // Set tombstones for key_03, key_07, key_11
      assert(builder.Add(key, value, is_deleted));
    }
    assert(builder.Finish());
  }

  // Step 2: Initialize SSTableIterator, call SeekToFirst(), and iterate sequentially
  {
    SSTableIterator it(filename);
    it.SeekToFirst();

    int count = 0;
    std::string previous_key = "";

    while (it.Valid()) {
      std::string current_key = it.Key();
      std::string current_val = it.Value();
      bool current_deleted = it.IsDeleted();

      // Assert lexicographical sorting
      if (!previous_key.empty()) {
        assert(current_key > previous_key);
      }
      previous_key = current_key;

      std::string expected_key = "key_" + (count < 10 ? "0" + std::to_string(count) : std::to_string(count));
      std::string expected_val = "val_" + std::to_string(count * 100);
      bool expected_deleted = (count == 3 || count == 7 || count == 11);

      assert(current_key == expected_key);
      assert(current_val == expected_val);
      assert(current_deleted == expected_deleted);

      count++;
      it.Next();
    }

    assert(count == TOTAL_ENTRIES);
    assert(!it.Valid());

    // Calling Next() when invalid should remain invalid
    it.Next();
    assert(!it.Valid());

    // Test SeekToFirst() rewinding iterator back to start
    it.SeekToFirst();
    assert(it.Valid());
    assert(it.Key() == "key_00");
    assert(it.Value() == "val_0");
    assert(!it.IsDeleted());
  }

  // Step 3: Test initializing iterator from SSTableReader
  {
    SSTableReader reader(filename);
    SSTableIterator it(reader);
    it.SeekToFirst();

    int count = 0;
    while (it.Valid()) {
      std::string expected_key = "key_" + (count < 10 ? "0" + std::to_string(count) : std::to_string(count));
      assert(it.Key() == expected_key);
      count++;
      it.Next();
    }
    assert(count == TOTAL_ENTRIES);
  }

  // Step 4: Test exception on invalid file path
  {
    bool threw_exception = false;
    try {
      SSTableIterator invalid_it("non_existent_file_123.sst");
    } catch (const std::runtime_error &) {
      threw_exception = true;
    }
    assert(threw_exception);
  }

  remove_file_if_exists(filename);
}

int main() {
  std::cout << "Starting SSTableIterator unit tests..." << std::endl;

  run_test("SSTableIterator Sequential Iteration", test_sstable_iterator_sequential);

  std::cout << "All SSTableIterator tests passed successfully!" << std::endl;
  return 0;
}
