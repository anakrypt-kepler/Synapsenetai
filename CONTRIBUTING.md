# Contributing to SynapseNet

Thank you for your interest in SynapseNet. Everyone is welcome to contribute, improve, and extend the project.

## Open Contribution

You may:

- **Add features** — New functionality, optimizations, integrations
- **Fix bugs** — Bug reports and patches are appreciated
- **Improve documentation** — Docs, comments, examples
- **Suggest enhancements** — Open issues, discuss design
- **Submit code** — Via pull requests, following the workflow below

## Consensus Rules (Immutable)

The **consensus rules** of SynapseNet were established by the creator **Kepler** and form the foundation of the protocol. These rules **must not be changed or violated** by contributors.

### What Are Consensus Rules?

Consensus rules define how the network agrees on:

- Proof of Emergence (PoE) validation logic
- Knowledge Chain structure and append rules
- NGT tokenomics and emission
- Protocol message formats and handshakes
- Cryptographic primitives and key derivation

### Why They Are Immutable

Changing consensus rules would create incompatible forks and break network unity. The rules were designed to ensure:

- Decentralization without central authority
- Fair rewards for knowledge contribution and validation
- Security against spam and Sybil attacks
- Compatibility across all nodes

### What You Can Do

- **Implement** — Build features that follow the consensus rules
- **Optimize** — Improve performance without changing semantics
- **Extend** — Add optional layers (privacy, quantum) that do not alter core consensus
- **Document** — Clarify how the rules work

### What You Must Not Do

- **Modify** — Change PoE logic, tokenomics, or protocol semantics
- **Remove** — Strip security checks or validation steps
- **Bypass** — Add backdoors or shortcuts around consensus
- **Fork** — Propose incompatible protocol changes as "improvements"

## How to Contribute

1. **Fork** the repository
2. **Create a branch** for your change (`git checkout -b feature/your-feature`)
3. **Make your changes** — Ensure they comply with the consensus rules
4. **Test** — Run existing tests, add new ones if needed
5. **Submit a pull request** — Describe what you changed and why

## Code Style

- Follow existing style in the codebase
- C++: match `KeplerSynapseNet` conventions
- Go: standard `gofmt`, `go vet`
- Keep commits focused and well-described

## Questions?

Open an issue for discussion. For consensus-related questions, refer to the design documents in `interfaces txt/`.

---

*"Intelligence belongs to everyone."* — Kepler
