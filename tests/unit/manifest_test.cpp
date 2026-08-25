#include "../../src/core/manifest.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
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

void cleanup_manifest_files(const std::string &path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(path + ".tmp", ec);
}

void test_manifest_basic_add_and_recover() {
  const std::string manifest_path = "test_manifest_basic.txt";
  cleanup_manifest_files(manifest_path);

  {
    Manifest manifest(manifest_path);
    assert(manifest.AddSSTable("data_0.sst"));
    assert(manifest.AddSSTable("data_1.sst"));
    assert(manifest.AddSSTable(2)); // Adds "data_2.sst"

    auto files = manifest.LoadSSTables();
    assert(files.size() == 3);
    assert(files[0] == "data_0.sst");
    assert(files[1] == "data_1.sst");
    assert(files[2] == "data_2.sst");

    auto ids = manifest.LoadSSTableIDs();
    assert(ids.size() == 3);
    assert(ids[0] == 0);
    assert(ids[1] == 1);
    assert(ids[2] == 2);
  }

  // Verify recovery from disk
  {
    Manifest recovered(manifest_path);
    auto files = recovered.Recover();
    assert(files.size() == 3);
    assert(files[0] == "data_0.sst");
    assert(files[1] == "data_1.sst");
    assert(files[2] == "data_2.sst");
  }

  cleanup_manifest_files(manifest_path);
}

void test_manifest_atomic_replace_by_ids() {
  const std::string manifest_path = "test_manifest_replace_ids.txt";
  cleanup_manifest_files(manifest_path);

  {
    Manifest manifest(manifest_path);
    assert(manifest.AddSSTable("data_0.sst"));
    assert(manifest.AddSSTable("data_1.sst"));
    assert(manifest.AddSSTable("data_2.sst"));
    assert(manifest.AddSSTable("data_3.sst"));
    assert(manifest.AddSSTable("data_4.sst"));

    // Attempt replacing non-existent ID -> should fail
    assert(!manifest.ReplaceSSTables({0, 1, 99}, 10));

    // Replace {0, 1, 2} with new compacted SSTable 5
    assert(manifest.ReplaceSSTables({0, 1, 2}, 5));

    auto files = manifest.LoadSSTables();
    assert(files.size() == 3);
    assert(files[0] == "compacted_5.sst");
    assert(files[1] == "data_3.sst");
    assert(files[2] == "data_4.sst");

    auto ids = manifest.LoadSSTableIDs();
    assert(ids.size() == 3);
    assert(ids[0] == 5);
    assert(ids[1] == 3);
    assert(ids[2] == 4);
  }

  // Verify crash-recovery state on disk
  {
    Manifest recovered(manifest_path);
    auto files = recovered.Recover();
    assert(files.size() == 3);
    assert(files[0] == "compacted_5.sst");
    assert(files[1] == "data_3.sst");
    assert(files[2] == "data_4.sst");
  }

  cleanup_manifest_files(manifest_path);
}

void test_manifest_atomic_replace_by_filenames() {
  const std::string manifest_path = "test_manifest_replace_names.txt";
  cleanup_manifest_files(manifest_path);

  {
    Manifest manifest(manifest_path);
    assert(manifest.AddSSTable("dir/data_10.sst"));
    assert(manifest.AddSSTable("dir/data_11.sst"));
    assert(manifest.AddSSTable("dir/data_12.sst"));

    // Replace {dir/data_10.sst, dir/data_11.sst} with "dir/compacted_20.sst"
    assert(manifest.ReplaceSSTables({"dir/data_10.sst", "dir/data_11.sst"}, "dir/compacted_20.sst"));

    auto files = manifest.LoadSSTables();
    assert(files.size() == 2);
    assert(files[0] == "dir/compacted_20.sst");
    assert(files[1] == "dir/data_12.sst");
  }

  {
    Manifest recovered(manifest_path);
    auto files = recovered.LoadSSTables();
    assert(files.size() == 2);
    assert(files[0] == "dir/compacted_20.sst");
    assert(files[1] == "dir/data_12.sst");
  }

  cleanup_manifest_files(manifest_path);
}

int main() {
  std::cout << "=================================================="
            << std::endl;
  std::cout << "  RUNNING MANIFEST ATOMIC REPLACEMENT TESTS       "
            << std::endl;
  std::cout << "=================================================="
            << std::endl;

  run_test("Manifest Basic Add and Recovery", test_manifest_basic_add_and_recover);
  run_test("Manifest Atomic Replace By IDs", test_manifest_atomic_replace_by_ids);
  run_test("Manifest Atomic Replace By Filenames", test_manifest_atomic_replace_by_filenames);

  std::cout << "ALL MANIFEST TESTS PASSED SUCCESSFULLY!"
            << std::endl;
  return 0;
}
