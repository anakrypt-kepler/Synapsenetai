#include "core/naan_harvest_plan.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace synapse {
namespace core {

namespace {

std::string lowerCopy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string queryEncode(const std::string& topic) {
    std::ostringstream o;
    for (unsigned char c : topic) {
        if (c == ' ') o << '+';
        else if (std::isalnum(c) || c == '-' || c == '_' || c == '.') o << c;
        else {
            static const char* hex = "0123456789ABCDEF";
            o << '%' << hex[c >> 4] << hex[c & 15];
        }
    }
    return o.str();
}

bool topicWantsOnion(const std::string& topic) {
    const std::string t = lowerCopy(topic);
    return t.find("onion") != std::string::npos ||
           t.find("darknet") != std::string::npos ||
           t.find("tor") != std::string::npos ||
           t.find("whistle") != std::string::npos ||
           t.find("leak") != std::string::npos;
}

} // namespace

NaanHarvestLoopAction decideNaanHarvestLoop(bool userStop, bool topicsEmpty,
                                            double spent, double budget) {
    if (userStop) return NaanHarvestLoopAction::StopUser;
    if (topicsEmpty) return NaanHarvestLoopAction::WaitTopic;
    if (budget > 0 && spent + 1.0 > budget) return NaanHarvestLoopAction::WaitBudget;
    return NaanHarvestLoopAction::Fetch;
}

std::vector<std::string> naanClearnetSearchEngines() {
    // Fetched over Tor SOCKS. These are result pages, not a single Ahmia funnel.
    return {
        "https://html.duckduckgo.com/html/?q=",
        "https://search.brave.com/search?q=",
        "https://en.wikipedia.org/w/index.php?search=",
        "https://arxiv.org/search/?query=",
    };
}

std::vector<std::string> naanDarknetSearchEngines() {
    return {
        "http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/search/?q=",
        "http://torchdeedp3i2jigzjdmfpn5ttjhthh5wbmda2rr3jvqjg5p77c54dqd.onion/search?query=",
        "http://duckduckgogg42xjoc72x3sjasowoarfbgcmvfimaftt6twagswzczad.onion/?q=",
        "http://darkzqtmbdeauwq5mzcmgeeuhet42fhfjj4p5wbak3ofx2yqgecoeqyd.onion/search?query=",
        "http://search7tdrcvri22rieiwgi5g46qnwsesvnubqav2xakhezv4hjzkkad.onion/result.php?search=",
        "http://haystak5njsmn2hqkewecpaxetahtwhsbsa64jom2k22z5afxhnpxfid.onion/?q=",
    };
}

NaanHarvestTarget pickNaanHarvestTarget(const std::string& topic, uint64_t tick) {
    NaanHarvestTarget out;
    const std::string q = queryEncode(topic);
    const std::string t = lowerCopy(topic);

    if (topicWantsOnion(topic)) {
        const auto engines = naanDarknetSearchEngines();
        const size_t idx = engines.empty() ? 0 : static_cast<size_t>(tick % engines.size());
        out.url = engines[idx] + q;
        out.engine = "darknet-" + std::to_string(idx);
        out.onion = true;
        return out;
    }

    // Direct knowledge pages stay first-class. Tor still carries the bytes.
    if (tick % 3 == 0) {
        if (t.find("crypto") != std::string::npos || t.find("zero-day") != std::string::npos ||
            t.find("cve") != std::string::npos || t.find("exploit") != std::string::npos) {
            out.url = "https://arxiv.org/list/cs.CR/recent";
            out.engine = "arxiv-cr";
            return out;
        }
        if (t.find("ai") != std::string::npos) {
            out.url = "https://arxiv.org/list/cs.AI/recent";
            out.engine = "arxiv-ai";
            return out;
        }
        out.url = "https://arxiv.org/search/?query=" + q + "&searchtype=all&source=header";
        out.engine = "arxiv-search";
        return out;
    }

    const auto engines = naanClearnetSearchEngines();
    const size_t idx = engines.empty() ? 0 : static_cast<size_t>(tick % engines.size());
    out.url = engines[idx] + q;
    if (engines[idx].find("duckduckgo") != std::string::npos) out.engine = "duckduckgo";
    else if (engines[idx].find("brave") != std::string::npos) out.engine = "brave";
    else if (engines[idx].find("wikipedia") != std::string::npos) out.engine = "wikipedia";
    else if (engines[idx].find("arxiv") != std::string::npos) out.engine = "arxiv";
    else out.engine = "clearnet-" + std::to_string(idx);
    out.onion = false;
    return out;
}

bool isJunkHarvestHtml(const std::string& html) {
    if (html.size() < 80) return true;
    const std::string h = lowerCopy(html.substr(0, std::min<size_t>(html.size(), 8000)));
    static const char* needles[] = {
        "dread access queue",
        "awaiting forwarding to the platform",
        "just a moment...",
        "cf-challenge",
        "attention required! | cloudflare",
        "enable javascript and cookies to continue",
        "checking your browser before accessing",
        "have not deployd non-javascript",
        "this site requires javascript",
        "please enable javascript to use ahmia",
    };
    for (const char* n : needles) {
        if (h.find(n) != std::string::npos) return true;
    }
    return false;
}

bool isHarvestSearchSurface(const std::string& url) {
    const std::string u = lowerCopy(url);
    if (u.find("duckduckgo.com") != std::string::npos) return true;
    if (u.find("duckduckgogg") != std::string::npos) return true;
    if (u.find("search.brave.com") != std::string::npos) return true;
    if (u.find("wikipedia.org/w/") != std::string::npos) return true;
    if (u.find("arxiv.org/search") != std::string::npos) return true;
    if (u.find("arxiv.org/list") != std::string::npos) return true;
    if (u.find("ahmia.") != std::string::npos) return true;
    if (u.find("juhanurmihxlp77") != std::string::npos) return true;
    if (u.find("torchdeedp3i2") != std::string::npos) return true;
    if (u.find("darkzqtmbdeauw") != std::string::npos) return true;
    if (u.find("search7tdrcvri") != std::string::npos) return true;
    if (u.find("haystak5njsmn") != std::string::npos) return true;
    return false;
}

namespace {

std::string harvestOrigin(const std::string& base) {
    const auto scheme = base.find("://");
    if (scheme == std::string::npos) return {};
    const auto slash = base.find('/', scheme + 3);
    return slash == std::string::npos ? base : base.substr(0, slash);
}

std::string resolveHarvestHref(const std::string& href, const std::string& base) {
    if (href.compare(0, 7, "http://") == 0 || href.compare(0, 8, "https://") == 0) return href;
    if (href.compare(0, 2, "//") == 0) {
        const auto scheme = base.find("://");
        const std::string prefix = (scheme == std::string::npos) ? std::string("https:")
                                                                : base.substr(0, scheme + 1);
        return prefix + href;
    }
    if (!href.empty() && href[0] == '/') {
        const std::string origin = harvestOrigin(base);
        if (origin.empty()) return {};
        return origin + href;
    }
    return {};
}

} // namespace

std::vector<std::string> extractHarvestHrefs(const std::string& html, size_t limit,
                                             const std::string& baseUrl) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (out.size() < limit) {
        pos = html.find("href=", pos);
        if (pos == std::string::npos) break;
        pos += 5;
        while (pos < html.size() && (html[pos] == ' ' || html[pos] == '"' || html[pos] == '\'')) pos++;
        if (pos >= html.size()) break;
        size_t end = pos;
        while (end < html.size() && html[end] != '"' && html[end] != '\'' && html[end] != ' ' &&
               html[end] != '>' && html[end] != '#') {
            end++;
        }
        const std::string raw = html.substr(pos, end - pos);
        const std::string url = resolveHarvestHref(raw, baseUrl);
        pos = end;
        if (url.empty()) continue;
        if (isHarvestSearchSurface(url)) continue;
        if (std::find(out.begin(), out.end(), url) == out.end()) out.push_back(url);
    }
    return out;
}

} // namespace core
} // namespace synapse
