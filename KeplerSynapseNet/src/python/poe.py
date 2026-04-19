import hashlib
import random
import time
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass
from enum import Enum

class VoteType(Enum):
    ACCEPT = 1
    REJECT = 2
    ABSTAIN = 3

class KnowledgeCategory(Enum):
    FACTUAL = "factual"
    PROCEDURAL = "procedural"
    CONCEPTUAL = "conceptual"
    METACOGNITIVE = "metacognitive"
    CREATIVE = "creative"
    TECHNICAL = "technical"

@dataclass
class KnowledgeSubmission:
    submission_id: str
    submitter_address: str
    content_hash: str
    category: KnowledgeCategory
    stake_amount: int
    timestamp: int
    metadata: Dict

@dataclass
class ValidationVote:
    validator_address: str
    submission_id: str
    vote: VoteType
    quality_score: int
    timestamp: int
    signature: bytes

@dataclass
class ValidationResult:
    submission_id: str
    accepted: bool
    total_votes: int
    accept_votes: int
    reject_votes: int
    abstain_votes: int
    average_score: float
    reward_amount: int
    validators: List[str]

class ProofOfEmergence:
    MIN_STAKE_SUBMIT = 10
    MIN_STAKE_VALIDATE = 100
    VALIDATORS_PER_SUBMISSION = 5
    ACCEPTANCE_THRESHOLD = 3
    BASE_REWARD = 100
    MAX_QUALITY_SCORE = 100
    
    def __init__(self):
        self.pending_submissions: Dict[str, KnowledgeSubmission] = {}
        self.votes: Dict[str, List[ValidationVote]] = {}
        self.results: Dict[str, ValidationResult] = {}
        self.validator_stakes: Dict[str, int] = {}
        self.validator_reputation: Dict[str, float] = {}
        self.network_load = 1.0
        self.submissions_per_hour = 0
        self.baseline_submissions = 100
    
    def submit_knowledge(self, submitter: str, content_hash: str, 
                         category: str, stake: int, metadata: Dict = None) -> Optional[str]:
        if stake < self.MIN_STAKE_SUBMIT:
            return None
        
        submission_id = self._generate_submission_id(submitter, content_hash)
        
        submission = KnowledgeSubmission(
            submission_id=submission_id,
            submitter_address=submitter,
            content_hash=content_hash,
            category=KnowledgeCategory(category) if category in [c.value for c in KnowledgeCategory] else KnowledgeCategory.FACTUAL,
            stake_amount=stake,
            timestamp=int(time.time()),
            metadata=metadata or {}
        )
        
        self.pending_submissions[submission_id] = submission
        self.votes[submission_id] = []
        self._update_network_load()
        
        return submission_id
    
    def select_validators(self, submission_id: str) -> List[str]:
        if submission_id not in self.pending_submissions:
            return []
        
        eligible = [
            addr for addr, stake in self.validator_stakes.items()
            if stake >= self.MIN_STAKE_VALIDATE
        ]
        
        if len(eligible) < self.VALIDATORS_PER_SUBMISSION:
            return eligible
        
        seed = int(hashlib.sha256(submission_id.encode()).hexdigest(), 16)
        random.seed(seed)
        
        weights = []
        for addr in eligible:
            stake_weight = self.validator_stakes.get(addr, 0) / 1000
            rep_weight = self.validator_reputation.get(addr, 0.5)
            weights.append(stake_weight * rep_weight)
        
        total_weight = sum(weights)
        if total_weight == 0:
            return random.sample(eligible, min(self.VALIDATORS_PER_SUBMISSION, len(eligible)))
        
        normalized = [w / total_weight for w in weights]
        
        selected = []
        remaining = list(zip(eligible, normalized))
        
        for _ in range(min(self.VALIDATORS_PER_SUBMISSION, len(eligible))):
            if not remaining:
                break
            
            r = random.random()
            cumulative = 0
            for i, (addr, weight) in enumerate(remaining):
                cumulative += weight
                if r <= cumulative:
                    selected.append(addr)
                    remaining.pop(i)
                    total = sum(w for _, w in remaining)
                    if total > 0:
                        remaining = [(a, w/total) for a, w in remaining]
                    break
        
        return selected
    
    def cast_vote(self, validator: str, submission_id: str, 
                  vote: int, quality_score: int, signature: bytes) -> bool:
        if submission_id not in self.pending_submissions:
            return False
        
        if self.validator_stakes.get(validator, 0) < self.MIN_STAKE_VALIDATE:
            return False
        
        existing_votes = [v for v in self.votes[submission_id] if v.validator_address == validator]
        if existing_votes:
            return False
        
        quality_score = max(0, min(self.MAX_QUALITY_SCORE, quality_score))
        
        validation_vote = ValidationVote(
            validator_address=validator,
            submission_id=submission_id,
            vote=VoteType(vote),
            quality_score=quality_score,
            timestamp=int(time.time()),
            signature=signature
        )
        
        self.votes[submission_id].append(validation_vote)
        
        if len(self.votes[submission_id]) >= self.VALIDATORS_PER_SUBMISSION:
            self._finalize_validation(submission_id)
        
        return True
    
    def _finalize_validation(self, submission_id: str) -> ValidationResult:
        if submission_id not in self.pending_submissions:
            return None
        
        submission = self.pending_submissions[submission_id]
        votes = self.votes[submission_id]
        
        accept_votes = sum(1 for v in votes if v.vote == VoteType.ACCEPT)
        reject_votes = sum(1 for v in votes if v.vote == VoteType.REJECT)
        abstain_votes = sum(1 for v in votes if v.vote == VoteType.ABSTAIN)
        
        accepted = accept_votes >= self.ACCEPTANCE_THRESHOLD
        
        accept_scores = [v.quality_score for v in votes if v.vote == VoteType.ACCEPT]
        average_score = sum(accept_scores) / len(accept_scores) if accept_scores else 0
        
        reward = 0
        if accepted:
            quality_multiplier = average_score / self.MAX_QUALITY_SCORE
            load_factor = 1 / (1 + self.network_load)
            reward = int(self.BASE_REWARD * quality_multiplier * load_factor)
            reward = max(1, reward)
        
        result = ValidationResult(
            submission_id=submission_id,
            accepted=accepted,
            total_votes=len(votes),
            accept_votes=accept_votes,
            reject_votes=reject_votes,
            abstain_votes=abstain_votes,
            average_score=average_score,
            reward_amount=reward,
            validators=[v.validator_address for v in votes]
        )
        
        self.results[submission_id] = result
        
        self._update_validator_reputation(votes, accepted)
        
        del self.pending_submissions[submission_id]
        
        return result
    
    def _update_validator_reputation(self, votes: List[ValidationVote], accepted: bool):
        majority_vote = VoteType.ACCEPT if accepted else VoteType.REJECT
        
        for vote in votes:
            addr = vote.validator_address
            current_rep = self.validator_reputation.get(addr, 0.5)
            
            if vote.vote == majority_vote:
                new_rep = min(1.0, current_rep + 0.01)
            elif vote.vote == VoteType.ABSTAIN:
                new_rep = current_rep
            else:
                new_rep = max(0.1, current_rep - 0.02)
            
            self.validator_reputation[addr] = new_rep
    
    def _update_network_load(self):
        self.submissions_per_hour += 1
        self.network_load = self.submissions_per_hour / self.baseline_submissions
    
    def _generate_submission_id(self, submitter: str, content_hash: str) -> str:
        data = f"{submitter}:{content_hash}:{time.time()}:{random.random()}"
        return hashlib.sha256(data.encode()).hexdigest()[:32]
    
    def register_validator(self, address: str, stake: int) -> bool:
        if stake < self.MIN_STAKE_VALIDATE:
            return False
        
        self.validator_stakes[address] = stake
        if address not in self.validator_reputation:
            self.validator_reputation[address] = 0.5
        
        return True
    
    def unregister_validator(self, address: str) -> int:
        stake = self.validator_stakes.pop(address, 0)
        return stake
    
    def get_submission_status(self, submission_id: str) -> Dict:
        if submission_id in self.results:
            result = self.results[submission_id]
            return {
                "status": "finalized",
                "accepted": result.accepted,
                "reward": result.reward_amount,
                "score": result.average_score
            }
        
        if submission_id in self.pending_submissions:
            votes = self.votes.get(submission_id, [])
            return {
                "status": "pending",
                "votes_received": len(votes),
                "votes_needed": self.VALIDATORS_PER_SUBMISSION
            }
        
        return {"status": "not_found"}
    
    def get_validator_stats(self, address: str) -> Dict:
        return {
            "stake": self.validator_stakes.get(address, 0),
            "reputation": self.validator_reputation.get(address, 0),
            "is_eligible": self.validator_stakes.get(address, 0) >= self.MIN_STAKE_VALIDATE
        }
    
    def get_network_stats(self) -> Dict:
        return {
            "pending_submissions": len(self.pending_submissions),
            "total_validators": len(self.validator_stakes),
            "eligible_validators": sum(1 for s in self.validator_stakes.values() if s >= self.MIN_STAKE_VALIDATE),
            "network_load": self.network_load,
            "submissions_per_hour": self.submissions_per_hour
        }
    
    def calculate_reward(self, quality_score: float, category: str) -> int:
        category_multipliers = {
            "factual": 1.0,
            "procedural": 1.2,
            "conceptual": 1.3,
            "metacognitive": 1.5,
            "creative": 1.4,
            "technical": 1.3
        }
        
        multiplier = category_multipliers.get(category, 1.0)
        quality_factor = quality_score / self.MAX_QUALITY_SCORE
        load_factor = 1 / (1 + self.network_load)
        
        reward = int(self.BASE_REWARD * multiplier * quality_factor * load_factor)
        return max(1, reward)