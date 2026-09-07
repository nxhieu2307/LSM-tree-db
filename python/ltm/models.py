from dataclasses import dataclass, field, asdict
from typing import Optional, List, Dict, Any


@dataclass
class ChatMessage:
    role: str
    content: str
    timestamp: float
    metadata: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "ChatMessage":
        return cls(
            role=d["role"],
            content=d["content"],
            timestamp=float(d["timestamp"]),
            metadata=d.get("metadata", {}),
        )


@dataclass
class MemoryChunk:
    doc_id: str
    chunk_id: int
    text: str
    metadata: Dict[str, Any] = field(default_factory=dict)
    embedding: Optional[List[float]] = None

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "MemoryChunk":
        return cls(
            doc_id=d["doc_id"],
            chunk_id=int(d["chunk_id"]),
            text=d["text"],
            metadata=d.get("metadata", {}),
            embedding=d.get("embedding"),
        )
