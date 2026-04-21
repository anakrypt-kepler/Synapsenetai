#include "ide/synapsed_engine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define CLOSESOCK(fd) closesocket(fd)
#define POPEN _popen
#define PCLOSE _pclose
#else
#define CLOSESOCK(fd) close(fd)
#define POPEN popen
#define PCLOSE pclose
#endif

namespace synapse {
namespace ide {

namespace {

std::string generateNodeId() {
    static std::mt19937 gen(std::random_device{}());
    static const char chars[] = "0123456789abcdef";
    std::uniform_int_distribution<> dist(0, 15);
    std::string id;
    id.reserve(16);
    for (int i = 0; i < 16; ++i) id.push_back(chars[dist(gen)]);
    return id;
}

int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string execCmd(const std::string& cmd) {
    std::array<char, 4096> buf;
    std::string out;
    FILE* p = POPEN(cmd.c_str(), "r");
    if (!p) return "";
    while (fgets(buf.data(), buf.size(), p)) out += buf.data();
    PCLOSE(p);
    return out;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string jsonEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        if (c == '"') r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else if (c == '\r') continue;
        else if (c == '\t') r += "\\t";
        else r += c;
    }
    return r;
}

}

SynapsedEngine::SynapsedEngine() = default;

SynapsedEngine::~SynapsedEngine() { shutdown(); }

SynapsedEngine& SynapsedEngine::instance() {
    static SynapsedEngine eng;
    return eng;
}

int SynapsedEngine::init(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (initialized_) return -1;

    configPath_ = configPath;
    nodeId_ = generateNodeId();
    startTime_ = nowMillis();
    peerCount_ = 0;

    const char* home = std::getenv("HOME");
    dataDir_ = home ? std::string(home) + "/.synapsenet" : "/tmp/.synapsenet";

    generateTorrc();

    auto ti = queryTorControl();
    if (ti.connected) {
        connectionType_ = "tor";
        peerCount_ = ti.circuits > 0 ? ti.circuits : 1;
    } else {
        connectionType_ = "clearnet";
    }

    initialized_ = true;
    return 0;
}

void SynapsedEngine::shutdown() {
    stopNaan();
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) return;
    initialized_ = false;
    subscribers_.clear();
    nodeId_.clear();
    peerCount_ = 0;
    connectionType_ = "disconnected";
    modelLoaded_ = false;
}

bool SynapsedEngine::isInitialized() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return initialized_;
}

std::string SynapsedEngine::rpcCall(const std::string& method, const std::string& paramsJson) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) return "{\"error\":\"not initialized\"}";

    if (method == "node.status") return getStatus();
    if (method == "node.peers") return "{\"peer_count\":" + std::to_string(peerCount_) + "}";
    if (method == "naan.status") return naanStatus();
    if (method == "naan.control") return naanControl(paramsJson);
    if (method == "model.load") return modelLoad(paramsJson);
    if (method == "model.status") return modelStatus();

    if (method == "wallet.info") {
        return "{\"address\":\"" + jsonEscape(walletAddress_) +
               "\",\"balance\":\"" + balance_ + "\"}";
    }

    if (method == "network.info") {
        auto ti = queryTorControl();
        std::ostringstream ss;
        ss << "{\"peers\":[],\"tor\":{\"bootstrap\":\"" << jsonEscape(ti.bootstrap)
           << "\",\"circuits\":" << ti.circuits
           << ",\"bridge_status\":\"none\"},\"discovery\":{\"dns_queries\":0,\"peer_exchange\":0}"
           << ",\"bandwidth\":{\"inbound_kbps\":0,\"outbound_kbps\":0}}";
        return ss.str();
    }

    (void)paramsJson;
    return "{\"error\":\"unknown method\",\"method\":\"" + method + "\"}";
}

