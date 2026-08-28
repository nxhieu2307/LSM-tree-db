#include "../../src/core/engine.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace lsm;

void run_test(const std::string &test_name, void (*test_func)()) {
  std::cout << "[RUNNING INTEGRATION] " << test_name << "..." << std::endl;
  try {
    test_func();
    std::cout << "[ PASSED INTEGRATION] " << test_name << "\n" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[ FAILED INTEGRATION] " << test_name << ": " << e.what() << std::endl;
    std::exit(1);
  } catch (...) {
    std::cerr << "[ FAILED INTEGRATION] " << test_name << ": Unknown error" << std::endl;
    std::exit(1);
  }
}

void cleanup_test_dir(const std::string &dir) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
}

// Test 1: Auto-compaction trigger on reaching threshold (4 SSTables -> 1 compacted SSTable)
void test_auto_compaction_trigger() {
  const std::string db_dir = "test_compaction_auto_dir";
  const std::string wal_path = db_dir + "/wal.log";
  cleanup_test_dir(db_dir);

  {
    StorageEngine engine(4096, wal_path, db_dir, 4);

    // Ingest and flush Batch 1
    assert(engine.Put("key_01", "val_01"));
    assert(engine.Put("key_02", "val_02"));
    engine.FlushMemTable(); // Creates data_0.sst (count = 1)
    assert(engine.sstable_count() == 1);

    // Ingest and flush Batch 2
    assert(engine.Put("key_03", "val_03"));
    assert(engine.Put("key_04", "val_04"));
    engine.FlushMemTable(); // Creates data_1.sst (count = 2)
    assert(engine.sstable_count() == 2);

    // Ingest and flush Batch 3
    assert(engine.Put("key_05", "val_05"));
    assert(engine.Put("key_06", "val_06"));
    engine.FlushMemTable(); // Creates data_2.sst (count = 3)
    assert(engine.sstable_count() == 3);

    // Ingest and flush Batch 4 -> Reaches threshold of 4 SSTables -> Triggers Auto-Compaction!
    assert(engine.Put("key_07", "val_07"));
    assert(engine.Put("key_08", "val_08"));
    engine.FlushMemTable(); // Creates data_3.sst -> Auto-compacts into sstable_4.sst (count = 1)

    // Verify in-memory reader collection dropped back to 1
    assert(engine.sstable_count() == 1);

    // Verify obsolete files data_0..3 are deleted from disk
    assert(!std::filesystem::exists(db_dir + "/data_0.sst"));
    assert(!std::filesystem::exists(db_dir + "/data_1.sst"));
    assert(!std::filesystem::exists(db_dir + "/data_2.sst"));
    assert(!std::filesystem::exists(db_dir + "/data_3.sst"));

    // Verify newly compacted SSTable exists
    assert(std::filesystem::exists(db_dir + "/sstable_4.sst"));

    // Verify all keys from all 4 flushed batches are intact
    for (int i = 1; i <= 8; ++i) {
      std::string k = "key_0" + std::to_string(i);
      std::string expected_v = "val_0" + std::to_string(i);
      std::string actual_v;
      bool is_deleted = false;
      assert(engine.Get(k, &actual_v, &is_deleted));
      assert(!is_deleted && actual_v == expected_v);
    }
  }

  cleanup_test_dir(db_dir);
  std::filesystem::remove_all(db_dir);
}

// Test 2: Data integrity, key updates, and tombstone purging across compaction
void test_compaction_data_integrity_and_tombstones() {
  const std::string db_dir = "test_compaction_integrity_dir";
  const std::string wal_path = db_dir + "/wal.log";
  cleanup_test_dir(db_dir);

  {
    StorageEngine engine(4096, wal_path, db_dir, 4);

    // Batch 1: Initial keys
    assert(engine.Put("k_update", "v_old"));
    assert(engine.Put("k_del", "v_del"));
    assert(engine.Put("k_keep", "v_keep"));
    engine.FlushMemTable();

    // Batch 2: Update k_update
    assert(engine.Put("k_update", "v_new"));
    engine.FlushMemTable();

    // Batch 3: Delete k_del
    assert(engine.Delete("k_del"));
    engine.FlushMemTable();

    // Batch 4: Add new key k_new -> Triggers auto-compaction (4 SSTables)
    assert(engine.Put("k_new", "v_new"));
    engine.FlushMemTable();

    assert(engine.sstable_count() == 1);

    std::string val;
    bool is_deleted = false;

    // Updated key must have latest value
    assert(engine.Get("k_update", &val, &is_deleted));
    assert(!is_deleted && val == "v_new");

    // Unmodified key must be preserved
    assert(engine.Get("k_keep", &val, &is_deleted));
    assert(!is_deleted && val == "v_keep");

    // New key must be accessible
    assert(engine.Get("k_new", &val, &is_deleted));
    assert(!is_deleted && val == "v_new");

    // Deleted key should be completely purged (Get returns false)
    assert(!engine.Get("k_del", &val, &is_deleted));
  }

  cleanup_test_dir(db_dir);
  std::filesystem::remove_all(db_dir);
}

// Test 3: Crash-consistency and recovery after compaction
void test_recovery_after_compaction() {
  const std::string db_dir = "test_compaction_recovery_dir";
  const std::string wal_path = db_dir + "/wal.log";
  cleanup_test_dir(db_dir);

  // Session 1: Run writes, trigger compaction, then shutdown
  {
    StorageEngine engine(4096, wal_path, db_dir, 4);

    for (int b = 0; b < 4; ++b) {
      assert(engine.Put("session1_k" + std::to_string(b), "session1_v" + std::to_string(b)));
      engine.FlushMemTable();
    }

    assert(engine.sstable_count() == 1);
  }

  // Session 2: Recover from disk and verify MANIFEST state
  {
    StorageEngine recovered_engine(4096, wal_path, db_dir, 4);

    // Must have recovered exactly 1 compacted SSTable from MANIFEST
    assert(recovered_engine.sstable_count() == 1);

    // Verify all keys are accessible
    for (int b = 0; b < 4; ++b) {
      std::string val;
      bool is_deleted = false;
      assert(recovered_engine.Get("session1_k" + std::to_string(b), &val, &is_deleted));
      assert(!is_deleted && val == "session1_v" + std::to_string(b));
    }

    // Verify new writes after recovery work seamlessly
    assert(recovered_engine.Put("session2_key", "session2_val"));
    std::string val;
    bool is_deleted = false;
    assert(recovered_engine.Get("session2_key", &val, &is_deleted));
    assert(!is_deleted && val == "session2_val");
  }

  cleanup_test_dir(db_dir);
  std::filesystem::remove_all(db_dir);
}

int main() {
  std::cout << "==================================================" << std::endl;
  std::cout << "  RUNNING ENGINE COMPACTION INTEGRATION TESTS     " << std::endl;
  std::cout << "==================================================" << std::endl;

  run_test("AutoCompactionTrigger", test_auto_compaction_trigger);
  run_test("DataIntegrityAndTombstones", test_compaction_data_integrity_and_tombstones);
  run_test("RecoveryAfterCompaction", test_recovery_after_compaction);

  std::cout << "ALL ENGINE COMPACTION INTEGRATION TESTS PASSED!" << std::endl;
  return 0;
}
