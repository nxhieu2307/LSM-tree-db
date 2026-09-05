#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <optional>
#include "core/engine.hpp"

namespace py = pybind11;

PYBIND11_MODULE(pylsm, m) {
  m.doc() = "pylsm: High-performance C++17 LSM-Tree Key-Value Database & Local AI Memory Engine Python Bindings";

  py::class_<lsm::StorageEngine>(m, "StorageEngine")
      .def(py::init([](const std::string &db_dir, size_t memtable_capacity) {
        std::string wal_path = (db_dir.empty() || db_dir == ".") ? "wal.log" : db_dir + "/wal.log";
        return std::make_unique<lsm::StorageEngine>(memtable_capacity, wal_path, db_dir);
      }),
      py::arg("db_dir") = ".",
      py::arg("memtable_capacity") = static_cast<size_t>(4 * 1024 * 1024))
      .def("put", [](lsm::StorageEngine &self, const std::string &key, const std::string &value) {
        py::gil_scoped_release release;
        return self.Put(key, value);
      }, py::arg("key"), py::arg("value"), "Insert or update a key-value entry.")
      .def("get", [](const lsm::StorageEngine &self, const std::string &key) -> std::optional<std::string> {
        std::string val;
        bool is_deleted = false;
        bool found = false;
        {
          py::gil_scoped_release release;
          found = self.Get(key, &val, &is_deleted);
        }
        if (found && !is_deleted) {
          return val;
        }
        return std::nullopt;
      }, py::arg("key"), "Retrieve the value for a key, or None if absent or deleted.")
      .def("delete", [](lsm::StorageEngine &self, const std::string &key) {
        py::gil_scoped_release release;
        return self.Delete(key);
      }, py::arg("key"), "Delete a key by writing a tombstone entry.")
      .def("scan", [](const lsm::StorageEngine &self, const std::string &start_key, const std::string &end_key, size_t limit) {
        py::gil_scoped_release release;
        return self.Scan(start_key, end_key, limit);
      }, py::arg("start_key") = "", py::arg("end_key") = "", py::arg("limit") = 0,
         "Scan a lexicographical key range [start_key, end_key] up to limit records.")
      .def("prefix_scan", [](const lsm::StorageEngine &self, const std::string &prefix, size_t limit) {
        py::gil_scoped_release release;
        return self.PrefixScan(prefix, limit);
      }, py::arg("prefix"), py::arg("limit") = 0,
         "Scan all key-value entries matching the given prefix up to limit records.")
      .def("close", [](lsm::StorageEngine &self) {
        py::gil_scoped_release release;
        self.Close();
      }, "Explicitly close the database and flush active memtable entries to disk.")
      .def("__enter__", [](py::object self) {
        return self;
      })
      .def("__exit__", [](lsm::StorageEngine &self, py::object, py::object, py::object) {
        py::gil_scoped_release release;
        self.Close();
      });
}
