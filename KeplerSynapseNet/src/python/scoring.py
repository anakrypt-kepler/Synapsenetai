import hashlib
import re
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass
from enum import Enum

class QualityDimension(Enum):
    ACCURACY = "accuracy"
    COMPLETENESS = "completeness"
    CLARITY = "clarity"
    RELEVANCE = "relevance"
    NOVELTY = "novelty"
    VERIFIABILITY = "verifiability"
    COHERENCE = "coherence"
    DEPTH = "depth"

@dataclass
class QualityScore:
    dimension: QualityDimension
    score: float
    confidence: float
    evidence: str

@dataclass
class ContentAnalysis:
    word_count: int
    sentence_count: int
    avg_sentence_length: float
    unique_words: int
    vocabulary_richness: float
    has_citations: bool
    has_code: bool
    has_data: bool
    language_detected: str

class KnowledgeScorer:
    MAX_SCORE = 100
    MIN_SCORE = 0
    
    DIMENSION_WEIGHTS = {
        QualityDimension.ACCURACY: 0.25,
        QualityDimension.COMPLETENESS: 0.15,
        QualityDimension.CLARITY: 0.15,
        QualityDimension.RELEVANCE: 0.15,
        QualityDimension.NOVELTY: 0.10,
        QualityDimension.VERIFIABILITY: 0.10,
        QualityDimension.COHERENCE: 0.05,
        QualityDimension.DEPTH: 0.05
    }
    
    def __init__(self):
        self.known_hashes: set = set()
        self.category_baselines: Dict[str, float] = {}
        self.spam_patterns: List[str] = [
            r'(.)\1{10,}',
            r'(buy|sell|click|free|winner)',
            r'[A-Z]{20,}',
            r'(.{5,})\1{3,}'
        ]
    
    def analyze_content(self, content: str) -> ContentAnalysis:
        words = content.split()
        word_count = len(words)
        
        sentences = re.split(r'[.!?]+', content)
        sentences = [s.strip() for s in sentences if s.strip()]
        sentence_count = len(sentences)
        
        avg_sentence_length = word_count / sentence_count if sentence_count > 0 else 0
        
        unique_words = len(set(w.lower() for w in words))
        vocabulary_richness = unique_words / word_count if word_count > 0 else 0
        
        has_citations = bool(re.search(r'\[\d+\]|\(\d{4}\)|doi:|arxiv:', content, re.I))
        has_code = bool(re.search(r'```|def |class |function |import |#include', content))
        has_data = bool(re.search(r'\d+\.\d+|\d+%|table|figure|chart', content, re.I))
        
        language_detected = "en"
        
        return ContentAnalysis(
            word_count=word_count,
            sentence_count=sentence_count,
            avg_sentence_length=avg_sentence_length,
            unique_words=unique_words,
            vocabulary_richness=vocabulary_richness,
            has_citations=has_citations,
            has_code=has_code,
            has_data=has_data,
            language_detected=language_detected
        )
    
    def score_accuracy(self, content: str, category: str, metadata: Dict) -> QualityScore:
        score = 50.0
        confidence = 0.5
        evidence = ""
        
        analysis = self.analyze_content(content)
        
        if analysis.has_citations:
            score += 15
            evidence += "Has citations. "
            confidence += 0.1
        
        if analysis.has_data:
            score += 10
            evidence += "Contains data. "
            confidence += 0.05
        
        if metadata.get("verified_source"):
            score += 20
            evidence += "Verified source. "
            confidence += 0.2
        
        if self._check_spam(content):
            score = max(0, score - 40)
            evidence += "Spam detected. "
            confidence = 0.9
        
        return QualityScore(
            dimension=QualityDimension.ACCURACY,
            score=min(100, max(0, score)),
            confidence=min(1.0, confidence),
            evidence=evidence.strip()
        )
    
    def score_completeness(self, content: str, category: str) -> QualityScore:
        analysis = self.analyze_content(content)
        score = 50.0
        evidence = ""
        
        if analysis.word_count < 50:
            score = 20
            evidence = "Too short. "
        elif analysis.word_count < 200:
            score = 40
            evidence = "Brief content. "
        elif analysis.word_count < 500:
            score = 60
            evidence = "Moderate length. "
        elif analysis.word_count < 1000:
            score = 80
            evidence = "Comprehensive. "
        else:
            score = 90
            evidence = "Very detailed. "
        
        if analysis.has_code and category == "technical":
            score += 10
            evidence += "Includes code examples. "
        
        return QualityScore(
            dimension=QualityDimension.COMPLETENESS,
            score=min(100, score),
            confidence=0.7,
            evidence=evidence.strip()
        )
    
    def score_clarity(self, content: str) -> QualityScore:
        analysis = self.analyze_content(content)
        score = 50.0
        evidence = ""
        
        if 10 <= analysis.avg_sentence_length <= 20:
            score += 20
            evidence = "Good sentence length. "
        elif analysis.avg_sentence_length > 30:
            score -= 15
            evidence = "Sentences too long. "
        elif analysis.avg_sentence_length < 5:
            score -= 10
            evidence = "Sentences too short. "
        
        if analysis.vocabulary_richness > 0.6:
            score += 15
            evidence += "Rich vocabulary. "
        elif analysis.vocabulary_richness < 0.3:
            score -= 10
            evidence += "Repetitive vocabulary. "
        
        return QualityScore(
            dimension=QualityDimension.CLARITY,
            score=min(100, max(0, score)),
            confidence=0.6,
            evidence=evidence.strip()
        )
    
    def score_novelty(self, content: str, content_hash: str) -> QualityScore:
        score = 70.0
        evidence = ""
        
        if content_hash in self.known_hashes:
            score = 0
            evidence = "Duplicate content. "
        else:
            self.known_hashes.add(content_hash)
            evidence = "New content. "
        
        return QualityScore(
            dimension=QualityDimension.NOVELTY,
            score=score,
            confidence=0.9,
            evidence=evidence.strip()
        )
    
    def score_verifiability(self, content: str, metadata: Dict) -> QualityScore:
        analysis = self.analyze_content(content)
        score = 40.0
        evidence = ""
        
        if analysis.has_citations:
            score += 30
            evidence = "Has citations. "
        
        if metadata.get("sources"):
            score += 20
            evidence += "Sources provided. "
        
        if analysis.has_data:
            score += 10
            evidence += "Contains verifiable data. "
        
        return QualityScore(
            dimension=QualityDimension.VERIFIABILITY,
            score=min(100, score),
            confidence=0.7,
            evidence=evidence.strip()
        )
    
    def _check_spam(self, content: str) -> bool:
        for pattern in self.spam_patterns:
            if re.search(pattern, content, re.I):
                return True
        return False
    
    def calculate_total_score(self, scores: List[QualityScore]) -> Tuple[float, float]:
        if not scores:
            return 0.0, 0.0
        
        weighted_sum = 0.0
        weight_sum = 0.0
        confidence_sum = 0.0
        
        for score in scores:
            weight = self.DIMENSION_WEIGHTS.get(score.dimension, 0.1)
            weighted_sum += score.score * weight * score.confidence
            weight_sum += weight * score.confidence
            confidence_sum += score.confidence
        
        total_score = weighted_sum / weight_sum if weight_sum > 0 else 0
        avg_confidence = confidence_sum / len(scores)
        
        return total_score, avg_confidence
    
    def score_knowledge(self, content: str, category: str, 
                        content_hash: str, metadata: Dict = None) -> Dict:
        metadata = metadata or {}
        
        scores = [
            self.score_accuracy(content, category, metadata),
            self.score_completeness(content, category),
            self.score_clarity(content),
            self.score_novelty(content, content_hash),
            self.score_verifiability(content, metadata)
        ]
        
        total_score, confidence = self.calculate_total_score(scores)
        
        return {
            "total_score": round(total_score, 2),
            "confidence": round(confidence, 2),
            "dimensions": {
                s.dimension.value: {
                    "score": round(s.score, 2),
                    "confidence": round(s.confidence, 2),
                    "evidence": s.evidence
                }
                for s in scores
            },
            "is_spam": self._check_spam(content),
            "recommendation": self._get_recommendation(total_score)
        }
    
    def _get_recommendation(self, score: float) -> str:
        if score >= 80:
            return "ACCEPT"
        elif score >= 60:
            return "ACCEPT_WITH_REVIEW"
        elif score >= 40:
            return "NEEDS_IMPROVEMENT"
        else:
            return "REJECT"
    
    def batch_score(self, submissions: List[Dict]) -> List[Dict]:
        results = []
        for sub in submissions:
            result = self.score_knowledge(
                content=sub.get("content", ""),
                category=sub.get("category", "factual"),
                content_hash=sub.get("hash", ""),
                metadata=sub.get("metadata", {})
            )
            result["submission_id"] = sub.get("id")
            results.append(result)
        return results
    
    def get_scoring_stats(self) -> Dict:
        return {
            "known_content_count": len(self.known_hashes),
            "dimension_weights": {d.value: w for d, w in self.DIMENSION_WEIGHTS.items()},
            "spam_patterns_count": len(self.spam_patterns)
        }


class SpamDetector:
    def __init__(self):
        self.patterns = [
            r'(.)\1{10,}',
            r'(buy|sell|click|free|winner|prize)',
            r'[A-Z]{20,}',
            r'(.{5,})\1{3,}',
            r'http[s]?://[^\s]{100,}',
            r'\$\d+[,\d]*\s*(million|billion)?',
            r'(urgent|act now|limited time)',
            r'[!]{3,}'
        ]
        self.threshold = 0.3
    
    def detect(self, content: str) -> Tuple[bool, float, List[str]]:
        matches = []
        for pattern in self.patterns:
            if re.search(pattern, content, re.I):
                matches.append(pattern)
        
        spam_score = len(matches) / len(self.patterns)
        is_spam = spam_score >= self.threshold
        
        return is_spam, spam_score, matches
    
    def add_pattern(self, pattern: str):
        self.patterns.append(pattern)
    
    def set_threshold(self, threshold: float):
        self.threshold = max(0.0, min(1.0, threshold))