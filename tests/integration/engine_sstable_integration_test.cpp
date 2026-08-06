#include "../../src/core/engine.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

using namespace lsm;

void run_test(const std::string &test_name, void (*test_func)()) {
  std::cout << "[RUNNING INTEGRATION] " << test_name << "..." << std::endl;
  try {
    test_func();
    std::cout << "[ PASSED INTEGRATION] " << test_name << "\n" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[ FAILED INTEGRATION] " << test_name << ": " << e.what()
              << std::endl;
    std::exit(1);
  } catch (...) {
    std::cerr << "[ FAILED INTEGRATION] " << test_name << ": Unknown error"
              << std::endl;
    std::exit(1);
  }
}

void cleanup_files() {
  std::remove("engine_test.wal");
  for (int i = 0; i < 20; ++i) {
    std::string sst_name = "data_" + std::to_string(i) + ".sst";
    std::remove(sst_name.c_str());
  }
}

// Test 1: Multiple flushes triggered by low write buffer capacity
void test_engine_sstable_flushing_and_retrieval() {
  cleanup_files();

  const std::string wal_path = "engine_test.wal";
  const size_t low_capacity = 100; // 100 bytes forces frequent flushes

  {
    StorageEngine engine(low_capacity, wal_path, ".");

    // Write enough entries to force multiple SSTable flushes
    for (int i = 1; i <= 15; ++i) {
      std::string key = "key_" + (i < 10 ? "0" + std::to_string(i) : std::to_string(i));
      std::string val = "value_" + std::to_string(i * 100);
      assert(engine.Put(key, val));
    }

    // Verify multiple SSTable flushes occurred
    assert(engine.sstable_count() >= 2);

    // Verify key retrieval from flushed disk files
    std::string val;
    bool is_deleted = false;

    assert(engine.Get("key_01", &val, &is_deleted));
    assert(!is_deleted && val == "value_100");

    assert(engine.Get("key_08", &val, &is_deleted));
    assert(!is_deleted && val == "value_800");

    assert(engine.Get("key_15", &val, &is_deleted));
    assert(!is_deleted && val == "value_1500");

    // Non-existent key lookup
    assert(!engine.Get("key_99", &val, &is_deleted));
  }

  cleanup_files();
}

// Test 2: Deletion tombstone masking older SSTable versions
void test_engine_tombstone_masking() {
  cleanup_files();

  const std::string wal_path = "engine_test.wal";
  const size_t low_capacity = 100;

  {
    StorageEngine engine(low_capacity, wal_path, ".");

    // Insert keys that will be flushed to an older SSTable (e.g. data_0.sst)
    assert(engine.Put("user_100", "Alice"));
    assert(engine.Put("user_200", "Bob"));
    engine.FlushMemTable();

    // Verify user_100 is accessible from SSTable
    std::string val;
    bool is_deleted = false;
    assert(engine.Get("user_100", &val, &is_deleted));
    assert(!is_deleted && val == "Alice");

    // Perform deletion on user_100 (tombstone written to active MemTable)
    assert(engine.Delete("user_100"));

    // Verify tombstone masks the older SSTable record from active RAM
    assert(engine.Get("user_100", &val, &is_deleted));
    assert(is_deleted);

    // Force flush so tombstone moves to a newer SSTable (e.g. data_1.sst)
    engine.FlushMemTable();

    // Verify tombstone in newer SSTable masks older SSTable version
    assert(engine.Get("user_100", &val, &is_deleted));
    assert(is_deleted);

    // Verify user_200 is still valid and un-deleted
    assert(engine.Get("user_200", &val, &is_deleted));
    assert(!is_deleted && val == "Bob");
  }

  cleanup_files();
}

// Test 3: Updating existing keys across flushes
void test_engine_key_updates_across_flushes() {
  cleanup_files();

  const std::string wal_path = "engine_test.wal";
  const size_t low_capacity = 100;

  {
    StorageEngine engine(low_capacity, wal_path, ".");

    // Write initial version
    assert(engine.Put("counter", "1"));
    engine.FlushMemTable(); // Flushed to data_0.sst

    // Update counter
    assert(engine.Put("counter", "2"));
    engine.FlushMemTable(); // Flushed to data_1.sst

    // Update counter again in RAM
    assert(engine.Put("counter", "3"));

    // Latest value in RAM should override both disk SSTables
    std::string val;
    bool is_deleted = false;
    assert(engine.Get("counter", &val, &is_deleted));
    assert(!is_deleted && val == "3");

    // Flush RAM
    engine.FlushMemTable(); // Flushed to data_2.sst

    // Latest SSTable should override older SSTables
    assert(engine.Get("counter", &val, &is_deleted));
    assert(!is_deleted && val == "3");
  }

  cleanup_files();
}

int main() {
  std::cout << "=================================================="
            << std::endl;
  std::cout << "  RUNNING STORAGE ENGINE SSTABLE INTEGRATION TESTS"
            << std::endl;
  std::cout << "=================================================="
            << std::endl;

  run_test("StorageEngine SSTable Flushing & Retrieval",
           test_engine_sstable_flushing_and_retrieval);
  run_test("StorageEngine Tombstone Masking Across SSTables",
           test_engine_tombstone_masking);
  run_test("StorageEngine Key Updates Across Flushes",
           test_engine_key_updates_across_flushes);

  std::cout << "ALL STORAGE ENGINE SSTABLE INTEGRATION TESTS PASSED!"
            << std::endl;
  return 0;
}
