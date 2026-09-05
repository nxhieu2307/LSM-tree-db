import os
import shutil
import unittest
import concurrent.futures
import pylsm


class TestPyLSM(unittest.TestCase):
    def setUp(self):
        self.test_dir = "test_pylsm_db"
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def tearDown(self):
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def test_basic_crud(self):
        db = pylsm.StorageEngine(self.test_dir, memtable_capacity=4096)
        
        # Test put and get
        self.assertTrue(db.put("user:100", "Alice"))
        self.assertTrue(db.put("user:101", "Bob"))
        self.assertEqual(db.get("user:100"), "Alice")
        self.assertEqual(db.get("user:101"), "Bob")
        
        # Test missing key returns None
        self.assertIsNone(db.get("user:999"))
        
        # Test update
        self.assertTrue(db.put("user:100", "Alice_Updated"))
        self.assertEqual(db.get("user:100"), "Alice_Updated")
        
        # Test delete
        self.assertTrue(db.delete("user:100"))
        self.assertIsNone(db.get("user:100"))
        
        db.close()

    def test_context_manager(self):
        with pylsm.StorageEngine(self.test_dir) as db:
            db.put("k1", "v1")
            self.assertEqual(db.get("k1"), "v1")
        
        # Reopen to verify persistence
        with pylsm.StorageEngine(self.test_dir) as db:
            self.assertEqual(db.get("k1"), "v1")

    def test_range_scan(self):
        with pylsm.StorageEngine(self.test_dir, memtable_capacity=100) as db:
            # Insert keys that will cause memtable flushes
            for i in range(10, 30):
                db.put(f"key_{i}", f"val_{i}")
            
            # Full scan
            all_items = db.scan()
            self.assertEqual(len(all_items), 20)
            self.assertEqual(all_items[0], ("key_10", "val_10"))
            self.assertEqual(all_items[-1], ("key_29", "val_29"))
            
            # Bounded range scan
            bounded = db.scan("key_12", "key_16")
            self.assertEqual(len(bounded), 5)
            self.assertEqual([k for k, _ in bounded], ["key_12", "key_13", "key_14", "key_15", "key_16"])
            
            # Scan with limit
            limited = db.scan("key_10", "key_25", limit=3)
            self.assertEqual(len(limited), 3)
            self.assertEqual([k for k, _ in limited], ["key_10", "key_11", "key_12"])

    def test_prefix_scan_chat_history(self):
        with pylsm.StorageEngine(self.test_dir) as db:
            db.put("chat:session_1:001", "User: Hello")
            db.put("chat:session_1:002", "Agent: Hi! How can I help?")
            db.put("chat:session_1:003", "User: What is LSM-tree?")
            db.put("chat:session_2:001", "User: Good morning")
            db.put("memory:user_profile:name", "Alice")
            
            # Query session 1 history
            sess1_msgs = db.prefix_scan("chat:session_1:")
            self.assertEqual(len(sess1_msgs), 3)
            self.assertEqual(sess1_msgs[0], ("chat:session_1:001", "User: Hello"))
            self.assertEqual(sess1_msgs[1], ("chat:session_1:002", "Agent: Hi! How can I help?"))
            self.assertEqual(sess1_msgs[2], ("chat:session_1:003", "User: What is LSM-tree?"))
            
            # Query session 1 with limit
            limited_msgs = db.prefix_scan("chat:session_1:", limit=2)
            self.assertEqual(len(limited_msgs), 2)
            self.assertEqual(limited_msgs[0][0], "chat:session_1:001")
            self.assertEqual(limited_msgs[1][0], "chat:session_1:002")
            
            # Query session 2
            sess2_msgs = db.prefix_scan("chat:session_2:")
            self.assertEqual(len(sess2_msgs), 1)
            self.assertEqual(sess2_msgs[0], ("chat:session_2:001", "User: Good morning"))
            
            # Query non-existent prefix
            empty_msgs = db.prefix_scan("chat:session_99:")
            self.assertEqual(len(empty_msgs), 0)

    def test_multithreaded_gil_release(self):
        with pylsm.StorageEngine(self.test_dir) as db:
            def worker_write(start_idx, count):
                for i in range(start_idx, start_idx + count):
                    db.put(f"thread_key_{i:05d}", f"thread_val_{i:05d}")
                return count

            def worker_read(start_idx, count):
                found = 0
                for i in range(start_idx, start_idx + count):
                    v = db.get(f"thread_key_{i:05d}")
                    if v is not None:
                        found += 1
                return found

            # Run concurrent writes with thread pool
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
                futures = [
                    executor.submit(worker_write, 0, 100),
                    executor.submit(worker_write, 100, 100),
                    executor.submit(worker_write, 200, 100),
                    executor.submit(worker_write, 300, 100),
                ]
                results = [f.result() for f in futures]
                self.assertEqual(sum(results), 400)

            # Run concurrent reads
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
                futures = [
                    executor.submit(worker_read, 0, 100),
                    executor.submit(worker_read, 100, 100),
                    executor.submit(worker_read, 200, 100),
                    executor.submit(worker_read, 300, 100),
                ]
                read_counts = [f.result() for f in futures]
                self.assertEqual(sum(read_counts), 400)


if __name__ == "__main__":
    unittest.main(verbosity=2)