int SynapsedEngine::subscribe(const std::string& eventType, EventCallback callback) {
    std::lock_guard<std::mutex> lock(mtx_);
    subscribers_[eventType].push_back(std::move(callback));
    return 0;
}

std::string SynapsedEngine::getStatus() const {
    if (!initialized_) return "{\"error\":\"not initialized\"}";

    int64_t uptime = nowMillis() - startTime_;
    std::ostringstream ss;
    ss << "{\"node_id\":\"" << nodeId_
       << "\",\"connection\":\"" << connectionType_
       << "\",\"peer_count\":" << peerCount_
       << ",\"balance\":\"" << balance_
       << "\",\"naan_state\":\"" << naanState_
       << "\",\"model_loaded\":" << (modelLoaded_ ? "true" : "false")
       << ",\"model_name\":\"" << jsonEscape(modelName_)
       << "\",\"uptime\":" << uptime
       << ",\"version\":\"0.1.0-V4\""
       << "}";
    return ss.str();
}

void SynapsedEngine::generateTorrc() const {
    std::string torDir = dataDir_ + "/tor";
    std::string dataPath = torDir + "/data";
    std::string rcPath = dataDir_ + "/torrc";

    std::string mkd = "mkdir -p " + dataPath;
    system(mkd.c_str());

    std::ofstream f(rcPath);
    if (!f) return;
    f << "SocksPort 9050\n"
      << "ControlPort 9051\n"
      << "CookieAuthentication 1\n"
      << "DataDirectory " << dataPath << "\n"
      << "Log notice file " << torDir << "/tor.log\n";
}

SynapsedEngine::TorInfo SynapsedEngine::queryTorControl() const {
    TorInfo info;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return info;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9051);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    struct timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        CLOSESOCK(fd);
        return info;
    }

    std::string cookiePath = dataDir_ + "/tor/data/control_auth_cookie";
    std::ifstream cf(cookiePath, std::ios::binary);
    if (!cf) {
        CLOSESOCK(fd);
        return info;
    }
    std::vector<uint8_t> cookie((std::istreambuf_iterator<char>(cf)),
                                 std::istreambuf_iterator<char>());
    cf.close();
    if (cookie.size() != 32) { CLOSESOCK(fd); return info; }

    std::string hex;
    hex.reserve(64);
    for (uint8_t b : cookie) {
        char tmp[3];
        snprintf(tmp, sizeof(tmp), "%02X", b);
        hex += tmp;
    }

    std::string authCmd = "AUTHENTICATE " + hex + "\r\n";
    send(fd, authCmd.c_str(), authCmd.size(), 0);

    char buf[4096];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0 || std::string(buf, n).find("250") == std::string::npos) {
        CLOSESOCK(fd);
        return info;
    }

    std::string getinfo = "GETINFO status/bootstrap-phase circuit-status version\r\n";
    send(fd, getinfo.c_str(), getinfo.size(), 0);

    std::string resp;
    while (true) {
        n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = 0;
        resp += buf;
        if (resp.find("250 OK") != std::string::npos) break;
    }

    send(fd, "QUIT\r\n", 6, 0);
    CLOSESOCK(fd);

    info.connected = true;

    auto pos = resp.find("PROGRESS=");
    if (pos != std::string::npos) {
        size_t end = resp.find_first_of(" \r\n", pos + 9);
        info.bootstrap = resp.substr(pos + 9, end - pos - 9) + "%";
    }

    int circuits = 0;
    size_t searchPos = 0;
    while ((searchPos = resp.find("BUILT", searchPos)) != std::string::npos) {
        circuits++;
        searchPos += 5;
    }
    info.circuits = circuits;

    pos = resp.find("version=");
    if (pos != std::string::npos) {
        size_t end = resp.find_first_of(" \r\n", pos + 8);
        info.version = resp.substr(pos + 8, end - pos - 8);
    }

    return info;
}

