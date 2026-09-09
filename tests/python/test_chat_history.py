import os
import shutil
import unittest
import pylsm
from ltm import MemoryStore, ChatMessage, ChatHistory


class TestChatHistory(unittest.TestCase):
    def setUp(self):
        self.test_dir = "test_chat_history_db"
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def tearDown(self):
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def test_multi_turn_sequence_integrity(self):
        with MemoryStore(self.test_dir) as store:
            chat = ChatHistory(store)

            # Alternate conversation turns
            msg1 = chat.append_message("sess_1", "user", "Hello assistant!")
            msg2 = chat.append_message("sess_1", "assistant", "Hello! How can I assist you today?")
            msg3 = chat.append_message("sess_1", "user", "Explain LSM-trees briefly.")
            msg4 = chat.append_message("sess_1", "assistant", "LSM-trees are write-optimized data structures.")

            self.assertIsInstance(msg1, ChatMessage)
            self.assertEqual(msg1.role, "user")
            self.assertEqual(msg1.content, "Hello assistant!")
            self.assertEqual(msg4.role, "assistant")
            self.assertEqual(msg4.content, "LSM-trees are write-optimized data structures.")

            # Direct key check in engine to verify zero-padded key formatting
            raw_keys = [k for k, _ in store.engine.prefix_scan("chat:sess_1:")]
            expected_keys = [
                "chat:sess_1:00000000",
                "chat:sess_1:00000001",
                "chat:sess_1:00000002",
                "chat:sess_1:00000003",
            ]
            self.assertEqual(raw_keys, expected_keys)

            # Verify meta key
            meta_val = store.engine.get("meta:chat_turn:sess_1")
            self.assertEqual(meta_val, "3")

    def test_get_history_all_and_chronological_order(self):
        with MemoryStore(self.test_dir) as store:
            chat = ChatHistory(store)
            chat.append_message("sess_order", "user", "Turn 0")
            chat.append_message("sess_order", "assistant", "Turn 1")
            chat.append_message("sess_order", "user", "Turn 2")

            history = chat.get_history("sess_order")
            self.assertEqual(len(history), 3)
            self.assertEqual(history[0].content, "Turn 0")
            self.assertEqual(history[1].content, "Turn 1")
            self.assertEqual(history[2].content, "Turn 2")
            self.assertTrue(history[0].timestamp <= history[1].timestamp <= history[2].timestamp)

    def test_get_history_last_n(self):
        with MemoryStore(self.test_dir) as store:
            chat = ChatHistory(store)
            for i in range(5):
                chat.append_message("sess_slice", "user" if i % 2 == 0 else "assistant", f"Message {i}")

            # last_n = 2
            last_two = chat.get_history("sess_slice", last_n=2)
            self.assertEqual(len(last_two), 2)
            self.assertEqual(last_two[0].content, "Message 3")
            self.assertEqual(last_two[1].content, "Message 4")

            # last_n = 1
            last_one = chat.get_history("sess_slice", last_n=1)
            self.assertEqual(len(last_one), 1)
            self.assertEqual(last_one[0].content, "Message 4")

            # last_n larger than total messages
            all_msgs = chat.get_history("sess_slice", last_n=10)
            self.assertEqual(len(all_msgs), 5)

            # last_n = 0
            empty_slice = chat.get_history("sess_slice", last_n=0)
            self.assertEqual(empty_slice, [])

    def test_session_isolation(self):
        with MemoryStore(self.test_dir) as store:
            chat = ChatHistory(store)

            chat.append_message("sess_a", "user", "Hello from A1")
            chat.append_message("sess_b", "user", "Hello from B1")
            chat.append_message("sess_a", "assistant", "Response to A1")
            chat.append_message("sess_b", "assistant", "Response to B1")
            chat.append_message("sess_a", "user", "Follow-up A2")

            hist_a = chat.get_history("sess_a")
            hist_b = chat.get_history("sess_b")

            self.assertEqual(len(hist_a), 3)
            self.assertEqual([m.content for m in hist_a], ["Hello from A1", "Response to A1", "Follow-up A2"])

            self.assertEqual(len(hist_b), 2)
            self.assertEqual([m.content for m in hist_b], ["Hello from B1", "Response to B1"])

    def test_clear_session(self):
        with MemoryStore(self.test_dir) as store:
            chat = ChatHistory(store)

            chat.append_message("sess_del", "user", "To be deleted")
            chat.append_message("sess_del", "assistant", "Also to be deleted")
            chat.append_message("sess_keep", "user", "Keep me safe")

            self.assertEqual(len(chat.get_history("sess_del")), 2)
            self.assertEqual(len(chat.get_history("sess_keep")), 1)

            # Clear sess_del
            chat.clear_session("sess_del")

            self.assertEqual(chat.get_history("sess_del"), [])
            self.assertIsNone(store.engine.get("meta:chat_turn:sess_del"))

            # sess_keep must remain intact
            hist_keep = chat.get_history("sess_keep")
            self.assertEqual(len(hist_keep), 1)
            self.assertEqual(hist_keep[0].content, "Keep me safe")

            # Re-appending to cleared session starts from turn 0
            new_msg = chat.append_message("sess_del", "user", "Fresh start")
            self.assertEqual(new_msg.content, "Fresh start")
            raw_keys = [k for k, _ in store.engine.prefix_scan("chat:sess_del:")]
            self.assertEqual(raw_keys, ["chat:sess_del:00000000"])

    def test_persistence_and_restart(self):
        # Session 1: write initial messages and close
        with MemoryStore(self.test_dir) as store:
            chat = ChatHistory(store)
            chat.append_message("sess_persist", "user", "First question")
            chat.append_message("sess_persist", "assistant", "First answer")

        # Session 2: reopen store, check history and append further messages
        with MemoryStore(self.test_dir) as store:
            chat = ChatHistory(store)
            history = chat.get_history("sess_persist")
            self.assertEqual(len(history), 2)
            self.assertEqual(history[0].content, "First question")
            self.assertEqual(history[1].content, "First answer")

            # Append new message; should get turn 2
            msg3 = chat.append_message("sess_persist", "user", "Second question")
            self.assertEqual(msg3.content, "Second question")

            raw_keys = [k for k, _ in store.engine.prefix_scan("chat:sess_persist:")]
            self.assertEqual(
                raw_keys,
                [
                    "chat:sess_persist:00000000",
                    "chat:sess_persist:00000001",
                    "chat:sess_persist:00000002",
                ],
            )
            self.assertEqual(store.engine.get("meta:chat_turn:sess_persist"), "2")

    def test_direct_storage_engine_wrapping(self):
        engine = pylsm.StorageEngine(self.test_dir)
        try:
            chat = ChatHistory(engine)
            self.assertIs(chat.engine, engine)
            self.assertIsNone(chat.store)

            chat.append_message("sess_direct", "user", "Direct engine test")
            hist = chat.get_history("sess_direct")
            self.assertEqual(len(hist), 1)
            self.assertEqual(hist[0].content, "Direct engine test")
        finally:
            engine.close()

    def test_message_metadata_roundtrip(self):
        with MemoryStore(self.test_dir) as store:
            chat = ChatHistory(store)
            meta = {
                "model": "gemini-2.5-pro",
                "tokens": 42,
                "finish_reason": "stop",
                "tools_used": ["web_search"],
            }
            chat.append_message("sess_meta", "assistant", "Answer with metadata", metadata=meta)

            history = chat.get_history("sess_meta")
            self.assertEqual(len(history), 1)
            self.assertEqual(history[0].metadata, meta)

    def test_empty_session_history(self):
        with MemoryStore(self.test_dir) as store:
            chat = ChatHistory(store)
            self.assertEqual(chat.get_history("non_existent_session"), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
