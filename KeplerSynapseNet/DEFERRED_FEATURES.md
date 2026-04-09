# Deferred Features

## DHT / Kademlia Peer Discovery

**Status:** Deferred  
**Decision Date:** March 2026  
**Reason:**  
No DHT or Kademlia implementation exists in the current source tree, tests, or any active branch. The existing peer discovery system uses DNS seeds, bootstrap nodes, and peer exchange, which covers the current network requirements.

DHT-based discovery may be revisited when the network grows past the point where static seed lists and peer exchange become a bottleneck. Until then, this is not active work and should not be presented as a shipped or near-term feature.

**Prerequisite for future implementation:**
- Abuse controls (Sybil resistance, eclipse attack mitigation)
- Rollout behind a feature flag
- Integration tests with the existing discovery subsystem
- Operator documentation for mixed-mode discovery
