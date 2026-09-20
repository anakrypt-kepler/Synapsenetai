#pragma once

// Harvest target policy for NAAN loops.
// Tor is the transport (SOCKS). It is not a reason to collapse every
// clearnet topic onto a single onion search engine.

#include <cstdint>
#include <string>
#include <vector>

namespace synapse {
namespace core {

enum class NaanHarvestLoopAction {
    Fetch,
    WaitTopic,
    WaitBudget,
    StopUser,
};

struct NaanHarvestTarget {
    std::string url;
    std::string engine;
    bool onion = false;
};

NaanHarvestLoopAction decideNaanHarvestLoop(bool userStop, bool topicsEmpty,
                                            double spent, double budget);

// Rotate search engines and keep direct clearnet pages (arxiv, cve, …).
// `tick` must change across loops so two agents do not always hit Ahmia.
NaanHarvestTarget pickNaanHarvestTarget(const std::string& topic, uint64_t tick);

std::vector<std::string> naanClearnetSearchEngines();
std::vector<std::string> naanDarknetSearchEngines();

bool isJunkHarvestHtml(const std::string& html);

// Search/catalog URLs (DDG, Brave, Wikipedia search, arXiv list, Ahmia, …).
// Filing those pages makes PoE SimHash too_similar and noteHash freeze.
bool isHarvestSearchSurface(const std::string& url);

// Pull result hrefs (absolute or site-relative) so the loop files the
// article, not the search UI. Skips engine/catalog URLs.
std::vector<std::string> extractHarvestHrefs(const std::string& html, size_t limit = 8,
                                             const std::string& baseUrl = {});

} // namespace core
} // namespace synapse
