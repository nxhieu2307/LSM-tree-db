import json
import math
import hashlib
from typing import Optional, List, Dict, Any, Union, Tuple
import pylsm
from .store import MemoryStore


class _FallbackVectorIndex:
    """In-memory vector index with pure Python / NumPy fallback.

    Provides exact nearest neighbor search using cosine similarity or euclidean distance.
    """

    def __init__(self, dim: int = 384, metric: str = "cos"):
        self.dim = dim
        self.metric = metric.lower()
        self._vectors: Dict[int, List[float]] = {}

    def add(self, int_id: int, vector: Union[List[float], Any]) -> None:
        if hasattr(vector, "tolist"):
            vec = [float(x) for x in vector.tolist()]
        else:
            vec = [float(x) for x in vector]
        if len(vec) != self.dim:
            raise ValueError(f"Vector dimension mismatch: expected {self.dim}, got {len(vec)}")
        self._vectors[int(int_id)] = vec

    def __len__(self) -> int:
        return len(self._vectors)

    def search(self, query_vector: Union[List[float], Any], top_k: int = 5) -> List[Tuple[int, float]]:
        if hasattr(query_vector, "tolist"):
            q = [float(x) for x in query_vector.tolist()]
        else:
            q = [float(x) for x in query_vector]

        if len(q) != self.dim:
            raise ValueError(f"Query vector dimension mismatch: expected {self.dim}, got {len(q)}")

        if not self._vectors or top_k <= 0:
            return []

        q_norm = math.sqrt(sum(x * x for x in q))
        scores: List[Tuple[int, float]] = []

        for int_id, vec in self._vectors.items():
            if self.metric in ("cos", "cosine"):
                dot = sum(a * b for a, b in zip(q, vec))
                v_norm = math.sqrt(sum(y * y for y in vec))
                if q_norm > 0.0 and v_norm > 0.0:
                    sim = dot / (q_norm * v_norm)
                else:
                    sim = 0.0
                scores.append((int_id, sim))
            elif self.metric in ("ip", "dot"):
                dot = sum(a * b for a, b in zip(q, vec))
                scores.append((int_id, dot))
            elif self.metric in ("l2", "euclidean"):
                dist = math.sqrt(sum((a - b) ** 2 for a, b in zip(q, vec)))
                scores.append((int_id, -dist))
            else:
                dot = sum(a * b for a, b in zip(q, vec))
                scores.append((int_id, dot))

        # Sort descending by score (highest similarity first)
        scores.sort(key=lambda item: item[1], reverse=True)
        return scores[:top_k]


def _create_vector_index(dim: int = 384, metric: str = "cos") -> Any:
    """Factory creating the best available vector index backend.

    Tries usearch -> hnswlib -> _FallbackVectorIndex.
    """
    try:
        import usearch.index
        import numpy as np

        class USearchWrapper:
            def __init__(self, d: int, m: str):
                metric_name = "cos" if m in ("cos", "cosine") else m
                self._index = usearch.index.Index(ndim=d, metric=metric_name)
                self.dim = d

            def add(self, int_id: int, vector: Any) -> None:
                arr = np.array(vector, dtype=np.float32)
                self._index.add(int(int_id), arr)

            def search(self, query_vector: Any, top_k: int = 5) -> List[Tuple[int, float]]:
                if len(self._index) == 0 or top_k <= 0:
                    return []
                arr = np.array(query_vector, dtype=np.float32)
                matches = self._index.search(arr, count=top_k)
                results: List[Tuple[int, float]] = []
                keys = matches.keys
                distances = matches.distances
                for k, d in zip(keys, distances):
                    sim = 1.0 - float(d)
                    results.append((int(k), sim))
                return results

            def __len__(self) -> int:
                return len(self._index)

        return USearchWrapper(dim, metric)
    except ImportError:
        pass

    try:
        import hnswlib
        import numpy as np

        class HnswlibWrapper:
            def __init__(self, d: int, m: str):
                space = "cosine" if m in ("cos", "cosine") else ("l2" if m in ("l2", "euclidean") else "ip")
                self._index = hnswlib.Index(space=space, dim=d)
                self._index.init_index(max_elements=10000, ef_construction=200, M=16)
                self._index.set_ef(50)
                self.dim = d
                self._count = 0

            def add(self, int_id: int, vector: Any) -> None:
                arr = np.array([vector], dtype=np.float32)
                if self._count >= self._index.get_max_elements():
                    self._index.resize_index(self._count * 2)
                self._index.add_items(arr, [int(int_id)])
                self._count += 1

            def search(self, query_vector: Any, top_k: int = 5) -> List[Tuple[int, float]]:
                if self._count == 0 or top_k <= 0:
                    return []
                k = min(top_k, self._count)
                arr = np.array([query_vector], dtype=np.float32)
                labels, distances = self._index.knn_query(arr, k=k)
                results: List[Tuple[int, float]] = []
                for label, dist in zip(labels[0], distances[0]):
                    sim = 1.0 - float(dist)
                    results.append((int(label), sim))
                return results

            def __len__(self) -> int:
                return self._count

        return HnswlibWrapper(dim, metric)
    except ImportError:
        pass

    return _FallbackVectorIndex(dim=dim, metric=metric)


