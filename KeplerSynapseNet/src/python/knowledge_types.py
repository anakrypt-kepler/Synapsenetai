from typing import Dict, List, Optional, Any
from dataclasses import dataclass, field
from enum import Enum
from datetime import datetime
import hashlib
import json

class KnowledgeCategory(Enum):
    FACTUAL = "factual"
    PROCEDURAL = "procedural"
    CONCEPTUAL = "conceptual"
    METACOGNITIVE = "metacognitive"
    CREATIVE = "creative"
    TECHNICAL = "technical"

class KnowledgeFormat(Enum):
    TEXT = "text"
    CODE = "code"
    DATA = "data"
    MIXED = "mixed"
    STRUCTURED = "structured"

class KnowledgeStatus(Enum):
    PENDING = "pending"
    VALIDATING = "validating"
    ACCEPTED = "accepted"
    REJECTED = "rejected"
    EXPIRED = "expired"

@dataclass
class KnowledgeMetadata:
    title: str = ""
    description: str = ""
    tags: List[str] = field(default_factory=list)
    language: str = "en"
    version: str = "1.0"
    sources: List[str] = field(default_factory=list)
    related_ids: List[str] = field(default_factory=list)
    custom: Dict[str, Any] = field(default_factory=dict)

@dataclass
class KnowledgeChunk:
    chunk_id: str
    knowledge_id: str
    content: str
    content_hash: str
    category: KnowledgeCategory
    format: KnowledgeFormat
    status: KnowledgeStatus
    submitter: str
    timestamp: int
    metadata: KnowledgeMetadata
    quality_score: float = 0.0
    validation_count: int = 0
    accept_count: int = 0
    reject_count: int = 0
    reward_amount: int = 0
    stake_amount: int = 0

class KnowledgeFactory:
    @staticmethod
    def create_chunk(content: str, category: str, submitter: str,
                     stake: int, metadata: Dict = None) -> KnowledgeChunk:
        content_hash = hashlib.sha256(content.encode()).hexdigest()
        knowledge_id = hashlib.sha256(
            f"{submitter}:{content_hash}:{datetime.now().timestamp()}".encode()
        ).hexdigest()[:32]
        chunk_id = hashlib.sha256(
            f"{knowledge_id}:0".encode()
        ).hexdigest()[:16]
        
        meta = KnowledgeMetadata()
        if metadata:
            meta.title = metadata.get("title", "")
            meta.description = metadata.get("description", "")
            meta.tags = metadata.get("tags", [])
            meta.language = metadata.get("language", "en")
            meta.sources = metadata.get("sources", [])
        
        format_type = KnowledgeFormat.TEXT
        if "```" in content or "def " in content or "class " in content:
            format_type = KnowledgeFormat.CODE
        elif "{" in content and "}" in content:
            format_type = KnowledgeFormat.STRUCTURED
        
        return KnowledgeChunk(
            chunk_id=chunk_id,
            knowledge_id=knowledge_id,
            content=content,
            content_hash=content_hash,
            category=KnowledgeCategory(category) if category in [c.value for c in KnowledgeCategory] else KnowledgeCategory.FACTUAL,
            format=format_type,
            status=KnowledgeStatus.PENDING,
            submitter=submitter,
            timestamp=int(datetime.now().timestamp()),
            metadata=meta,
            stake_amount=stake
        )
    
    @staticmethod
    def from_dict(data: Dict) -> KnowledgeChunk:
        meta = KnowledgeMetadata(
            title=data.get("metadata", {}).get("title", ""),
            description=data.get("metadata", {}).get("description", ""),
            tags=data.get("metadata", {}).get("tags", []),
            language=data.get("metadata", {}).get("language", "en"),
            sources=data.get("metadata", {}).get("sources", [])
        )
        
        return KnowledgeChunk(
            chunk_id=data.get("chunk_id", ""),
            knowledge_id=data.get("knowledge_id", ""),
            content=data.get("content", ""),
            content_hash=data.get("content_hash", ""),
            category=KnowledgeCategory(data.get("category", "factual")),
            format=KnowledgeFormat(data.get("format", "text")),
            status=KnowledgeStatus(data.get("status", "pending")),
            submitter=data.get("submitter", ""),
            timestamp=data.get("timestamp", 0),
            metadata=meta,
            quality_score=data.get("quality_score", 0.0),
            validation_count=data.get("validation_count", 0),
            accept_count=data.get("accept_count", 0),
            reject_count=data.get("reject_count", 0),
            reward_amount=data.get("reward_amount", 0),
            stake_amount=data.get("stake_amount", 0)
        )
    
    @staticmethod
    def to_dict(chunk: KnowledgeChunk) -> Dict:
        return {
            "chunk_id": chunk.chunk_id,
            "knowledge_id": chunk.knowledge_id,
            "content": chunk.content,
            "content_hash": chunk.content_hash,
            "category": chunk.category.value,
            "format": chunk.format.value,
            "status": chunk.status.value,
            "submitter": chunk.submitter,
            "timestamp": chunk.timestamp,
            "metadata": {
                "title": chunk.metadata.title,
                "description": chunk.metadata.description,
                "tags": chunk.metadata.tags,
                "language": chunk.metadata.language,
                "sources": chunk.metadata.sources
            },
            "quality_score": chunk.quality_score,
            "validation_count": chunk.validation_count,
            "accept_count": chunk.accept_count,
            "reject_count": chunk.reject_count,
            "reward_amount": chunk.reward_amount,
            "stake_amount": chunk.stake_amount
        }


