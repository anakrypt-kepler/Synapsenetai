#pragma once

// Proof of working code. A contributor ships a patch plus tests. Every cell
// compiles and runs that bundle in-process, with no clock, no files, and no
// network, then hashes the canonical transcript. Host compilers are not the
// consensus runner: their stdout is not the same on two machines.
// NGT for CODE is minted only when the tests pass and a majority of cells
// sign this same result hash.

#include "core/agent_runtime.h"
#include "core/poe_v1_objects.h"
#include "crypto/crypto.h"

#include <string>

namespace synapse::core {

struct WorkExecution {
    bool passed = false;
    std::string reason;
    std::string transcript;
    crypto::Hash256 resultHash{};
    uint32_t asserts = 0;
    uint32_t steps = 0;
};

// Canonical bundle. patch and tests are the two sections cells re-run.
std::string workBundle(const std::string& patch, const std::string& tests);

// Runs under a propose-only sandbox: PROPOSE_KNOWLEDGE, no side effects.
WorkExecution executeWork(const std::string& bundle);
WorkExecution executeWork(const std::string& bundle, const AgentRuntimeSandbox& sandbox);

std::string workVoteNote(const crypto::Hash256& resultHash);
bool parseWorkVoteNote(const std::string& note, crypto::Hash256* out);

// The note carries the full hash. The signed score prefix binds that hash
// into the vote payload, which does not otherwise cover the note trailer.
void bindWorkVote(poe_v1::ValidationVoteV1& vote, const crypto::Hash256& resultHash);
bool workVoteMatches(const poe_v1::ValidationVoteV1& vote, const crypto::Hash256& resultHash);

}