class DummyEmbedding:
    """Deterministic, lightweight pseudo-embedding generator.

    Produces normalized float vectors for testing and local environments without ML weights.
    """

    def __init__(self, dim: int = 384):
        self.dim = dim

    def embed(self, text: str) -> List[float]:
        vec = [0.0] * self.dim
        tokens = text.lower().strip().split()
        if not tokens:
            tokens = ["<empty>"]

        for token in tokens:
            digest = hashlib.sha256(token.encode("utf-8")).digest()
            for i in range(self.dim):
                byte_val = digest[i % len(digest)]
                signed_val = byte_val - 128
                vec[i] += float(signed_val)

        # L2-normalize
        norm = math.sqrt(sum(x * x for x in vec))
        if norm > 0:
            vec = [x / norm for x in vec]
        return vec

    def embed_documents(self, texts: List[str]) -> List[List[float]]:
        return [self.embed(t) for t in texts]


def get_default_embedding_model(dim: int = 384) -> Any:
    """Returns FastEmbed model if installed, otherwise returns DummyEmbedding."""
    try:
        import fastembed

        class FastEmbedWrapper:
            def __init__(self, model_name: str = "BAAI/bge-small-en-v1.5"):
                self.model = fastembed.TextEmbedding(model_name=model_name)

            def embed(self, text: str) -> List[float]:
                embeddings = list(self.model.embed([text]))
                return embeddings[0].tolist()

            def embed_documents(self, texts: List[str]) -> List[List[float]]:
                return [e.tolist() for e in self.model.embed(texts)]

        return FastEmbedWrapper()
    except (ImportError, Exception):
        return DummyEmbedding(dim=dim)