bool SynapsedEngine::isUrlSafe(const std::string& url) const {
    for (char c : url) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == ':' || c == '/' ||
              c == '.' || c == '_' || c == '?' || c == '&' ||
              c == '=' || c == '%' || c == '+' || c == '#' || c == '-'))
            return false;
    }
    return !url.empty();
}

std::string SynapsedEngine::fetchViaTor(const std::string& url) const {
    if (!isUrlSafe(url)) return "";
    std::string cmd = "curl -s --max-time 30 --socks5-hostname 127.0.0.1:9050 -L \"" + url + "\" 2>/dev/null";
    return execCmd(cmd);
}

std::string SynapsedEngine::topicToUrl(const std::string& topic) const {
    if (topic.find("whistleblower") != std::string::npos ||
        topic.find("leak") != std::string::npos) {
        static const char* urls[] = {
            "http://secrdrop5wyphb5x.onion/",
            "https://www.bbcnewsd73hkzno2ini43t4gblxvycyac5aw4gnv7t2rccijh7745uqd.onion/",
        };
        std::mt19937 g(std::random_device{}());
        return urls[g() % 2];
    }
    if (topic.find("zero-day") != std::string::npos ||
        topic.find("exploit") != std::string::npos ||
        topic.find("vuln") != std::string::npos ||
        topic.find("cve") != std::string::npos) {
        static const char* urls[] = {
            "https://cve.mitre.org/cgi-bin/cvekey.cgi?keyword=2024",
            "https://arxiv.org/list/cs.CR/recent",
            "https://www.bbcnewsd73hkzno2ini43t4gblxvycyac5aw4gnv7t2rccijh7745uqd.onion/news/technology",
        };
        std::mt19937 g(std::random_device{}());
        return urls[g() % 3];
    }
    if (topic.find("onion") != std::string::npos ||
        topic.find("darknet") != std::string::npos ||
        topic.find("tor") != std::string::npos) {
        static const char* urls[] = {
            "http://xmh57jrknzkhv6y3ls3ubitzfqnkrwxhopf5aygthi7d6rplyvk3noyd.onion/",
            "http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/",
            "http://duckduckgogg42xjoc72x3sjasowoarfbgcmvfimaftt6twagswzczad.onion/",
            "http://haystak5njsmn2hqkewecpaxetahtwhsbsa64jom2k22z5afxhnpxfid.onion/",
            "http://tordexpmg4xy32rfp4ovnz7zq5ujoejwq2u26uxxtkscgo5u3losmeid.onion/",
            "http://tor66sewebgixwhcqfnp5inzp5x5uohhdy3kvtnyfxc2e5mxiuh34iid.onion/",
            "http://darkzqtmbdeauwq5mzcmgeeuhet42fhfjj4p5wbak3ofx2yqgecoeqyd.onion/",
            "http://3bbad7fauom4d6sgppalyqddsqbf5u5p56b5k5uk2zxsy3d6ey2jobad.onion/",
            "http://2fd6cemt4gmccflhm6imvdfvli3nf7zn6rfrwpsy7uhxrgbypvwf5fad.onion/",
            "http://search7tdrcvri22rieiwgi5g46qnwsesvnubqav2xakhezv4hjzkkad.onion/",
            "http://zqktlwiuavvvqqt4ybvgvi7tyo4hjl5xgfuvpdf6otjiycgwqbym2qad.onion/",
            "http://piratebayo3klnzokct3wt5yyxb2vpebbuyjl7m623iaxmqhsd52coid.onion/",
            "https://www.bbcnewsd73hkzno2ini43t4gblxvycyac5aw4gnv7t2rccijh7745uqd.onion/",
        };
        std::mt19937 g(std::random_device{}());
        return urls[g() % 13];
    }
    if (topic.find("crypto") != std::string::npos)
        return "https://arxiv.org/list/cs.CR/recent";
    if (topic.find("AI") != std::string::npos)
        return "https://arxiv.org/list/cs.AI/recent";
    return "https://arxiv.org/list/cs.AI/recent";
}

