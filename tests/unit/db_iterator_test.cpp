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

// Test 5: StorageEngine::Scan range bounds, limits, tombstones, and multi-SSTable overrides
void test_storage_engine_scan() {
  const std::string db_dir = "test_engine_scan_dir";
  const std::string wal_path = db_dir + "/wal.log";
  std::error_code ec;
  std::filesystem::remove_all(db_dir, ec);
  std::filesystem::create_directories(db_dir, ec);

  {
    StorageEngine engine(4096, wal_path, db_dir);

    // Level 1: SSTable 1
    assert(engine.Put("k01", "v01_old"));
    assert(engine.Put("k02", "v02_old"));
    assert(engine.Put("k03", "v03_old"));
    assert(engine.Put("k04", "v04_old"));
    engine.FlushMemTable();

    // Level 2: SSTable 2
    assert(engine.Put("k02", "v02_updated"));
    assert(engine.Put("k03", "v03_to_delete"));
    assert(engine.Put("k05", "v05_sst2"));
    engine.FlushMemTable();

    // Active MemTable
    assert(engine.Delete("k03")); // Tombstone over k03
    assert(engine.Put("k06", "v06_mem"));
    assert(engine.Put("k07", "v07_mem"));

    // 1. Full scan (all live keys)
    auto full_res = engine.Scan("", "");
    assert(full_res.size() == 6);
    assert(full_res[0].first == "k01" && full_res[0].second == "v01_old");
    assert(full_res[1].first == "k02" && full_res[1].second == "v02_updated");
    assert(full_res[2].first == "k04" && full_res[2].second == "v04_old");
    assert(full_res[3].first == "k05" && full_res[3].second == "v05_sst2");
    assert(full_res[4].first == "k06" && full_res[4].second == "v06_mem");
    assert(full_res[5].first == "k07" && full_res[5].second == "v07_mem");

    // 2. Sub-range scan ["k02", "k05"]
    auto sub_res = engine.Scan("k02", "k05");
    assert(sub_res.size() == 3);
    assert(sub_res[0].first == "k02" && sub_res[0].second == "v02_updated");
    assert(sub_res[1].first == "k04" && sub_res[1].second == "v04_old");
    assert(sub_res[2].first == "k05" && sub_res[2].second == "v05_sst2");

    // 3. Sub-range scan with limit = 2
    auto limit_res = engine.Scan("k01", "k07", 2);
    assert(limit_res.size() == 2);
    assert(limit_res[0].first == "k01" && limit_res[0].second == "v01_old");
    assert(limit_res[1].first == "k02" && limit_res[1].second == "v02_updated");

    // 4. Non-existent range (out of bounds)
    auto empty_res = engine.Scan("k90", "k99");
    assert(empty_res.empty());

    // 5. Inverted range (start > end)
    auto inv_res = engine.Scan("k05", "k02");
    assert(inv_res.empty());
  }

  std::filesystem::remove_all(db_dir, ec);
}

// Test 6: StorageEngine::PrefixScan prefix filtering, multi-version overrides, limits, and tombstones
void test_storage_engine_prefix_scan() {
  const std::string db_dir = "test_engine_prefix_dir";
  const std::string wal_path = db_dir + "/wal.log";
  std::error_code ec;
  std::filesystem::remove_all(db_dir, ec);
  std::filesystem::create_directories(db_dir, ec);

  {
    StorageEngine engine(4096, wal_path, db_dir);

    // Populate keys with various prefixes
    // SSTable 1:
    assert(engine.Put("user:100", "Alice"));
    assert(engine.Put("user:101", "Bob_old"));
    assert(engine.Put("user:102", "Charlie"));
    assert(engine.Put("order:500", "Order_1"));
    engine.FlushMemTable();

    // SSTable 2:
    assert(engine.Put("user:101", "Bob_new")); // Updated
    assert(engine.Put("user:102", "Charlie_del"));
    assert(engine.Put("product:001", "Widget"));
    engine.FlushMemTable();

    // MemTable:
    assert(engine.Delete("user:102")); // Deleted
    assert(engine.Put("user:103", "David"));
    assert(engine.Put("user:104", "Eve"));
    assert(engine.Put("order:501", "Order_2"));

    // 1. Prefix scan "user:" (unlimited)
    // Expected: user:100, user:101 (Bob_new), user:103, user:104. (user:102 suppressed)
    auto user_res = engine.PrefixScan("user:");
    assert(user_res.size() == 4);
    assert(user_res[0].first == "user:100" && user_res[0].second == "Alice");
    assert(user_res[1].first == "user:101" && user_res[1].second == "Bob_new");
    assert(user_res[2].first == "user:103" && user_res[2].second == "David");
    assert(user_res[3].first == "user:104" && user_res[3].second == "Eve");

    // 2. Prefix scan "user:" with limit = 2
    auto user_lim_res = engine.PrefixScan("user:", 2);
    assert(user_lim_res.size() == 2);
    assert(user_lim_res[0].first == "user:100" && user_lim_res[0].second == "Alice");
    assert(user_lim_res[1].first == "user:101" && user_lim_res[1].second == "Bob_new");

    // 3. Prefix scan "order:"
    auto order_res = engine.PrefixScan("order:");
    assert(order_res.size() == 2);
    assert(order_res[0].first == "order:500" && order_res[0].second == "Order_1");
    assert(order_res[1].first == "order:501" && order_res[1].second == "Order_2");

    // 4. Prefix scan "product:"
    auto prod_res = engine.PrefixScan("product:");
    assert(prod_res.size() == 1);
    assert(prod_res[0].first == "product:001" && prod_res[0].second == "Widget");

    // 5. Non-existent prefix before existing keys ("account:")
    auto non_exist_res1 = engine.PrefixScan("account:");
    assert(non_exist_res1.empty());

    // 6. Non-existent prefix after existing keys ("zebra:")
    auto non_exist_res2 = engine.PrefixScan("zebra:");
    assert(non_exist_res2.empty());
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
  run_test("StorageEngine Scan Range & Limit", test_storage_engine_scan);
  run_test("StorageEngine PrefixScan", test_storage_engine_prefix_scan);

  std::cout << "ALL DB_ITERATOR TESTS PASSED SUCCESSFULLY!" << std::endl;
  return 0;
}