class KnowledgeIndex:
    def __init__(self):
        self.by_id: Dict[str, KnowledgeChunk] = {}
        self.by_category: Dict[str, List[str]] = {c.value: [] for c in KnowledgeCategory}
        self.by_submitter: Dict[str, List[str]] = {}
        self.by_status: Dict[str, List[str]] = {s.value: [] for s in KnowledgeStatus}
        self.by_hash: Dict[str, str] = {}
    
    def add(self, chunk: KnowledgeChunk):
        self.by_id[chunk.knowledge_id] = chunk
        self.by_category[chunk.category.value].append(chunk.knowledge_id)
        
        if chunk.submitter not in self.by_submitter:
            self.by_submitter[chunk.submitter] = []
        self.by_submitter[chunk.submitter].append(chunk.knowledge_id)
        
        self.by_status[chunk.status.value].append(chunk.knowledge_id)
        self.by_hash[chunk.content_hash] = chunk.knowledge_id
    
    def remove(self, knowledge_id: str):
        if knowledge_id not in self.by_id:
            return
        
        chunk = self.by_id[knowledge_id]
        
        del self.by_id[knowledge_id]
        
        if knowledge_id in self.by_category[chunk.category.value]:
            self.by_category[chunk.category.value].remove(knowledge_id)
        
        if chunk.submitter in self.by_submitter:
            if knowledge_id in self.by_submitter[chunk.submitter]:
                self.by_submitter[chunk.submitter].remove(knowledge_id)
        
        if knowledge_id in self.by_status[chunk.status.value]:
            self.by_status[chunk.status.value].remove(knowledge_id)
        
        if chunk.content_hash in self.by_hash:
            del self.by_hash[chunk.content_hash]
    
    def update_status(self, knowledge_id: str, new_status: KnowledgeStatus):
        if knowledge_id not in self.by_id:
            return
        
        chunk = self.by_id[knowledge_id]
        old_status = chunk.status
        
        if knowledge_id in self.by_status[old_status.value]:
            self.by_status[old_status.value].remove(knowledge_id)
        
        chunk.status = new_status
        self.by_status[new_status.value].append(knowledge_id)
    
    def get(self, knowledge_id: str) -> Optional[KnowledgeChunk]:
        return self.by_id.get(knowledge_id)
    
    def get_by_hash(self, content_hash: str) -> Optional[KnowledgeChunk]:
        knowledge_id = self.by_hash.get(content_hash)
        if knowledge_id:
            return self.by_id.get(knowledge_id)
        return None
    
    def get_by_category(self, category: str, limit: int = 100) -> List[KnowledgeChunk]:
        ids = self.by_category.get(category, [])[:limit]
        return [self.by_id[kid] for kid in ids if kid in self.by_id]
    
    def get_by_submitter(self, submitter: str, limit: int = 100) -> List[KnowledgeChunk]:
        ids = self.by_submitter.get(submitter, [])[:limit]
        return [self.by_id[kid] for kid in ids if kid in self.by_id]
    
    def get_by_status(self, status: str, limit: int = 100) -> List[KnowledgeChunk]:
        ids = self.by_status.get(status, [])[:limit]
        return [self.by_id[kid] for kid in ids if kid in self.by_id]
    
    def exists(self, content_hash: str) -> bool:
        return content_hash in self.by_hash
    
    def count(self) -> int:
        return len(self.by_id)
    
    def count_by_category(self) -> Dict[str, int]:
        return {cat: len(ids) for cat, ids in self.by_category.items()}
    
    def count_by_status(self) -> Dict[str, int]:
        return {status: len(ids) for status, ids in self.by_status.items()}
    
    def get_stats(self) -> Dict:
        return {
            "total": self.count(),
            "by_category": self.count_by_category(),
            "by_status": self.count_by_status(),
            "unique_submitters": len(self.by_submitter)
        }