import uuid
from typing import Optional, List, Dict, Any, Union
from .models import ChatMessage, MemoryChunk
from .store import MemoryStore
from .chat_history import ChatHistory
from .vector_index import VectorStore, DummyEmbedding, get_default_embedding_model


class MemoryManager:
    """Top-level Long-Term Memory (LTM) coordinator for local AI agents.

    Unifies:
    1. Knowledge Ingestion: Chunking, embedding, vector indexing, and LSM persistence.
    2. Episodic Chat Tracking: Session-keyed chronological conversation turn logs.
    3. Hybrid Recall: Combined semantic knowledge retrieval and conversational recency
       assembled into an LLM-ready prompt context string.
    """

    def __init__(
        self,
        db_path: str,
        embedding_model: Optional[Any] = None,
        dim: int = 384,
        memtable_capacity: int = 4 * 1024 * 1024,
    ):
        """Initializes MemoryManager.

        Args:
            db_path: Path to the database directory.
            embedding_model: Optional custom embedding model/callable. If omitted,
                             fastembed or fallback DummyEmbedding is used.
            dim: Vector embedding dimensionality (default: 384).
            memtable_capacity: LSM memtable byte capacity (default: 4MB).
        """
        self.db_path = db_path
        self.dim = dim
        self.store = MemoryStore(db_path=db_path, memtable_capacity=memtable_capacity)
        self.chat_history = ChatHistory(self.store)
        self.vector_store = VectorStore(self.store, dim=dim)

        if embedding_model is not None:
            self.embedder = embedding_model
        else:
            self.embedder = get_default_embedding_model(dim=dim)

    def _embed(self, text: str) -> List[float]:
        """Generates dense embedding vector for text using the configured embedder."""
        if hasattr(self.embedder, "embed"):
            vec = self.embedder.embed(text)
        elif hasattr(self.embedder, "encode"):
            vec = self.embedder.encode(text)
        elif callable(self.embedder):
            vec = self.embedder(text)
        elif hasattr(self.embedder, "embed_documents"):
            vec = self.embedder.embed_documents([text])[0]
        else:
            raise TypeError(f"Configured embedder {type(self.embedder)} does not support embedding text.")

        if hasattr(vec, "tolist"):
            vec = vec.tolist()
        return [float(x) for x in vec]

    def remember(
        self,
        text: str,
        doc_id: Optional[str] = None,
        metadata: Optional[Dict[str, Any]] = None,
    ) -> int:
        """Ingests a piece of knowledge into the memory store and vector index.

        Args:
            text: Text content of the knowledge fact/document.
            doc_id: Unique string identifier (generated if omitted).
            metadata: Optional dictionary of metadata attributes.

        Returns:
            The assigned integer vector ID (int_id).
        """
        if doc_id is None:
            doc_id = f"mem_{uuid.uuid4().hex[:12]}"

        meta = metadata if metadata is not None else {}
        embedding = self._embed(text)

        # 1. Persist raw chunk in MemoryStore
        chunk = MemoryChunk(
            doc_id=doc_id,
            chunk_id=0,
            text=text,
            metadata=meta,
            embedding=embedding,
        )
        self.store.put_chunk(chunk)

        # 2. Index vector and payload in VectorStore
        int_id = self.vector_store.add(
            doc_id=doc_id,
            text=text,
            embedding=embedding,
            metadata=meta,
        )
        return int_id

    def add_chat_turn(
        self,
        session_id: str,
        role: str,
        content: str,
        metadata: Optional[Dict[str, Any]] = None,
    ) -> ChatMessage:
        """Appends a new conversational message turn to episodic chat history.

        Args:
            session_id: Unique identifier for the conversation session.
            role: Speaker role ('user', 'assistant', 'system', etc.).
            content: Message body.
            metadata: Optional dictionary of metadata attributes.

        Returns:
            The created ChatMessage object.
        """
        return self.chat_history.append_message(
            session_id=session_id,
            role=role,
            content=content,
            metadata=metadata,
        )

    def get_chat_history(
        self, session_id: str, last_n: Optional[int] = None
    ) -> List[ChatMessage]:
        """Retrieves chronological dialogue turns for a session.

        Args:
            session_id: Unique identifier for the conversation session.
            last_n: Optional limit to return only the most recent N turns.

        Returns:
            List of ChatMessage objects in chronological order.
        """
        return self.chat_history.get_history(session_id=session_id, last_n=last_n)

    def recall(
        self,
        query: str,
        session_id: Optional[str] = None,
        top_k_facts: int = 3,
        last_n_turns: int = 5,
    ) -> Dict[str, Any]:
        """Performs hybrid recall combining semantic facts and episodic conversation turns.

        Args:
            query: Semantic search query string.
            session_id: Optional session identifier for episodic history.
            top_k_facts: Max number of semantically relevant facts to retrieve.
            last_n_turns: Max number of recent chat turns to retrieve.

        Returns:
            Structured dictionary containing:
            - "query": Query string.
            - "relevant_facts": List of matching knowledge facts with scores.
            - "recent_history": Chronological list of recent chat turns.
            - "formatted_prompt_context": Formatted context block for LLM prompts.
        """
        query_vec = self._embed(query)
        semantic_hits = self.vector_store.search(query_vec, top_k=top_k_facts)

        relevant_facts = [
            {
                "text": hit["text"],
                "score": hit["score"],
                "metadata": hit["metadata"],
            }
            for hit in semantic_hits
        ]

        if session_id:
            history_messages = self.chat_history.get_history(
                session_id=session_id, last_n=last_n_turns
            )
        else:
            history_messages = []

        recent_history = [
            {
                "role": msg.role,
                "content": msg.content,
                "timestamp": msg.timestamp,
            }
            for msg in history_messages
        ]

        # Assemble LLM prompt context string
        sections: List[str] = []
        if relevant_facts:
            facts_lines = [f"- {fact['text']}" for fact in relevant_facts]
            sections.append("### Relevant Knowledge:\n" + "\n".join(facts_lines))

        if recent_history:
            history_lines = [
                f"{msg['role'].capitalize()}: {msg['content']}" for msg in recent_history
            ]
            sections.append("### Conversation History:\n" + "\n".join(history_lines))

        formatted_prompt_context = "\n\n".join(sections)

        return {
            "query": query,
            "relevant_facts": relevant_facts,
            "recent_history": recent_history,
            "formatted_prompt_context": formatted_prompt_context,
        }

    def close(self) -> None:
        """Closes the underlying database and flushes active memtables."""
        self.store.close()

    def __enter__(self) -> "MemoryManager":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()
