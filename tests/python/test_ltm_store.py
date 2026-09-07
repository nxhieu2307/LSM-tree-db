import os
import shutil
import unittest
from ltm import MemoryStore, MemoryChunk, ChatMessage


class TestLTMStore(unittest.TestCase):
    def setUp(self):
        self.test_dir = "test_ltm_db"
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def tearDown(self):
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def test_chat_message_model(self):
        msg = ChatMessage(
            role="user",
            content="Hello world",
            timestamp=1700000000.0,
            metadata={"session_id": "sess_123", "token_count": 2},
        )
        d = msg.to_dict()
        self.assertEqual(d["role"], "user")
        self.assertEqual(d["content"], "Hello world")
        self.assertEqual(d["timestamp"], 1700000000.0)
        self.assertEqual(d["metadata"]["session_id"], "sess_123")

        restored = ChatMessage.from_dict(d)
        self.assertEqual(restored.role, msg.role)
        self.assertEqual(restored.content, msg.content)
        self.assertEqual(restored.timestamp, msg.timestamp)
        self.assertEqual(restored.metadata, msg.metadata)

    def test_memory_chunk_model(self):
        chunk = MemoryChunk(
            doc_id="doc_abc",
            chunk_id=42,
            text="LSM-tree is an append-only data structure.",
            metadata={"source": "wikipedia", "page": 1},
            embedding=[0.1, 0.2, -0.3, 0.4],
        )
        d = chunk.to_dict()
        self.assertEqual(d["doc_id"], "doc_abc")
        self.assertEqual(d["chunk_id"], 42)
        self.assertEqual(d["embedding"], [0.1, 0.2, -0.3, 0.4])

        restored = MemoryChunk.from_dict(d)
        self.assertEqual(restored.doc_id, chunk.doc_id)
        self.assertEqual(restored.chunk_id, chunk.chunk_id)
        self.assertEqual(restored.text, chunk.text)
        self.assertEqual(restored.metadata, chunk.metadata)
        self.assertEqual(restored.embedding, chunk.embedding)

    def test_put_and_get_chunk(self):
        store = MemoryStore(self.test_dir)
        chunk = MemoryChunk(
            doc_id="report_2026",
            chunk_id=1,
            text="Executive summary of local AI architectures.",
            metadata={"author": "AI Team"},
            embedding=[0.05, -0.12, 0.98],
        )
        store.put_chunk(chunk)

        retrieved = store.get_chunk("report_2026", 1)
        self.assertIsNotNone(retrieved)
        self.assertEqual(retrieved.doc_id, "report_2026")
        self.assertEqual(retrieved.chunk_id, 1)
        self.assertEqual(retrieved.text, "Executive summary of local AI architectures.")
        self.assertEqual(retrieved.metadata, {"author": "AI Team"})
        self.assertEqual(retrieved.embedding, [0.05, -0.12, 0.98])

        store.close()

    def test_get_non_existent_chunk(self):
        store = MemoryStore(self.test_dir)
        self.assertIsNone(store.get_chunk("non_existent_doc", 0))
        self.assertIsNone(store.get_chunk("doc_1", 999))
        store.close()

    def test_delete_chunk(self):
        store = MemoryStore(self.test_dir)
        chunk = MemoryChunk(
            doc_id="temp_doc",
            chunk_id=0,
            text="Temporary note to be deleted.",
        )
        store.put_chunk(chunk)
        self.assertIsNotNone(store.get_chunk("temp_doc", 0))

        store.delete_chunk("temp_doc", 0)
        self.assertIsNone(store.get_chunk("temp_doc", 0))
        store.close()

    def test_context_manager_and_persistence(self):
        chunk1 = MemoryChunk(doc_id="paper_1", chunk_id=0, text="Section 1: Intro")
        chunk2 = MemoryChunk(doc_id="paper_1", chunk_id=1, text="Section 2: Method")

        with MemoryStore(self.test_dir) as store:
            store.put_chunk(chunk1)
            store.put_chunk(chunk2)

        # Reopen in new context manager session to verify persistence
        with MemoryStore(self.test_dir) as store:
            r1 = store.get_chunk("paper_1", 0)
            r2 = store.get_chunk("paper_1", 1)
            self.assertIsNotNone(r1)
            self.assertIsNotNone(r2)
            self.assertEqual(r1.text, "Section 1: Intro")
            self.assertEqual(r2.text, "Section 2: Method")


if __name__ == "__main__":
    unittest.main(verbosity=2)