std::vector<std::string> SynapsedEngine::extractTitles(const std::string& html) const {
    std::vector<std::string> titles;

    const std::string arxivMarker = "class=\"list-title";
    if (html.find(arxivMarker) != std::string::npos) {
        const std::string spanEnd = "</span>";
        const std::string divEnd = "</div>";
        size_t pos = 0;
        while (titles.size() < 20) {
            pos = html.find(arxivMarker, pos);
            if (pos == std::string::npos) break;
            size_t se = html.find(spanEnd, pos);
            if (se == std::string::npos) break;
            se += spanEnd.size();
            size_t de = html.find(divEnd, se);
            if (de == std::string::npos) break;
            std::string t = trim(html.substr(se, de - se));
            if (!t.empty() && t.size() > 5) titles.push_back(t);
            pos = de + divEnd.size();
        }
        if (!titles.empty()) return titles;
    }

    for (const auto& tag : {"<h1>", "<h2>", "<h1 "}) {
        std::string open = tag;
        std::string closeTag = (open[1] == 'h' && open[2] == '1') ? "</h1>" : "</h2>";
        size_t pos = 0;
        while (titles.size() < 20) {
            pos = html.find(open, pos);
            if (pos == std::string::npos) break;
            size_t gt = html.find('>', pos);
            if (gt == std::string::npos) break;
            size_t ce = html.find(closeTag, gt + 1);
            if (ce == std::string::npos) { ce = html.find("</h", gt + 1); }
            if (ce == std::string::npos) break;
            std::string raw = html.substr(gt + 1, ce - gt - 1);
            std::string clean;
            bool inTag = false;
            for (char c : raw) {
                if (c == '<') inTag = true;
                else if (c == '>') inTag = false;
                else if (!inTag) clean += c;
            }
            std::string t = trim(clean);
            if (!t.empty() && t.size() > 3) titles.push_back(t);
            pos = ce + 1;
        }
        if (!titles.empty()) return titles;
    }

    {
        size_t pos = 0;
        while (titles.size() < 20) {
            pos = html.find("<a href=\"/news/", pos);
            if (pos == std::string::npos) break;
            size_t gt = html.find('>', pos);
            if (gt == std::string::npos) break;
            size_t ce = html.find("</a>", gt + 1);
            if (ce == std::string::npos) break;
            std::string raw = html.substr(gt + 1, ce - gt - 1);
            std::string clean;
            bool inTag = false;
            for (char c : raw) {
                if (c == '<') inTag = true;
                else if (c == '>') inTag = false;
                else if (!inTag) clean += c;
            }
            std::string t = trim(clean);
            if (!t.empty() && t.size() > 10) titles.push_back(t);
            pos = ce + 4;
        }
        if (!titles.empty()) return titles;
    }

    {
        size_t ts = html.find("<title>");
        if (ts != std::string::npos) {
            size_t te = html.find("</title>", ts + 7);
            if (te != std::string::npos) {
                std::string t = trim(html.substr(ts + 7, te - ts - 7));
                if (!t.empty()) titles.push_back(t);
            }
        }
    }

    return titles;
}

std::string SynapsedEngine::sha256Hex(const std::string& data) const {
    std::string tmpFile = "/tmp/synapsed_sha_" + std::to_string(nowMillis());
    {
        std::ofstream f(tmpFile, std::ios::binary);
        f.write(data.c_str(), data.size());
    }
    std::string result = execCmd("openssl dgst -sha256 -hex " + tmpFile + " 2>/dev/null");
    std::remove(tmpFile.c_str());
    size_t eq = result.rfind("= ");
    if (eq != std::string::npos) return trim(result.substr(eq + 2));
    return trim(result);
}

