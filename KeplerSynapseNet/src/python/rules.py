from typing import Dict, List, Optional, Callable
from dataclasses import dataclass
from enum import Enum
import re
import hashlib

class RuleType(Enum):
    CONTENT = "content"
    STAKE = "stake"
    TIMING = "timing"
    REPUTATION = "reputation"
    CATEGORY = "category"
    RATE_LIMIT = "rate_limit"

class RuleSeverity(Enum):
    INFO = 0
    WARNING = 1
    ERROR = 2
    CRITICAL = 3

@dataclass
class RuleViolation:
    rule_id: str
    rule_type: RuleType
    severity: RuleSeverity
    message: str
    details: Dict

@dataclass
class ValidationRule:
    rule_id: str
    rule_type: RuleType
    name: str
    description: str
    severity: RuleSeverity
    enabled: bool
    check_fn: Callable

class RuleEngine:
    def __init__(self):
        self.rules: Dict[str, ValidationRule] = {}
        self.violations: List[RuleViolation] = []
        self._register_default_rules()
    
    def _register_default_rules(self):
        self.register_rule(ValidationRule(
            rule_id="min_content_length",
            rule_type=RuleType.CONTENT,
            name="Minimum Content Length",
            description="Content must be at least 50 characters",
            severity=RuleSeverity.ERROR,
            enabled=True,
            check_fn=lambda ctx: len(ctx.get("content", "")) >= 50
        ))
        
        self.register_rule(ValidationRule(
            rule_id="max_content_length",
            rule_type=RuleType.CONTENT,
            name="Maximum Content Length",
            description="Content must not exceed 100000 characters",
            severity=RuleSeverity.ERROR,
            enabled=True,
            check_fn=lambda ctx: len(ctx.get("content", "")) <= 100000
        ))
        
        self.register_rule(ValidationRule(
            rule_id="min_stake",
            rule_type=RuleType.STAKE,
            name="Minimum Stake",
            description="Submission requires minimum 10 NGT stake",
            severity=RuleSeverity.CRITICAL,
            enabled=True,
            check_fn=lambda ctx: ctx.get("stake", 0) >= 10
        ))
        
        self.register_rule(ValidationRule(
            rule_id="validator_min_stake",
            rule_type=RuleType.STAKE,
            name="Validator Minimum Stake",
            description="Validators require minimum 100 NGT stake",
            severity=RuleSeverity.CRITICAL,
            enabled=True,
            check_fn=lambda ctx: ctx.get("validator_stake", 0) >= 100
        ))
        
        self.register_rule(ValidationRule(
            rule_id="no_spam_patterns",
            rule_type=RuleType.CONTENT,
            name="No Spam Patterns",
            description="Content must not contain spam patterns",
            severity=RuleSeverity.ERROR,
            enabled=True,
            check_fn=self._check_no_spam
        ))
        
        self.register_rule(ValidationRule(
            rule_id="valid_category",
            rule_type=RuleType.CATEGORY,
            name="Valid Category",
            description="Category must be one of the allowed types",
            severity=RuleSeverity.ERROR,
            enabled=True,
            check_fn=self._check_valid_category
        ))
        
        self.register_rule(ValidationRule(
            rule_id="rate_limit_submissions",
            rule_type=RuleType.RATE_LIMIT,
            name="Submission Rate Limit",
            description="Maximum 10 submissions per hour per address",
            severity=RuleSeverity.WARNING,
            enabled=True,
            check_fn=lambda ctx: ctx.get("submissions_last_hour", 0) < 10
        ))
        
        self.register_rule(ValidationRule(
            rule_id="min_reputation",
            rule_type=RuleType.REPUTATION,
            name="Minimum Reputation",
            description="Submitter must have reputation >= 0.1",
            severity=RuleSeverity.WARNING,
            enabled=True,
            check_fn=lambda ctx: ctx.get("reputation", 0.5) >= 0.1
        ))
        
        self.register_rule(ValidationRule(
            rule_id="no_duplicate_content",
            rule_type=RuleType.CONTENT,
            name="No Duplicate Content",
            description="Content hash must be unique",
            severity=RuleSeverity.ERROR,
            enabled=True,
            check_fn=lambda ctx: not ctx.get("is_duplicate", False)
        ))
        
        self.register_rule(ValidationRule(
            rule_id="valid_encoding",
            rule_type=RuleType.CONTENT,
            name="Valid Encoding",
            description="Content must be valid UTF-8",
            severity=RuleSeverity.ERROR,
            enabled=True,
            check_fn=self._check_valid_encoding
        ))
    
    def _check_no_spam(self, ctx: Dict) -> bool:
        content = ctx.get("content", "")
        spam_patterns = [
            r'(.)\1{10,}',
            r'(buy|sell|click|free|winner)',
            r'[A-Z]{20,}',
            r'(.{5,})\1{3,}'
        ]
        for pattern in spam_patterns:
            if re.search(pattern, content, re.I):
                return False
        return True
    
    def _check_valid_category(self, ctx: Dict) -> bool:
        valid_categories = [
            "factual", "procedural", "conceptual",
            "metacognitive", "creative", "technical"
        ]
        return ctx.get("category", "").lower() in valid_categories
    
    def _check_valid_encoding(self, ctx: Dict) -> bool:
        content = ctx.get("content", "")
        try:
            if isinstance(content, bytes):
                content.decode("utf-8")
            return True
        except:
            return False
    
    def register_rule(self, rule: ValidationRule):
        self.rules[rule.rule_id] = rule
    
    def unregister_rule(self, rule_id: str):
        self.rules.pop(rule_id, None)
    
    def enable_rule(self, rule_id: str):
        if rule_id in self.rules:
            self.rules[rule_id].enabled = True
    
    def disable_rule(self, rule_id: str):
        if rule_id in self.rules:
            self.rules[rule_id].enabled = False
    
    def validate(self, context: Dict) -> List[RuleViolation]:
        violations = []
        
        for rule_id, rule in self.rules.items():
            if not rule.enabled:
                continue
            
            try:
                passed = rule.check_fn(context)
                if not passed:
                    violation = RuleViolation(
                        rule_id=rule_id,
                        rule_type=rule.rule_type,
                        severity=rule.severity,
                        message=rule.description,
                        details={"context_keys": list(context.keys())}
                    )
                    violations.append(violation)
            except Exception as e:
                violation = RuleViolation(
                    rule_id=rule_id,
                    rule_type=rule.rule_type,
                    severity=RuleSeverity.ERROR,
                    message=f"Rule check failed: {str(e)}",
                    details={"error": str(e)}
                )
                violations.append(violation)
        
        self.violations = violations
        return violations
    
    def is_valid(self, context: Dict) -> bool:
        violations = self.validate(context)
        critical_or_error = [
            v for v in violations
            if v.severity in [RuleSeverity.CRITICAL, RuleSeverity.ERROR]
        ]
        return len(critical_or_error) == 0
    
    def get_violations_by_severity(self, severity: RuleSeverity) -> List[RuleViolation]:
        return [v for v in self.violations if v.severity == severity]
    
    def get_violations_by_type(self, rule_type: RuleType) -> List[RuleViolation]:
        return [v for v in self.violations if v.rule_type == rule_type]
    
    def get_rule_stats(self) -> Dict:
        return {
            "total_rules": len(self.rules),
            "enabled_rules": sum(1 for r in self.rules.values() if r.enabled),
            "rules_by_type": {
                rt.value: sum(1 for r in self.rules.values() if r.rule_type == rt)
                for rt in RuleType
            },
            "rules_by_severity": {
                rs.name: sum(1 for r in self.rules.values() if r.severity == rs)
                for rs in RuleSeverity
            }
        }
    
    def export_rules(self) -> List[Dict]:
        return [
            {
                "rule_id": r.rule_id,
                "rule_type": r.rule_type.value,
                "name": r.name,
                "description": r.description,
                "severity": r.severity.name,
                "enabled": r.enabled
            }
            for r in self.rules.values()
        ]


class ConsensusRules:
    VALIDATORS_REQUIRED = 5
    ACCEPTANCE_THRESHOLD = 3
    VOTE_TIMEOUT_SECONDS = 3600
    MAX_PENDING_SUBMISSIONS = 1000
    
    @staticmethod
    def check_quorum(votes: int) -> bool:
        return votes >= ConsensusRules.VALIDATORS_REQUIRED
    
    @staticmethod
    def check_acceptance(accept_votes: int) -> bool:
        return accept_votes >= ConsensusRules.ACCEPTANCE_THRESHOLD
    
    @staticmethod
    def calculate_reward(base_reward: int, quality_score: float, 
                         network_load: float) -> int:
        quality_factor = quality_score / 100.0
        load_factor = 1.0 / (1.0 + network_load)
        reward = int(base_reward * quality_factor * load_factor)
        return max(1, reward)
    
    @staticmethod
    def calculate_penalty(stake: int, severity: str) -> int:
        penalties = {
            "spam": 1.0,
            "duplicate": 0.5,
            "low_quality": 0.2,
            "invalid": 0.3
        }
        factor = penalties.get(severity, 0.1)
        return int(stake * factor)