#pragma once

// Deterministic usefulness for a NAAN harvest essay. Both cells run this on
// the entry body. No clock, no network, no model. A 403 page, a link catalog,
// and a page that misses its declared topic do not pass, so they do not mint.
// Ordinary TEXT that is not a harvest record is left to the existing PoE gates.

#include "core/poe_v1_objects.h"

#include <string>

namespace synapse::core {

struct TextUsefulness {
    bool passed = false;
    bool harvest = false;
    std::string reason;
    uint16_t words = 0;
    uint16_t uniqueWords = 0;
    uint16_t topicHits = 0;
};

bool isHarvestKnowledge(const std::string& body);

// Topic lexicon is taken from the topic line stored on the record.
TextUsefulness assessTextUsefulness(const std::string& title, const std::string& body);

// Topic lexicon is the caller-supplied topic. A topic line inside the page
// does not select the lexicon. Scored text stops before the caller's own
// topic trailer, the last matching topic line placed in front of "url:".
TextUsefulness assessTextUsefulness(const std::string& title, const std::string& body,
                                    const std::string& topic);

void bindUsefulnessVote(poe_v1::ValidationVoteV1& vote, const TextUsefulness& use);
bool usefulnessVoteMatches(const poe_v1::ValidationVoteV1& vote, const TextUsefulness& use);

} // namespace synapse::core
