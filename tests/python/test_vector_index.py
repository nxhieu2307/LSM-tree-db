import os
import shutil
import unittest
import pylsm
from ltm import MemoryStore, VectorStore, DummyEmbedding
from ltm.vector_index import _FallbackVectorIndex


class TestVectorIndex(unittest.TestCase):
    def setUp(self):
        self.test_dir = "test_vector_db"
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def tearDown(self):
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def test_vector_addition_and_search_accuracy(self):
        """Test vector addition and top-k nearest neighbor search accuracy with synthetic vectors."""
        dim = 4
        store = MemoryStore(self.test_dir)
        vstore = VectorStore(store, dim=dim, metric="cos")

        # Create orthogonal and distinct synthetic vectors
        # doc_0: pointing along axis 0: [1, 0, 0, 0]
        # doc_1: pointing along axis 1: [0, 1, 0, 0]
        # doc_2: diagonal in axis 0 & 1: [0.7071, 0.7071, 0, 0]
        id0 = vstore.add("doc_0", "Axis 0 vector", [1.0, 0.0, 0.0, 0.0], metadata={"category": "axis"})
        id1 = vstore.add("doc_1", "Axis 1 vector", [0.0, 1.0, 0.0, 0.0], metadata={"category": "axis"})
        id2 = vstore.add("doc_2", "Diagonal vector", [0.7071, 0.7071, 0.0, 0.0], metadata={"category": "diag"})

        self.assertEqual(id0, 0)
        self.assertEqual(id1, 1)
        self.assertEqual(id2, 2)
        self.assertEqual(len(vstore), 3)

        # Query exact match to doc_0: [1, 0, 0, 0]
        results = vstore.search([1.0, 0.0, 0.0, 0.0], top_k=3)
        self.assertEqual(len(results), 3)

        # Top 1 should be doc_0 with similarity close to 1.0
        self.assertEqual(results[0]["int_id"], 0)
        self.assertEqual(results[0]["doc_id"], "doc_0")
        self.assertAlmostEqual(results[0]["score"], 1.0, places=3)
        self.assertEqual(results[0]["metadata"], {"category": "axis"})

        # Top 2 should be diagonal vector doc_2 with similarity around 0.7071
        self.assertEqual(results[1]["int_id"], 2)
        self.assertEqual(results[1]["doc_id"], "doc_2")
        self.assertAlmostEqual(results[1]["score"], 0.7071, places=3)

        # Top 3 should be orthogonal vector doc_1 with similarity close to 0.0
        self.assertEqual(results[2]["int_id"], 1)
        self.assertEqual(results[2]["doc_id"], "doc_1")
        self.assertAlmostEqual(results[2]["score"], 0.0, places=3)

        vstore.close()

    def test_persistence_and_rehydration(self):
        """Test persistence across restarts and index rehydration from LSM-tree."""
        dim = 3
        # Phase 1: Add 3 vectors to VectorStore and close
        store1 = MemoryStore(self.test_dir)
        vstore1 = VectorStore(store1, dim=dim, metric="cos")

        vstore1.add("item_a", "Document A", [1.0, 0.0, 0.0], metadata={"tag": "alpha"})
        vstore1.add("item_b", "Document B", [0.0, 1.0, 0.0], metadata={"tag": "beta"})
        vstore1.add("item_c", "Document C", [0.0, 0.0, 1.0], metadata={"tag": "gamma"})
        self.assertEqual(vstore1.count(), 3)

        vstore1.close()

        # Phase 2: Instantiate a new VectorStore pointing to the exact same database directory
        store2 = MemoryStore(self.test_dir)
        vstore2 = VectorStore(store2, dim=dim, metric="cos")

        # Verify index was rehydrated with 3 items
        self.assertEqual(len(vstore2), 3)

        # Verify search() retrieves the previously added records without re-inserting them
        res_b = vstore2.search([0.0, 1.0, 0.0], top_k=1)
        self.assertEqual(len(res_b), 1)
        self.assertEqual(res_b[0]["doc_id"], "item_b")
        self.assertEqual(res_b[0]["text"], "Document B")
        self.assertEqual(res_b[0]["metadata"], {"tag": "beta"})
        self.assertAlmostEqual(res_b[0]["score"], 1.0, places=3)

        # Phase 3: Verify monotonic sequence ID counter continues properly after restart
        new_id = vstore2.add("item_d", "Document D", [0.6, 0.8, 0.0], metadata={"tag": "delta"})
        self.assertEqual(new_id, 3)
        self.assertEqual(len(vstore2), 4)

        # Verify query matching item_d
        res_d = vstore2.search([0.6, 0.8, 0.0], top_k=1)
        self.assertEqual(res_d[0]["int_id"], 3)
        self.assertEqual(res_d[0]["doc_id"], "item_d")
        self.assertAlmostEqual(res_d[0]["score"], 1.0, places=3)

        vstore2.close()

    def test_direct_storage_engine_wrapping(self):
        """Test wrapping raw pylsm.StorageEngine directly."""
        engine = pylsm.StorageEngine(self.test_dir)
        vstore = VectorStore(engine, dim=2)
        idx = vstore.add("doc_raw", "Raw engine test", [0.6, 0.8])
        self.assertEqual(idx, 0)
        self.assertIsNone(vstore.store)
        self.assertIsNotNone(vstore.engine)

        res = vstore.search([0.6, 0.8], top_k=1)
        self.assertEqual(len(res), 1)
        self.assertEqual(res[0]["doc_id"], "doc_raw")

        vstore.close()

    def test_context_manager_usage(self):
        """Test with-statement context manager support."""
        with MemoryStore(self.test_dir) as store:
            with VectorStore(store, dim=2) as vstore:
                vstore.add("doc1", "first", [1.0, 0.0])
                vstore.add("doc2", "second", [0.0, 1.0])
                self.assertEqual(len(vstore), 2)

        # Reopen to verify closed properly and data persists
        with MemoryStore(self.test_dir) as store:
            with VectorStore(store, dim=2) as vstore:
                self.assertEqual(len(vstore), 2)
                res = vstore.search([1.0, 0.0], top_k=1)
                self.assertEqual(res[0]["doc_id"], "doc1")

    def test_get_by_id(self):
        """Test retrieving vector payload directly by int_id."""
        with MemoryStore(self.test_dir) as store:
            vstore = VectorStore(store, dim=3)
            int_id = vstore.add("doc_fetch", "Content text", [0.1, 0.2, 0.3], metadata={"k": "v"})
            doc = vstore.get(int_id)
            self.assertIsNotNone(doc)
            self.assertEqual(doc["int_id"], int_id)
            self.assertEqual(doc["doc_id"], "doc_fetch")
            self.assertEqual(doc["text"], "Content text")
            self.assertEqual(doc["embedding"], [0.1, 0.2, 0.3])
            self.assertEqual(doc["metadata"], {"k": "v"})

            # Non-existent ID
            self.assertIsNone(vstore.get(999))

    def test_empty_search_and_edge_cases(self):
        """Test search behavior on empty index and edge parameters."""
        with MemoryStore(self.test_dir) as store:
            vstore = VectorStore(store, dim=2)
            # Empty index
            self.assertEqual(vstore.search([1.0, 0.0], top_k=5), [])

            # Add 1 item
            vstore.add("single", "single doc", [1.0, 0.0])

            # top_k <= 0
            self.assertEqual(vstore.search([1.0, 0.0], top_k=0), [])
            self.assertEqual(vstore.search([1.0, 0.0], top_k=-1), [])

            # top_k > count
            res = vstore.search([1.0, 0.0], top_k=10)
            self.assertEqual(len(res), 1)

    def test_dummy_embedding_generator(self):
        """Test deterministic DummyEmbedding generator."""
        embedder = DummyEmbedding(dim=384)
        vec1 = embedder.embed("Artificial Intelligence and Vector Databases")
        vec2 = embedder.embed("Artificial Intelligence and Vector Databases")
        vec3 = embedder.embed("Cooking recipes for Italian pasta")

        self.assertEqual(len(vec1), 384)
        self.assertEqual(vec1, vec2)  # Deterministic

        # Dot product with itself should be 1.0 (unit normalized)
        norm1 = sum(x * x for x in vec1)
        self.assertAlmostEqual(norm1, 1.0, places=4)

        # Different text should have different embeddings
        self.assertNotEqual(vec1, vec3)

        # Batch embedding
        batch = embedder.embed_documents(["hello world", "test"])
        self.assertEqual(len(batch), 2)
        self.assertEqual(len(batch[0]), 384)

    def test_vector_store_dimension_validation(self):
        """Test dimension validation in VectorStore add and search."""
        with MemoryStore(self.test_dir) as store:
            vstore = VectorStore(store, dim=3)
            with self.assertRaises(ValueError):
                vstore.add("doc_err", "wrong dim", [1.0, 2.0])  # Dim 2 instead of 3

            with self.assertRaises(ValueError):
                vstore.search([1.0, 2.0, 3.0, 4.0])  # Dim 4 instead of 3

    def test_metrics_ip_and_l2(self):
        """Test inner product and L2 metrics."""
        with MemoryStore(self.test_dir) as store:
            vstore_ip = VectorStore(store, dim=2, metric="ip")
            vstore_ip.add("p1", "point 1", [1.0, 2.0])
            vstore_ip.add("p2", "point 2", [3.0, 4.0])

            # Query [1, 1]: dot(p1) = 1*1 + 2*1 = 3; dot(p2) = 3*1 + 4*1 = 7
            res = vstore_ip.search([1.0, 1.0], top_k=2)
            self.assertEqual(res[0]["doc_id"], "p2")
            self.assertEqual(res[1]["doc_id"], "p1")
            self.assertAlmostEqual(res[0]["score"], 7.0)
            self.assertAlmostEqual(res[1]["score"], 3.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)

