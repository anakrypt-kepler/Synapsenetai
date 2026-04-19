#include "web/web.h"
#include <algorithm>

namespace synapse {
namespace web {

static std::string applyTemplate(const std::string& base, const std::string& query) {
    std::string encoded = urlEncode(query);
    std::string url = base;
    size_t pos = url.find("{query}");
    if (pos != std::string::npos) {
        url.replace(pos, 7, encoded);
        return url;
    }
    pos = url.find("%s");
    if (pos != std::string::npos) {
        url.replace(pos, 2, encoded);
        return url;
    }
    if (url.find('?') == std::string::npos) {
        return url + "?q=" + encoded;
    }
    return url + "&q=" + encoded;
}

static const std::vector<DarknetEngineInfo>& engineList() {
    static const std::vector<DarknetEngineInfo> engines = {
        {SearchEngine::AHMIA, "Ahmia", "http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/", true},
        {SearchEngine::TORCH, "Torch", "http://torchdeedp3i2jigzjdmfpn5ttjhthh5wbmda2rr3jvqjg5p77c54dqd.onion/", true},
        {SearchEngine::NOTEVIL, "NotEvil", "http://hss3uro2hsxfogfq.onion/", true},
        {SearchEngine::DARKSEARCH, "DarkSearch", "http://darkzqtmbdeauwq5mzcmgeeuhet42fhfjj4p5wbak3ofx2yqgecoeqyd.onion/", true},
        {SearchEngine::DEEPSEARCH, "DeepSearch", "http://search7tdrcvri22rieiwgi5g46qnwsesvnubqav2xakhezv4hjzkkad.onion/", true}
    };
    return engines;
}

DarknetEngines::DarknetEngines() = default;

std::vector<DarknetEngineInfo> DarknetEngines::list() const {
    return engineList();
}

std::string DarknetEngines::buildSearchUrl(SearchEngine engine, const std::string& query) const {
    const auto& engines = engineList();
    auto it = std::find_if(engines.begin(), engines.end(),
                           [&](const DarknetEngineInfo& info) { return info.engine == engine; });
    if (it == engines.end()) return "";
    return applyTemplate(it->baseUrl, query);
}

}
}
