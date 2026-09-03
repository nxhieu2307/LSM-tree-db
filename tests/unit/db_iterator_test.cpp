#include "../../src/core/db_iterator.hpp"
#include "../../src/core/engine.hpp"
#include "../../src/core/memtable.hpp"
#include "../../src/core/sstable_builder.hpp"
#include "../../src/core/sstable_iterator.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
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

void cleanup_files(const std::vector<std::string> &files) {
  for (const auto &f : files) {
    std::error_code ec;
    std::filesystem::remove(f, ec);
  }
}

// Test 1: Basic unified iteration over MemTable and 2 SSTables with deduplication & tombstone purging
void test_db_iterator_unified_merge_and_tombstones() {
  std::string sst1_path = "test_db_iter_1.sst"; // file_id = 1 (oldest)
  std::string sst2_path = "test_db_iter_2.sst"; // file_id = 2 (middle)
  cleanup_files({sst1_path, sst2_path});

  // SSTable 1 (oldest, file_id = 1):
  // "apple" -> "v_apple_old"
  // "banana" -> "v_banana_old"
  // "cherry" -> "v_cherry_old"
  // "date" -> "v_date_old"
  {
    SSTableBuilder b1(sst1_path);
    assert(b1.Add("apple", "v_apple_old"));
    assert(b1.Add("banana", "v_banana_old"));
    assert(b1.Add("cherry", "v_cherry_old"));
    assert(b1.Add("date", "v_date_old"));
    assert(b1.Finish());
  }

  // SSTable 2 (middle, file_id = 2):
  // "banana" -> "v_banana_v2" (updated)
  // "date" -> "" (deleted in sst2)
  // "elderberry" -> "v_elderberry"
  {
    SSTableBuilder b2(sst2_path);
    assert(b2.Add("banana", "v_banana_v2"));
    assert(b2.Add("date", "", /*is_deleted=*/true));
    assert(b2.Add("elderberry", "v_elderberry"));
    assert(b2.Finish());
  }

  // Active MemTable (highest priority):
  // "apple" -> deleted in MemTable
  // "cherry" -> "v_cherry_mem" (updated in MemTable)
  // "fig" -> "v_fig" (new key in MemTable)
  auto memtable = std::make_shared<MemTable>();
  assert(memtable->Delete("apple"));
  assert(memtable->Put("cherry", "v_cherry_mem"));
  assert(memtable->Put("fig", "v_fig"));

  auto sst_it1 = std::make_shared<SSTableIterator>(sst1_path);
  auto sst_it2 = std::make_shared<SSTableIterator>(sst2_path);

  std::vector<CompactorInput> sst_inputs = {
      {1, sst_it1},
      {2, sst_it2}
  };

  DBIterator iter(memtable->NewIterator(), sst_inputs);

  // Expected live items in sorted order:
  // "banana" -> "v_banana_v2" (from sst2)
  // "cherry" -> "v_cherry_mem" (from memtable)
  // "elderberry" -> "v_elderberry" (from sst2)
  // "fig" -> "v_fig" (from memtable)
  // ("apple" deleted in MemTable -> suppressed)
  // ("date" deleted in sst2 -> suppressed)

  std::vector<std::pair<std::string, std::string>> results;
  for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
    results.emplace_back(iter.Key(), iter.Value());
  }

  assert(results.size() == 4);
  assert(results[0].first == "banana" && results[0].second == "v_banana_v2");
  assert(results[1].first == "cherry" && results[1].second == "v_cherry_mem");
  assert(results[2].first == "elderberry" && results[2].second == "v_elderberry");
  assert(results[3].first == "fig" && results[3].second == "v_fig");

  cleanup_files({sst1_path, sst2_path});
}

// Test 2: Range bounds (start_key and end_key)
void test_db_iterator_range_bounds() {
  auto memtable = std::make_shared<MemTable>();
  assert(memtable->Put("a", "1"));
  assert(memtable->Put("b", "2"));
  assert(memtable->Put("c", "3"));
  assert(memtable->Put("d", "4"));
  assert(memtable->Put("e", "5"));

  // Range ["b", "d"]
  DBIterator iter(memtable->NewIterator(), {}, "b", "d");

  std::vector<std::pair<std::string, std::string>> results;
  for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
    results.emplace_back(iter.Key(), iter.Value());
  }

  assert(results.size() == 3);
  assert(results[0].first == "b" && results[0].second == "2");
  assert(results[1].first == "c" && results[1].second == "3");
  assert(results[2].first == "d" && results[2].second == "4");
}

// Test 3: Seek positioning within target key
void test_db_iterator_seek() {
  auto memtable = std::make_shared<MemTable>();
  assert(memtable->Put("key_10", "v10"));
  assert(memtable->Put("key_20", "v20"));
  assert(memtable->Put("key_30", "v30"));
  assert(memtable->Put("key_40", "v40"));

  DBIterator iter(memtable->NewIterator(), {});

  // Seek to exact key
  iter.Seek("key_20");
  assert(iter.Valid());
  assert(iter.Key() == "key_20");
  assert(iter.Value() == "v20");

  // Seek to non-existent intermediate key ("key_25" -> should land on "key_30")
  iter.Seek("key_25");
  assert(iter.Valid());
  assert(iter.Key() == "key_30");
  assert(iter.Value() == "v30");

  // Seek beyond all keys
  iter.Seek("key_99");
  assert(!iter.Valid());
}

// Test 4: End-to-end integration via StorageEngine::NewIterator
void test_db_iterator_storage_engine_integration() {
  const std::string db_dir = "test_db_iter_engine_dir";
  const std::string wal_path = db_dir + "/wal.log";
  std::error_code ec;
  std::filesystem::remove_all(db_dir, ec);
  std::filesystem::create_directories(db_dir, ec);

  {
    StorageEngine engine(4096, wal_path, db_dir);

    // Flushed to SSTable 1
    assert(engine.Put("item_1", "v1_sstable"));
    assert(engine.Put("item_2", "v2_sstable"));
    engine.FlushMemTable();

    // In active MemTable
    assert(engine.Put("item_2", "v2_mem_override")); // override item_2
    assert(engine.Put("item_3", "v3_mem"));
    assert(engine.Put("item_4", "v4_mem"));
    assert(engine.Delete("item_1")); // delete item_1 in MemTable

    auto it = engine.NewIterator();
    assert(it != nullptr);

    std::vector<std::pair<std::string, std::string>> entries;
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
      entries.emplace_back(it->Key(), it->Value());
    }

    // Expected: item_1 is deleted; item_2 is overridden; item_3 and item_4 are present
    assert(entries.size() == 3);
    assert(entries[0].first == "item_2" && entries[0].second == "v2_mem_override");
    assert(entries[1].first == "item_3" && entries[1].second == "v3_mem");
    assert(entries[2].first == "item_4" && entries[2].second == "v4_mem");
  }

  std::filesystem::remove_all(db_dir, ec);
}

int main() {
  std::cout << "==================================================" << std::endl;
  std::cout << "  RUNNING DB_ITERATOR UNIT & INTEGRATION TESTS    " << std::endl;
  std::cout << "==================================================" << std::endl;

  run_test("DBIterator Unified Merge & Tombstones", test_db_iterator_unified_merge_and_tombstones);
  run_test("DBIterator Range Bounds", test_db_iterator_range_bounds);
  run_test("DBIterator Seek", test_db_iterator_seek);
  run_test("DBIterator StorageEngine Integration", test_db_iterator_storage_engine_integration);

  std::cout << "ALL DB_ITERATOR TESTS PASSED SUCCESSFULLY!" << std::endl;
  return 0;
}