class VectorStore:
    """Semantic vector store pairing in-memory ANN search with persistent LSM-tree storage.

    Stores vector payloads sequentially under keys:
        vec:<int_id:08d>
    and tracks monotonic sequence ID counter under:
        meta:vector_id_counter
    """

    def __init__(
        self,
        store: Union[MemoryStore, pylsm.StorageEngine],
        dim: int = 384,
        metric: str = "cos",
        embedding_model: Optional[Any] = None,
    ):
        """Initializes VectorStore.

        Args:
            store: Active MemoryStore or pylsm.StorageEngine instance.
            dim: Dimension of vector embeddings (default: 384).
            metric: Distance/similarity metric ('cos', 'cosine', 'l2', 'ip').
            embedding_model: Optional embedding generator model.
        """
        if isinstance(store, MemoryStore):
            self._store: Optional[MemoryStore] = store
            self._engine: pylsm.StorageEngine = store.engine
        else:
            self._store = None
            self._engine = store

        self._dim = dim
        self._metric = metric
        self._embedding_model = embedding_model
        self._index = _create_vector_index(dim=self._dim, metric=self._metric)
        self._id_counter = 0

        self._rehydrate_index()

    @property
    def engine(self) -> pylsm.StorageEngine:
        return self._engine

    @property
    def store(self) -> Optional[MemoryStore]:
        return self._store

    @property
    def dim(self) -> int:
        return self._dim

    @property
    def metric(self) -> str:
        return self._metric

    def _rehydrate_index(self) -> None:
        """Scans 'vec:' entries from the storage engine and re-populates the in-memory index."""
        max_id = -1
        items = self._engine.prefix_scan("vec:")
        for _, payload in items:
            try:
                data = json.loads(payload)
                int_id = int(data["int_id"])
                embedding = data["embedding"]
                self._index.add(int_id, embedding)
                if int_id > max_id:
                    max_id = int_id
            except (KeyError, ValueError, json.JSONDecodeError):
                continue

        # Restore sequence ID counter from metadata or max discovered ID
        meta_val = self._engine.get("meta:vector_id_counter")
        if meta_val is not None:
            try:
                self._id_counter = int(meta_val)
            except ValueError:
                self._id_counter = max_id + 1 if max_id >= 0 else 0
        else:
            self._id_counter = max_id + 1 if max_id >= 0 else 0

        if max_id >= self._id_counter:
            self._id_counter = max_id + 1

    def add(
        self,
        doc_id: str,
        text: str,
        embedding: List[float],
        metadata: Optional[Dict[str, Any]] = None,
    ) -> int:
        """Adds a document and its embedding vector into the index and persistent store.

        Args:
            doc_id: Unique string identifier for the source document/chunk.
            text: Text content associated with the embedding.
            embedding: Float vector list of dimension `self.dim`.
            metadata: Optional dictionary of additional metadata attributes.

        Returns:
            The allocated integer vector ID (int_id).
        """
        if len(embedding) != self._dim:
            raise ValueError(f"Embedding dimension mismatch: expected {self._dim}, got {len(embedding)}")

        int_id = self._id_counter
        self._id_counter += 1

        # Add to in-memory ANN index
        self._index.add(int_id, embedding)

        # Prepare and persist JSON payload
        payload = {
            "int_id": int_id,
            "doc_id": doc_id,
            "text": text,
            "embedding": list(embedding),
            "metadata": metadata if metadata is not None else {},
        }
        key = f"vec:{int_id:08d}"
        self._engine.put(key, json.dumps(payload))

        # Update metadata counter
        self._engine.put("meta:vector_id_counter", str(self._id_counter))
        return int_id

    def search(self, query_vector: List[float], top_k: int = 5) -> List[Dict[str, Any]]:
        """Performs nearest-neighbor semantic search against the vector index.

        Args:
            query_vector: Float vector list of dimension `self.dim`.
            top_k: Number of nearest neighbors to retrieve.

        Returns:
            List of dictionaries sorted by similarity score (descending):
            [
                {
                    "int_id": int,
                    "doc_id": str,
                    "text": str,
                    "score": float,
                    "metadata": dict
                },
                ...
            ]
        """
        if len(query_vector) != self._dim:
            raise ValueError(f"Query vector dimension mismatch: expected {self._dim}, got {len(query_vector)}")

        if top_k <= 0 or len(self._index) == 0:
            return []

        matches = self._index.search(query_vector, top_k=top_k)
        results: List[Dict[str, Any]] = []

        for int_id, score in matches:
            val = self._engine.get(f"vec:{int_id:08d}")
            if val is None:
                continue
            try:
                data = json.loads(val)
                results.append(
                    {
                        "int_id": data["int_id"],
                        "doc_id": data["doc_id"],
                        "text": data["text"],
                        "score": float(score),
                        "metadata": data.get("metadata", {}),
                    }
                )
            except (KeyError, json.JSONDecodeError):
                continue

        # Sort results by similarity score in descending order
        results.sort(key=lambda r: r["score"], reverse=True)
        return results

    def get(self, int_id: int) -> Optional[Dict[str, Any]]:
        """Fetches the full vector document payload by its integer ID."""
        val = self._engine.get(f"vec:{int_id:08d}")
        if val is None:
            return None
        return json.loads(val)

    def count(self) -> int:
        """Returns total number of vectors in the active in-memory index."""
        return len(self._index)

    def __len__(self) -> int:
        return self.count()

    def close(self) -> None:
        """Closes the underlying storage engine if owned."""
        if self._store is not None:
            self._store.close()
        else:
            self._engine.close()

    def __enter__(self) -> "VectorStore":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()