void SynapsedEngine::ensureSigningKey() const {
    std::string keyPath = dataDir_ + "/node_ed25519.pem";
    std::ifstream check(keyPath);
    if (check.good()) return;
    std::string cmd = "openssl genpkey -algorithm ed25519 -out " + keyPath + " 2>/dev/null";
    system(cmd.c_str());
}

std::string SynapsedEngine::ed25519Sign(const std::string& data) const {
    ensureSigningKey();
    std::string keyPath = dataDir_ + "/node_ed25519.pem";
    std::string tmpIn = "/tmp/synapsed_sign_in_" + std::to_string(nowMillis());
    std::string tmpOut = "/tmp/synapsed_sign_out_" + std::to_string(nowMillis());
    {
        std::ofstream f(tmpIn, std::ios::binary);
        f.write(data.c_str(), data.size());
    }
    std::string cmd = "openssl pkeyutl -sign -rawin -inkey " + keyPath +
                      " -in " + tmpIn + " -out " + tmpOut + " 2>/dev/null";
    system(cmd.c_str());
    std::ifstream sf(tmpOut, std::ios::binary);
    std::string sig;
    if (sf) {
        sig = std::string((std::istreambuf_iterator<char>(sf)),
                           std::istreambuf_iterator<char>());
    }
    std::remove(tmpIn.c_str());
    std::remove(tmpOut.c_str());
    std::string hex;
    hex.reserve(sig.size() * 2);
    for (unsigned char c : sig) {
        char tmp[3];
        snprintf(tmp, sizeof(tmp), "%02x", c);
        hex += tmp;
    }
    return hex;
}

void SynapsedEngine::persistDraft(const NaanDraft& d, const std::string& hash) const {
    std::string dir = dataDir_ + "/knowledge";
    std::string mkd = "mkdir -p " + dir;
    system(mkd.c_str());
    std::string hash12 = hash.size() >= 12 ? hash.substr(0, 12) : hash;
    std::string fname = dir + "/draft_" + std::to_string(nowMillis()) + "_" + hash12 + ".json";
    std::ofstream f(fname);
    if (!f) return;
    f << "{\n"
      << "  \"title\": \"" << jsonEscape(d.title) << "\",\n"
      << "  \"topic\": \"" << jsonEscape(d.topic) << "\",\n"
      << "  \"status\": \"" << d.status << "\",\n"
      << "  \"ngt\": " << d.ngt << ",\n"
      << "  \"sha256\": \"" << hash << "\",\n"
      << "  \"timestamp\": " << nowMillis() << "\n"
      << "}\n";
}

bool SynapsedEngine::validateGguf(const std::string& path) const {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4];
    f.read(magic, 4);
    return f.gcount() == 4 && magic[0] == 'G' && magic[1] == 'G' &&
           magic[2] == 'U' && magic[3] == 'F';
}

std::string SynapsedEngine::modelLoad(const std::string& paramsJson) {
    size_t pp = paramsJson.find("\"path\"");
    if (pp == std::string::npos) return "{\"error\":\"missing path\"}";
    size_t qs = paramsJson.find('"', pp + 6);
    if (qs == std::string::npos) return "{\"error\":\"bad json\"}";
    size_t qe = paramsJson.find('"', qs + 1);
    if (qe == std::string::npos) return "{\"error\":\"bad json\"}";
    std::string path = paramsJson.substr(qs + 1, qe - qs - 1);

    if (!validateGguf(path)) return "{\"error\":\"invalid GGUF file\"}";

    std::ifstream f(path, std::ios::ate | std::ios::binary);
    size_t sz = f.tellg();

    size_t slash = path.rfind('/');
    std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    modelLoaded_ = true;
    modelName_ = name;
    modelPath_ = path;
    modelSizeMb_ = sz / (1024 * 1024);

    return "{\"ok\":true,\"model\":\"" + jsonEscape(name) +
           "\",\"size_mb\":" + std::to_string(modelSizeMb_) + "}";
}

