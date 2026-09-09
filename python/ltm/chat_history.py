import json
import time
from typing import Optional, List, Dict, Any, Union
import pylsm
from .models import ChatMessage
from .store import MemoryStore


class ChatHistory:
    """Episodic chat memory manager backed by LSM-Tree storage.

    Stores conversation turns sequentially under keys:
        chat:<session_id>:<turn_index:08d>
    and tracks session turn counter under:
        meta:chat_turn:<session_id>
    """

    def __init__(self, store: Union[MemoryStore, pylsm.StorageEngine]):
        if isinstance(store, MemoryStore):
            self._store: Optional[MemoryStore] = store
            self._engine: pylsm.StorageEngine = store.engine
        else:
            self._store = None
            self._engine = store

    @property
    def engine(self) -> pylsm.StorageEngine:
        return self._engine

    @property
    def store(self) -> Optional[MemoryStore]:
        return self._store

    def append_message(
        self,
        session_id: str,
        role: str,
        content: str,
        metadata: Optional[Dict[str, Any]] = None,
    ) -> ChatMessage:
        """Appends a new turn message to the episodic chat history.

        Args:
            session_id: Unique session identifier.
            role: Role of the speaker (e.g. 'user', 'assistant', 'system').
            content: The text content of the message.
            metadata: Optional dictionary of additional metadata attributes.

        Returns:
            The created ChatMessage object.
        """
        meta_key = f"meta:chat_turn:{session_id}"
        meta_val = self._engine.get(meta_key)
        if meta_val is not None:
            turn_index = int(meta_val) + 1
        else:
            turn_index = 0

        msg = ChatMessage(
            role=role,
            content=content,
            timestamp=time.time(),
            metadata=metadata if metadata is not None else {},
        )

        turn_key = f"chat:{session_id}:{turn_index:08d}"
        payload = json.dumps(msg.to_dict())

        self._engine.put(turn_key, payload)
        self._engine.put(meta_key, str(turn_index))
        return msg

    def get_history(
        self, session_id: str, last_n: Optional[int] = None
    ) -> List[ChatMessage]:
        """Retrieves chronological chat history for a session.

        Args:
            session_id: Unique session identifier.
            last_n: If provided and > 0, returns only the most recent last_n messages.
                    If <= 0, returns an empty list.

        Returns:
            List of ChatMessage objects in chronological order.
        """
        prefix = f"chat:{session_id}:"
        items = self._engine.prefix_scan(prefix)

        messages: List[ChatMessage] = []
        for _, payload in items:
            data = json.loads(payload)
            messages.append(ChatMessage.from_dict(data))

        if last_n is not None:
            if last_n <= 0:
                return []
            return messages[-last_n:]

        return messages

    def clear_session(self, session_id: str) -> None:
        """Deletes all messages and metadata associated with a session.

        Args:
            session_id: Unique session identifier.
        """
        prefix = f"chat:{session_id}:"
        items = self._engine.prefix_scan(prefix)
        for key, _ in items:
            self._engine.delete(key)

        meta_key = f"meta:chat_turn:{session_id}"
        self._engine.delete(meta_key)
