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

void cleanup_recovery_files() {
  std::remove("recovery_test.wal");
  std::remove("MANIFEST");
  for (int i = 0; i < 20; ++i) {
    std::string sst_name = "data_" + std::to_string(i) + ".sst";
    std::remove(sst_name.c_str());
  }
}

// Test 1: Full crash recovery of flushed SSTables (from MANIFEST) and un-flushed writes (from WAL)
void test_engine_startup_recovery() {
  cleanup_recovery_files();

  const std::string wal_path = "recovery_test.wal";
  const std::string db_dir = ".";
  const size_t write_buffer_capacity = 200;

  // Session 1: Write entries, force flush to SSTable, write un-flushed entries, then "crash"
  {
    StorageEngine engine(write_buffer_capacity, wal_path, db_dir);

    // Flushed entries
    assert(engine.Put("flushed_key_1", "flushed_val_100"));
    assert(engine.Put("flushed_key_2", "flushed_val_200"));
    engine.FlushMemTable(); // Flushed to data_0.sst, recorded in MANIFEST

    // Un-flushed entries in active MemTable / WAL
    assert(engine.Put("unflushed_key_3", "unflushed_val_300"));
    assert(engine.Delete("flushed_key_2")); // Tombstone for flushed_key_2 in WAL

    // Simulate process crash: engine goes out of scope without explicit shutdown
  }

  // Session 2: Recovery after crash
  {
    StorageEngine recovered_engine(write_buffer_capacity, wal_path, db_dir);

    // Assert SSTable recovery from MANIFEST
    assert(recovered_engine.sstable_count() >= 1);

    std::string val;
    bool is_deleted = false;

    // 1. Verify flushed key 1 recovered from SSTable
    assert(recovered_engine.Get("flushed_key_1", &val, &is_deleted));
    assert(!is_deleted && val == "flushed_val_100");

    // 2. Verify un-flushed key 3 recovered from WAL
    assert(recovered_engine.Get("unflushed_key_3", &val, &is_deleted));
    assert(!is_deleted && val == "unflushed_val_300");

    // 3. Verify un-flushed tombstone for flushed key 2 recovered from WAL masks SSTable value
    assert(recovered_engine.Get("flushed_key_2", &val, &is_deleted));
    assert(is_deleted);

    // 4. Verify non-existent key returns false
    assert(!recovered_engine.Get("unknown_key", &val, &is_deleted));
  }

  cleanup_recovery_files();
}

// Test 2: Multi-session recovery across multiple SSTable flushes and restart sessions
void test_engine_multisession_recovery() {
  cleanup_recovery_files();

  const std::string wal_path = "recovery_test.wal";
  const std::string db_dir = ".";
  const size_t write_buffer_capacity = 200;

  // Session 1: Write & flush data_0.sst
  {
    StorageEngine s1(write_buffer_capacity, wal_path, db_dir);
    s1.Put("s1_key1", "s1_val1");
    s1.FlushMemTable();
  }

  // Session 2: Recover s1 data, write & flush data_1.sst, write to WAL
  {
    StorageEngine s2(write_buffer_capacity, wal_path, db_dir);
    assert(s2.sstable_count() == 1);

    std::string val;
    assert(s2.Get("s1_key1", &val) && val == "s1_val1");

    s2.Put("s2_key2", "s2_val2");
    s2.FlushMemTable(); // data_1.sst created

    s2.Put("s2_key3_unflushed", "s2_val3");
  }

  // Session 3: Recover both SSTables and un-flushed WAL entry
  {
    StorageEngine s3(write_buffer_capacity, wal_path, db_dir);
    assert(s3.sstable_count() == 2);

    std::string val;
    bool is_deleted = false;

    assert(s3.Get("s1_key1", &val, &is_deleted) && !is_deleted && val == "s1_val1");
    assert(s3.Get("s2_key2", &val, &is_deleted) && !is_deleted && val == "s2_val2");
    assert(s3.Get("s2_key3_unflushed", &val, &is_deleted) && !is_deleted && val == "s2_val3");
  }

  cleanup_recovery_files();
}

int main() {
  std::cout << "=================================================="
            << std::endl;
  std::cout << "  RUNNING STORAGE ENGINE RECOVERY INTEGRATION TESTS"
            << std::endl;
  std::cout << "=================================================="
            << std::endl;

  run_test("StorageEngine Startup Recovery (SSTables & WAL)",
           test_engine_startup_recovery);
  run_test("StorageEngine Multi-session Recovery",
           test_engine_multisession_recovery);

  std::cout << "ALL STORAGE ENGINE RECOVERY INTEGRATION TESTS PASSED!"
            << std::endl;
  return 0;
}