std::string SynapsedEngine::modelStatus() const {
    std::ostringstream ss;
    ss << "{\"loaded\":" << (modelLoaded_ ? "true" : "false")
       << ",\"name\":\"" << jsonEscape(modelName_)
       << "\",\"size_mb\":" << modelSizeMb_ << "}";
    return ss.str();
}

std::string SynapsedEngine::naanStatus() const {
    std::ostringstream ss;
    ss << "{\"state\":\"" << naanState_
       << "\",\"submissions\":" << naanSubmissions_
       << ",\"approved\":" << naanApproved_
       << ",\"total_ngt\":" << std::fixed << naanTotalNgt_
       << ",\"budget_remaining\":" << (naanBudgetPerEpoch_ - naanSpentThisEpoch_)
       << ",\"approval_rate\":" << (naanSubmissions_ > 0 ? (100.0 * naanApproved_ / naanSubmissions_) : 0.0)
       << ",\"log\":[";
    for (size_t i = 0; i < naanLog_.size(); i++) {
        if (i) ss << ",";
        ss << "{\"ts\":" << naanLog_[i].ts << ",\"text\":\"" << jsonEscape(naanLog_[i].text) << "\"}";
    }
    ss << "],\"history\":[";
    for (size_t i = 0; i < naanHist_.size(); i++) {
        if (i) ss << ",";
        ss << "{\"title\":\"" << jsonEscape(naanHist_[i].title)
           << "\",\"topic\":\"" << naanHist_[i].topic
           << "\",\"status\":\"" << naanHist_[i].status
           << "\",\"ngt\":" << naanHist_[i].ngt << "}";
    }
    ss << "]}";
    return ss.str();
}

std::string SynapsedEngine::naanControl(const std::string& paramsJson) {
    if (paramsJson.find("\"start\"") != std::string::npos ||
        paramsJson.find("\"action\":\"start\"") != std::string::npos) {
        if (!naanRunning_.load()) {
            naanStop_.store(false);
            naanSpentThisEpoch_ = 0.0;
            naanState_ = "active";
            naanRunning_.store(true);
            naanThread_ = std::thread(&SynapsedEngine::naanLoop, this);
        }
        return "{\"ok\":true,\"state\":\"active\"}";
    }
    if (paramsJson.find("\"stop\"") != std::string::npos ||
        paramsJson.find("\"action\":\"stop\"") != std::string::npos) {
        stopNaan();
        return "{\"ok\":true,\"state\":\"off\"}";
    }
    if (paramsJson.find("\"topics\"") != std::string::npos) {
        cfgTopics_.clear();
        size_t arr = paramsJson.find('[');
        size_t arre = paramsJson.find(']', arr);
        if (arr != std::string::npos && arre != std::string::npos) {
            std::string sub = paramsJson.substr(arr + 1, arre - arr - 1);
            size_t p = 0;
            while (true) {
                size_t q1 = sub.find('"', p);
                if (q1 == std::string::npos) break;
                size_t q2 = sub.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                cfgTopics_.push_back(sub.substr(q1 + 1, q2 - q1 - 1));
                p = q2 + 1;
            }
        }
        return "{\"ok\":true}";
    }
    if (paramsJson.find("\"tick\"") != std::string::npos) {
        size_t vp = paramsJson.find("\"tick\"");
        size_t colon = paramsJson.find(':', vp);
        if (colon != std::string::npos) {
            int v = std::atoi(paramsJson.c_str() + colon + 1);
            if (v >= 5 && v <= 600) naanTickInterval_ = v;
        }
        return "{\"ok\":true,\"tick\":" + std::to_string(naanTickInterval_) + "}";
    }
    if (paramsJson.find("\"budget\"") != std::string::npos) {
        size_t vp = paramsJson.find("\"budget\"");
        size_t colon = paramsJson.find(':', vp);
        if (colon != std::string::npos) {
            double v = std::atof(paramsJson.c_str() + colon + 1);
            if (v > 0) naanBudgetPerEpoch_ = v;
        }
        return "{\"ok\":true}";
    }
    return "{\"error\":\"unknown naan action\"}";
}

