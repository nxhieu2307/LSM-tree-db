#include "../../src/core/sstable_builder.hpp"
#include "../../src/core/sstable_format.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
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

void test_sstable_builder_serialization() {
  const std::string filename = "test_sstable_builder.sst";
  remove_file_if_exists(filename);

  const int TOTAL_ENTRIES = 50;

  // Step 1: Write 50 entries using SSTableBuilder
  {
    SSTableBuilder builder(filename);
    for (int i = 0; i < TOTAL_ENTRIES; ++i) {
      std::string key = "key_" + (i < 10 ? "0" + std::to_string(i) : std::to_string(i));
      std::string value = "value_" + std::to_string(i * 10);
      bool is_deleted = (i % 5 == 0);
      assert(builder.Add(key, value, is_deleted));
    }
    assert(builder.Finish());
  }

  // Step 2: Read binary SSTable file and verify structure
  {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    assert(file.is_open());
    std::streamsize file_size = file.tellg();
    assert(file_size > static_cast<std::streamsize>(sizeof(SSTableFooter)));

    // Read Footer from last 28 bytes
    file.seekg(file_size - static_cast<std::streamsize>(sizeof(SSTableFooter)));
    SSTableFooter footer;
    file.read(reinterpret_cast<char *>(&footer), sizeof(footer));

    assert(footer.magic_number == kSSTableMagicNumber);
    assert(footer.entry_count == static_cast<uint64_t>(TOTAL_ENTRIES));
    assert(footer.index_offset < static_cast<uint64_t>(file_size));
    assert(footer.index_offset + footer.index_size + sizeof(SSTableFooter) ==
           static_cast<uint64_t>(file_size));

    // Read and verify Sparse Index block
    file.seekg(footer.index_offset);
    uint64_t index_bytes_read = 0;
    int index_count = 0;

    while (index_bytes_read < footer.index_size) {
      uint32_t key_len = 0;
      file.read(reinterpret_cast<char *>(&key_len), sizeof(key_len));
      std::string key(key_len, '\0');
      file.read(&key[0], key_len);
      uint64_t offset = 0;
      file.read(reinterpret_cast<char *>(&offset), sizeof(offset));

      index_bytes_read += sizeof(key_len) + key_len + sizeof(offset);

      // Verify index key matches entry at index_count * 16
      int expected_entry_idx = index_count * kSparseIndexInterval;
      std::string expected_key = "key_" + (expected_entry_idx < 10 ? "0" + std::to_string(expected_entry_idx) : std::to_string(expected_entry_idx));
      assert(key == expected_key);
      index_count++;
    }

    // Expected 4 index entries: for 0, 16, 32, 48
    assert(index_count == 4);

    // Read raw data entries sequentially from beginning
    file.seekg(0);
    for (int i = 0; i < TOTAL_ENTRIES; ++i) {
      uint32_t key_len = 0, val_len = 0;
      uint8_t del_byte = 0;

      file.read(reinterpret_cast<char *>(&key_len), sizeof(key_len));
      file.read(reinterpret_cast<char *>(&val_len), sizeof(val_len));

      std::string key(key_len, '\0');
      file.read(&key[0], key_len);

      std::string value(val_len, '\0');
      file.read(&value[0], val_len);

      file.read(reinterpret_cast<char *>(&del_byte), sizeof(del_byte));

      std::string expected_key = "key_" + (i < 10 ? "0" + std::to_string(i) : std::to_string(i));
      std::string expected_val = "value_" + std::to_string(i * 10);
      bool expected_deleted = (i % 5 == 0);

      assert(key == expected_key);
      assert(value == expected_val);
      assert((del_byte != 0) == expected_deleted);
    }
  }

  remove_file_if_exists(filename);
}

int main() {
  std::cout << "Starting SSTableBuilder component tests..." << std::endl;

  run_test("SSTableBuilder Binary Serialization", test_sstable_builder_serialization);

  std::cout << "All SSTableBuilder tests passed successfully!" << std::endl;
  return 0;
}
