#include "core/naan_harvest_plan.h"

#include <cassert>
#include <string>
#include <unordered_set>

using synapse::core::NaanHarvestLoopAction;
using synapse::core::decideNaanHarvestLoop;
using synapse::core::extractHarvestHrefs;
using synapse::core::isHarvestSearchSurface;
using synapse::core::isJunkHarvestHtml;
using synapse::core::naanClearnetSearchEngines;
using synapse::core::naanDarknetSearchEngines;
using synapse::core::pickNaanHarvestTarget;

static void testLoopActions() {
    assert(decideNaanHarvestLoop(true, false, 0, 100) == NaanHarvestLoopAction::StopUser);
    assert(decideNaanHarvestLoop(false, true, 0, 100) == NaanHarvestLoopAction::WaitTopic);
    assert(decideNaanHarvestLoop(false, false, 100, 100) == NaanHarvestLoopAction::WaitBudget);
    assert(decideNaanHarvestLoop(false, false, 0, 100) == NaanHarvestLoopAction::Fetch);
}

static void testClearnetNotFunneledToAhmia() {
    const std::string ahmia = "juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion";
    bool sawHttpsKnowledge = false;
    for (uint64_t tick = 0; tick < 12; ++tick) {
        auto t = pickNaanHarvestTarget("AI", tick);
        assert(t.url.find(ahmia) == std::string::npos);
        if (t.url.find("arxiv.org") != std::string::npos ||
            t.url.find("duckduckgo.com") != std::string::npos ||
            t.url.find("brave.com") != std::string::npos ||
            t.url.find("wikipedia.org") != std::string::npos) {
            sawHttpsKnowledge = true;
        }
    }
    assert(sawHttpsKnowledge);
}

static void testOnionRotation() {
    std::unordered_set<std::string> engines;
    const auto dark = naanDarknetSearchEngines();
    assert(dark.size() >= 4);
    for (uint64_t tick = 0; tick < dark.size(); ++tick) {
        engines.insert(pickNaanHarvestTarget("darknet", tick).url);
    }
    assert(engines.size() == dark.size());
}

static void testClearnetRoster() {
    std::string blob;
    for (const auto& e : naanClearnetSearchEngines()) blob += e + " ";
    assert(blob.find("duckduckgo.com") != std::string::npos);
    assert(blob.find("search.brave.com") != std::string::npos);
}

static void testJunkAndHrefs() {
    const std::string queue =
        "<html><body>dread Access QueuereadYou have been placed in a queue, "
        "awaiting forwarding to the platform.Your estimated entry time is soon"
        "</body></html>";
    assert(isJunkHarvestHtml(queue));
    assert(isJunkHarvestHtml("tiny"));
    const std::string ahmiaJs =
        std::string("<html><body>") + std::string(80, 'x') +
        "This site requires Javascript. We have not deployd non-javascript "
        "version of this site. Please enable javascript to use Ahmia."
        "</body></html>";
    assert(isJunkHarvestHtml(ahmiaJs));
    const std::string ok = std::string("<html>") + std::string(80, 'x') + " arxiv paper title list</html>";
    assert(!isJunkHarvestHtml(ok));

    const std::string html =
        "<a href=\"https://html.duckduckgo.com/html/?q=x\">ddg</a>"
        "<a href=\"https://arxiv.org/abs/2401.00001\">paper</a>"
        "<a href=\"https://en.wikipedia.org/wiki/Tor\">wiki</a>";
    auto hrefs = extractHarvestHrefs(html, 8);
    assert(hrefs.size() >= 2);
    bool sawArxiv = false;
    for (const auto& u : hrefs) {
        assert(u.find("duckduckgo.com") == std::string::npos);
        if (u.find("arxiv.org") != std::string::npos) sawArxiv = true;
    }
    assert(sawArxiv);
}

static void testSearchSurfaceAndRelativeHrefs() {
    assert(isHarvestSearchSurface("https://arxiv.org/list/cs.AI/recent"));
    assert(isHarvestSearchSurface("https://html.duckduckgo.com/html/?q=ai"));
    assert(isHarvestSearchSurface("https://search.brave.com/search?q=ai"));
    assert(!isHarvestSearchSurface("https://arxiv.org/abs/2401.00001"));
    assert(!isHarvestSearchSurface("https://en.wikipedia.org/wiki/Tor"));
    const std::string html =
        "<a href=\"/abs/2401.00001\">paper</a>"
        "<a href=\"https://html.duckduckgo.com/html/?q=x\">ddg</a>";
    auto hrefs = extractHarvestHrefs(html, 8, "https://arxiv.org/list/cs.AI/recent");
    bool sawAbs = false;
    for (const auto& u : hrefs) {
        assert(u.find("duckduckgo.com") == std::string::npos);
        if (u.find("/abs/2401.00001") != std::string::npos) sawAbs = true;
    }
    assert(sawAbs);
}

int main() {
    testLoopActions();
    testClearnetNotFunneledToAhmia();
    testOnionRotation();
    testClearnetRoster();
    testJunkAndHrefs();
    testSearchSurfaceAndRelativeHrefs();
    return 0;
}
