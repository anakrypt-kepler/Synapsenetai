#include "core/naan_harvest_plan.h"
#include "core/text_usefulness.h"

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
    const std::string forbidden =
        std::string("<html><title>Error 403 (Forbidden)!!1</title><body>") +
        "403. That's an error. Your client does not have permission.</body></html>";
    assert(isJunkHarvestHtml(forbidden));
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

static std::string harvestBody(const std::string& topic, const std::string& excerpt) {
    return excerpt + "\n\ntopic: " + topic +
        "\nurl: https://example.test/page\nvia: tor_clearnet\nsha256: abc\n";
}

static void testUsefulness() {
    using synapse::core::assessTextUsefulness;
    const std::string torEssay =
        "A Tor circuit is a path of relays. The Tor browser uses onion routing so the "
        "destination sees an exit relay instead of the user. Anonymity depends on more "
        "than one relay in the circuit. Privacy research studies how a hidden service "
        "publishes an onion address. Censorship circumvention is one reason people run "
        "a relay. This note explains the circuit, the relay, and the browser together.";
    auto ok = assessTextUsefulness("NAAN harvest: darknet", harvestBody("darknet", torEssay));
    assert(ok.harvest);
    assert(ok.passed);
    assert(ok.topicHits >= 2);

    const std::string err =
        "Error 403 (Forbidden)!!1 403. That's an error. Your client does not have "
        "permission to get URL / from this server. That's all we know.";
    auto bad = assessTextUsefulness("NAAN harvest: AI", harvestBody("AI", err));
    assert(!bad.passed);
    assert(bad.reason == "error_page");

    std::string catalog = "Onion directory listings for the Tor browser and privacy tools. ";
    for (int i = 0; i < 6; ++i) catalog += "shop" + std::to_string(i) + ".onion ";
    auto farm = assessTextUsefulness("NAAN harvest: darknet", harvestBody("darknet", catalog));
    assert(!farm.passed);
    assert(farm.reason == "link_catalog");

    const std::string wiki =
        "Welcome. The Wikimedia movement is a global community of people, projects, and "
        "activities working together to create and share knowledge freely. Join us in "
        "making all knowledge available to everyone, everywhere. Projects include an "
        "encyclopedia, a dictionary, a media collection, and a free travel guide. "
        "Readers write articles, fix spelling, and add pictures every day of the week.";
    auto miss = assessTextUsefulness("NAAN harvest: crypto", harvestBody("crypto", wiki));
    assert(!miss.passed);
    assert(miss.reason == "topic_mismatch");

    const std::string history =
        "A history of the directory project. Storage grew as contributors wrote editorial "
        "notes about the monitor and the vector of each collection. The history directory "
        "holds stories, maps, and pictures. Readers browse the history and the directory "
        "again. Storage, history, directory, contributor, editorial, monitor, and vector "
        "are the words of this page. None of them name a hidden path or a hop.";
    auto buried = assessTextUsefulness("NAAN harvest: darknet", harvestBody("darknet", history));
    assert(!buried.passed);
    assert(buried.reason == "topic_mismatch");

    auto plain = assessTextUsefulness("plain_text_title", std::string(80, 't'));
    assert(!plain.harvest);
    assert(plain.passed);
}

static std::string poeBody(const std::string& topic, const std::string& excerpt) {
    return excerpt + "\n\ntopic: " + topic +
        "\nurl: https://example.test/page\ntitle: t\nvia: tor_clearnet\nsha256: abc\n";
}

static void testExcerptSkipsMenu() {
    using synapse::core::assessTextUsefulness;
    using synapse::core::harvestKnowledgeExcerpt;
    std::string menu;
    for (int i = 0; i < 250; ++i) menu += "English\n";
    const std::string article =
        "The Tor browser uses onion routing so a destination sees an exit relay. "
        "Anonymity depends on more than one relay in the circuit. Privacy research "
        "studies censorship circumvention and how a hidden service publishes an address. "
        "This note explains the browser, the relay, and the circuit together in detail.";
    const std::string page = menu + article;
    const std::string excerpt = harvestKnowledgeExcerpt(page, 1800);
    assert(excerpt.find("onion routing") != std::string::npos);
    assert(excerpt.find("English") == std::string::npos);
    auto passed = assessTextUsefulness("NAAN harvest: darknet", poeBody("darknet", excerpt));
    assert(passed.passed);
    assert(passed.reason == "ok");
    const std::string prefix = page.substr(0, 1800);
    auto missed = assessTextUsefulness("NAAN harvest: darknet", poeBody("darknet", prefix));
    assert(!missed.passed);

    const std::string essay =
        "A Tor circuit is a path of relays. The Tor browser uses onion routing so the "
        "destination sees an exit relay instead of the user. Anonymity depends on more "
        "than one relay in the circuit.";
    assert(harvestKnowledgeExcerpt(essay, 1800) == essay);

    std::string buried;
    while (buried.size() < 7000) buried += "nav\n";
    buried += article;
    const std::string capped = harvestKnowledgeExcerpt(buried, 1800);
    assert(capped.find("onion routing") == std::string::npos);

    const std::string err =
        "Error 403 (Forbidden)!!1 403. That's an error. Your client does not have "
        "permission to get URL / from this server. That's all we know.";
    auto stillBad = assessTextUsefulness("NAAN harvest: AI", poeBody("AI", harvestKnowledgeExcerpt(err, 1800)));
    assert(!stillBad.passed);
}

static void testNamedEngine() {
    using synapse::core::pickNaanHarvestTarget;
    auto brave = pickNaanHarvestTarget("crypto", 1, "brave");
    assert(brave.url.find("search.brave.com") != std::string::npos);
    assert(brave.engine == "brave");
    assert(!brave.onion);
    auto torch = pickNaanHarvestTarget("darknet", 4, "torch");
    assert(torch.url.find("torchdeed") != std::string::npos);
    assert(torch.engine == "torch");
    assert(torch.onion);
    auto rotated = pickNaanHarvestTarget("crypto", 2);
    auto automatic = pickNaanHarvestTarget("crypto", 2, "auto");
    assert(automatic.url == rotated.url);
    auto unknown = pickNaanHarvestTarget("crypto", 2, "not-an-engine");
    assert(unknown.url == rotated.url);
}

int main() {
    testLoopActions();
    testClearnetNotFunneledToAhmia();
    testOnionRotation();
    testClearnetRoster();
    testJunkAndHrefs();
    testSearchSurfaceAndRelativeHrefs();
    testUsefulness();
    testExcerptSkipsMenu();
    testNamedEngine();
    return 0;
}
