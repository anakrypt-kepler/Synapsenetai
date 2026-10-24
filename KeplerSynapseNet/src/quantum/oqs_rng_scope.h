#pragma once

// One process-wide liboqs RNG hook. liboqs 0.12 has a single function
// pointer and no derandomized keygen, so deterministic keygen arms this
// hook for that call and then puts the system generator back.
// Another thread never reads the coin stream and is never given zeros.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace synapse::quantum::detail {

class OqsDeterministicRngGuard {
public:
    explicit OqsDeterministicRngGuard(std::vector<uint8_t> stream);
    ~OqsDeterministicRngGuard();

    OqsDeterministicRngGuard(const OqsDeterministicRngGuard&) = delete;
    OqsDeterministicRngGuard& operator=(const OqsDeterministicRngGuard&) = delete;

    // True when this thread's stream supplied at least minCoins and did not run short.
    bool accepted(size_t minCoins) const;

private:
    struct Lock;
    Lock* lock_ = nullptr;
    std::vector<uint8_t> prevStream_;
    size_t prevPos_ = 0;
    bool prevActive_ = false;
    bool prevUnderflow_ = false;
    bool armed_ = false;
};

} // namespace synapse::quantum::detail
