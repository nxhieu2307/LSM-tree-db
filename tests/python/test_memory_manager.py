import os
import shutil
import unittest
from ltm import MemoryManager, DummyEmbedding


class TestMemoryManager(unittest.TestCase):
    def setUp(self):
        self.test_dir = "test_memory_manager_db"
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def tearDown(self):
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def test_remember_and_search(self):
        """Test remembering knowledge chunks and searching them."""
        with MemoryManager(self.test_dir, dim=384) as mm:
            id1 = mm.remember(
                "LSM-Tree uses Write-Ahead Logging (WAL) for durability.",
                doc_id="arch_doc_1",
                metadata={"topic": "storage"},
            )
            id2 = mm.remember(
                "MemTable buffers writes in-memory before flushing to SSTables.",
                doc_id="arch_doc_2",
                metadata={"topic": "storage"},
            )
            id3 = mm.remember(
                "French onion soup is made with caramelized onions and beef broth.",
                doc_id="recipe_doc_1",
                metadata={"topic": "food"},
            )

            self.assertEqual(id1, 0)
            self.assertEqual(id2, 1)
            self.assertEqual(id3, 2)
            self.assertEqual(mm.vector_store.count(), 3)

            # Raw chunk verification in MemoryStore
            chunk1 = mm.store.get_chunk("arch_doc_1", 0)
            self.assertIsNotNone(chunk1)
            self.assertEqual(chunk1.text, "LSM-Tree uses Write-Ahead Logging (WAL) for durability.")
            self.assertEqual(chunk1.metadata, {"topic": "storage"})

            # Semantic search query with token overlap
            recall_res = mm.recall("LSM-Tree WAL durability storage", top_k_facts=2)
            self.assertEqual(recall_res["query"], "LSM-Tree WAL durability storage")
            self.assertEqual(len(recall_res["relevant_facts"]), 2)
            self.assertEqual(len(recall_res["recent_history"]), 0)

            # Formatted prompt should include relevant knowledge
            prompt = recall_res["formatted_prompt_context"]
            self.assertIn("### Relevant Knowledge:", prompt)
            self.assertIn("LSM-Tree uses Write-Ahead Logging", prompt)

    def test_chat_turns_and_history(self):
        """Test conversational episodic memory turns."""
        with MemoryManager(self.test_dir) as mm:
            session_id = "user_sess_101"
            m1 = mm.add_chat_turn(session_id, "user", "What is the status of project Apollo?")
            m2 = mm.add_chat_turn(session_id, "assistant", "Project Apollo is on track for Q4 release.")
            m3 = mm.add_chat_turn(session_id, "user", "Can we add vector search to it?")

            self.assertEqual(m1.role, "user")
            self.assertEqual(m2.role, "assistant")
            self.assertEqual(m3.role, "user")

            # Fetch all history
            history = mm.get_chat_history(session_id)
            self.assertEqual(len(history), 3)
            self.assertEqual(history[0].content, "What is the status of project Apollo?")
            self.assertEqual(history[1].content, "Project Apollo is on track for Q4 release.")
            self.assertEqual(history[2].content, "Can we add vector search to it?")

            # Fetch last_n turns
            last_2 = mm.get_chat_history(session_id, last_n=2)
            self.assertEqual(len(last_2), 2)
            self.assertEqual(last_2[0].content, "Project Apollo is on track for Q4 release.")
            self.assertEqual(last_2[1].content, "Can we add vector search to it?")

    def test_recall_hybrid_payload_and_prompt_formatting(self):
        """Test hybrid recall combining semantic facts and episodic conversation turns."""
        with MemoryManager(self.test_dir) as mm:
            # Add long-term facts
            mm.remember("Antigravity engine supports zero-gravity propulsion.", doc_id="sci_1")
            mm.remember("Quantum stabilizers require liquid helium cooling.", doc_id="sci_2")

            # Add dialogue turns
            session_id = "agent_session_42"
            mm.add_chat_turn(session_id, "user", "Check propulsion systems.")
            mm.add_chat_turn(session_id, "assistant", "Propulsion systems check complete. All nominal.")
            mm.add_chat_turn(session_id, "user", "What about cooling?")

            recall_data = mm.recall(
                query="How are quantum stabilizers cooled?",
                session_id=session_id,
                top_k_facts=2,
                last_n_turns=2,
            )

            # Validate structured dictionary
            self.assertEqual(recall_data["query"], "How are quantum stabilizers cooled?")
            self.assertTrue(len(recall_data["relevant_facts"]) > 0)
            self.assertEqual(len(recall_data["recent_history"]), 2)

            # Check history entries
            self.assertEqual(recall_data["recent_history"][0]["role"], "assistant")
            self.assertEqual(recall_data["recent_history"][1]["role"], "user")

            # Check prompt context formatting
            prompt_context = recall_data["formatted_prompt_context"]
            self.assertIn("### Relevant Knowledge:", prompt_context)
            self.assertIn("Quantum stabilizers require liquid helium cooling.", prompt_context)
            self.assertIn("### Conversation History:", prompt_context)
            self.assertIn("Assistant: Propulsion systems check complete.", prompt_context)
            self.assertIn("User: What about cooling?", prompt_context)

    def test_persistence_across_restarts(self):
        """Test full restart persistence for knowledge, chat turns, and sequence IDs."""
        session_id = "persist_sess"

        # Phase 1: Ingest facts and dialogue, then close
        with MemoryManager(self.test_dir, dim=4) as mm1:
            # Custom deterministic embedder
            mm1.embedder = DummyEmbedding(dim=4)
            id0 = mm1.remember("System alpha online", doc_id="sys_a")
            id1 = mm1.remember("System beta offline", doc_id="sys_b")
            self.assertEqual(id0, 0)
            self.assertEqual(id1, 1)

            mm1.add_chat_turn(session_id, "user", "Hello computer.")
            mm1.add_chat_turn(session_id, "assistant", "Greetings, operator.")

        # Phase 2: Reopen in new MemoryManager instance on exact same directory
        with MemoryManager(self.test_dir, dim=4) as mm2:
            mm2.embedder = DummyEmbedding(dim=4)

            # Verify index rehydration
            self.assertEqual(mm2.vector_store.count(), 2)

            # Verify hybrid recall on existing data
            res = mm2.recall("System status", session_id=session_id, top_k_facts=2, last_n_turns=2)
            self.assertEqual(len(res["relevant_facts"]), 2)
            self.assertEqual(len(res["recent_history"]), 2)
            self.assertIn("System alpha online", res["formatted_prompt_context"])
            self.assertIn("Greetings, operator.", res["formatted_prompt_context"])

            # Verify sequence ID counter continuation
            id2 = mm2.remember("System gamma standby", doc_id="sys_g")
            self.assertEqual(id2, 2)
            self.assertEqual(mm2.vector_store.count(), 3)

            turn3 = mm2.add_chat_turn(session_id, "user", "Status check.")
            history = mm2.get_chat_history(session_id)
            self.assertEqual(len(history), 3)
            self.assertEqual(history[2].content, "Status check.")

    def test_custom_embedding_model(self):
        """Test custom callable or object embedding model."""
        def custom_embedder(text: str):
            # 4-dim one-hot vector based on simple keyword match
            if "database" in text:
                return [1.0, 0.0, 0.0, 0.0]
            elif "network" in text:
                return [0.0, 1.0, 0.0, 0.0]
            else:
                return [0.0, 0.0, 1.0, 0.0]

        with MemoryManager(self.test_dir, embedding_model=custom_embedder, dim=4) as mm:
            mm.remember("Postgres is a relational database.", doc_id="db_doc")
            mm.remember("BGP is a network protocol.", doc_id="net_doc")

            res_db = mm.recall("Tell me about database systems.", top_k_facts=1)
            self.assertEqual(len(res_db["relevant_facts"]), 1)
            self.assertEqual(res_db["relevant_facts"][0]["text"], "Postgres is a relational database.")
            self.assertAlmostEqual(res_db["relevant_facts"][0]["score"], 1.0, places=3)


if __name__ == "__main__":
    unittest.main(verbosity=2)