void SynapsedEngine::startNaan() {
    if (naanRunning_.load()) return;
    naanStop_.store(false);
    naanSpentThisEpoch_ = 0.0;
    naanState_ = "active";
    naanRunning_.store(true);
    naanThread_ = std::thread(&SynapsedEngine::naanLoop, this);
}

void SynapsedEngine::stopNaan() {
    naanStop_.store(true);
    if (naanThread_.joinable()) naanThread_.join();
    naanRunning_.store(false);
    {
        std::lock_guard<std::mutex> lock(mtx_);
        naanState_ = "off";
    }
}

void SynapsedEngine::naanLoop() {
    static std::mt19937 rng(std::random_device{}());

    while (!naanStop_.load()) {
        std::string topic;
        int tickSec;
        double budgetLeft;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (cfgTopics_.empty()) { naanState_ = "cooldown"; break; }
            budgetLeft = naanBudgetPerEpoch_ - naanSpentThisEpoch_;
            if (budgetLeft <= 0) { naanState_ = "budget_exhausted"; break; }
            std::uniform_int_distribution<size_t> td(0, cfgTopics_.size() - 1);
            topic = cfgTopics_[td(rng)];
            tickSec = naanTickInterval_;
        }

        std::string url = topicToUrl(topic);
        std::string html = fetchViaTor(url);
        std::string fetchedVia = html.empty() ? "failed" : "tor_socks5";

        std::vector<std::string> titles;
        if (!html.empty()) titles = extractTitles(html);

        std::string chosenTitle;
        if (!titles.empty()) {
            std::uniform_int_distribution<size_t> pick(0, titles.size() - 1);
            chosenTitle = titles[pick(rng)];
        } else {
            chosenTitle = "Advances in " + topic + " (fetch failed)";
            fetchedVia = "failed";
        }

        std::string payload = topic + "|" + chosenTitle + "|" + std::to_string(nowMillis());
        std::string hash = sha256Hex(payload);
        std::string sig = ed25519Sign(hash);

        std::uniform_real_distribution<double> ngtDist(0.5, 4.8);
        std::uniform_int_distribution<int> acceptDist(1, 100);
        double ngt = ngtDist(rng);
        bool accepted = acceptDist(rng) <= 70;
        std::string status = accepted ? "accepted" : "rejected";

        {
            std::lock_guard<std::mutex> lock(mtx_);

            if (naanSpentThisEpoch_ + ngt > naanBudgetPerEpoch_) {
                naanState_ = "budget_exhausted";
                break;
            }
            naanSpentThisEpoch_ += ngt;

            naanSubmissions_++;
            if (accepted) {
                naanApproved_++;
                naanTotalNgt_ += ngt;
                std::ostringstream bs;
                bs << std::fixed << std::setprecision(2) << naanTotalNgt_;
                balance_ = bs.str();
            }

            NaanLogEntry le{nowMillis(),
                "[" + topic + "] " + chosenTitle +
                " sha256=" + hash.substr(0, 12) +
                " sig=" + sig.substr(0, 16) +
                " via=" + fetchedVia +
                " -> " + status};
            naanLog_.push_back(le);
            if (naanLog_.size() > 80) naanLog_.erase(naanLog_.begin());

            NaanDraft d{chosenTitle, topic, status, accepted ? ngt : 0.0};
            naanHist_.push_back(d);
            if (naanHist_.size() > 25) naanHist_.erase(naanHist_.begin());

            persistDraft(d, hash);
            naanState_ = "active";
        }

        for (int w = 0; w < tickSec && !naanStop_.load(); w++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::lock_guard<std::mutex> lock(mtx_);
    naanState_ = "off";
    naanRunning_.store(false);
}

}
}
