import json
from typing import Optional
import pylsm
from .models import MemoryChunk


class MemoryStore:
    """High-level Long-Term Memory (LTM) store wrapping pylsm.StorageEngine."""

    def __init__(self, db_path: str, memtable_capacity: int = 4 * 1024 * 1024):
        self._engine = pylsm.StorageEngine(db_path, memtable_capacity)

    def put_chunk(self, chunk: MemoryChunk) -> None:
        key = f"doc:{chunk.doc_id}:{chunk.chunk_id:06d}"
        payload = json.dumps(chunk.to_dict())
        self._engine.put(key, payload)

    def get_chunk(self, doc_id: str, chunk_id: int) -> Optional[MemoryChunk]:
        key = f"doc:{doc_id}:{chunk_id:06d}"
        val = self._engine.get(key)
        if val is None:
            return None
        data = json.loads(val)
        return MemoryChunk.from_dict(data)

    def delete_chunk(self, doc_id: str, chunk_id: int) -> None:
        key = f"doc:{doc_id}:{chunk_id:06d}"
        self._engine.delete(key)

    def close(self) -> None:
        self._engine.close()

    def __enter__(self) -> "MemoryStore":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()
