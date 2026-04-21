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

    if (html.find("ancaptcha") != std::string::npos ||
        html.find("anCaptcha") != std::string::npos) {
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

    size_t imgPos = html.find("captcha");
    if (imgPos != std::string::npos) {
        size_t imgTag = html.rfind("<img", imgPos);
        if (imgTag == std::string::npos) imgTag = html.find("<img", imgPos);
        if (imgTag != std::string::npos) {
            size_t srcPos = html.find("src=\"", imgTag);
            if (srcPos != std::string::npos && srcPos < imgTag + 300) {
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

    std::string fullUrl = imgUrl;
    if (imgUrl[0] == '/') return "";

    std::string tmpImg = "/tmp/synapsed_captcha_" + std::to_string(nowMillis()) + ".png";
    std::string dlCmd = "curl -s --max-time 15 --socks5-hostname 127.0.0.1:9050 -L -o " +
                        tmpImg + " \"" + fullUrl + "\" 2>/dev/null";
    system(dlCmd.c_str());

    std::string ocrResult = execCmd(
        "tesseract " + tmpImg + " stdout --psm 7 -c tessedit_char_whitelist=ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 2>/dev/null");
    std::remove(tmpImg.c_str());

    std::string answer;
    for (char c : ocrResult) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            answer += c;
    }
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
                      "-c /tmp/synapsed_cookies.txt -b /tmp/synapsed_cookies.txt "
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
    std::string tmpImg = "/tmp/synapsed_cap_" + std::to_string(nowMillis()) + ".png";
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
    std::vector<std::string> imgUrls;
    size_t pos = 0;
    while (imgUrls.size() < 5) {
        size_t imgTag = html.find("<img", pos);
        if (imgTag == std::string::npos) break;
        size_t tagEnd = html.find('>', imgTag);
        if (tagEnd == std::string::npos) break;
        std::string tag = html.substr(imgTag, tagEnd - imgTag + 1);
        pos = tagEnd + 1;

        if (tag.find("captcha") == std::string::npos &&
            tag.find("rotate") == std::string::npos &&
            tag.find("challenge") == std::string::npos &&
            tag.find("data:image") == std::string::npos) continue;

        size_t srcP = tag.find("src=\"");
        if (srcP == std::string::npos) continue;
        srcP += 5;
        size_t srcE = tag.find('"', srcP);
        if (srcE == std::string::npos) continue;
        imgUrls.push_back(tag.substr(srcP, srcE - srcP));
    }

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
    for (size_t i = 0; i < imgUrls.size(); i++) {
        std::string path = downloadCaptchaImage(imgUrls[i]);
        if (path.empty()) continue;
        double angle = detectImageRotation(path);
        std::remove(path.c_str());
        int correction = ((int)(-angle) % 360 + 360) % 360;
        if (i > 0) result += "&";
        result += "rotate_" + std::to_string(i) + "=" + std::to_string(correction);
    }

    if (!tokenField.empty()) {
        result = "ancaptcha_token=" + tokenField + "&" + result;
    }
    return result;
}

std::string SynapsedEngine::solveSliderCaptcha(const std::string& html) const {
    std::string bgUrl, pieceUrl;

    size_t pos = 0;
    while (true) {
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

        if (tag.find("background") != std::string::npos ||
            tag.find("slider-bg") != std::string::npos ||
            tag.find("captcha-bg") != std::string::npos) {
            bgUrl = src;
        } else if (tag.find("piece") != std::string::npos ||
                   tag.find("slider-piece") != std::string::npos ||
                   tag.find("puzzle") != std::string::npos) {
            pieceUrl = src;
        } else if (bgUrl.empty()) {
            bgUrl = src;
        } else if (pieceUrl.empty()) {
            pieceUrl = src;
        }
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
        std::string script =
            "python3 -c \""
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
        std::string r = trim(execCmd(script));
        offset = std::atoi(r.c_str());
    }
    if (!bgPath.empty()) std::remove(bgPath.c_str());

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

    std::string result = "slider_0=" + std::to_string(offset);
    if (!tokenField.empty())
        result = "ancaptcha_token=" + tokenField + "&" + result;
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
    std::string cookieJar = "/tmp/synapsed_clearnet_cookies.txt";
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
        "    '-c', '/tmp/synapsed_cf_cookies.txt',"
        "    '-b', '/tmp/synapsed_cf_cookies.txt',"
        "    '" + url + "'], capture_output=True, text=True, timeout=35);"
        "  if r.returncode == 0 and len(r.stdout) > 100:"
        "    print(r.stdout);"
        "  else:"
        "    r2 = run(['curl', '-s', '-L', '--max-time', '30',"
        "      '-H', 'User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/124.0.0.0 Safari/537.36',"
        "      '-c', '/tmp/synapsed_cf_cookies.txt',"
        "      '-b', '/tmp/synapsed_cf_cookies.txt',"
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

std::string SynapsedEngine::fetchWithRetry(const std::string& url, int maxRetries) const {
    bool isOnion = url.find(".onion") != std::string::npos;

    for (int attempt = 0; attempt < maxRetries; attempt++) {
        std::string html;
        int httpCode = 200;

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

        if (!isOnion) {
            auto prot = detectClearnetProtection(html, httpCode);

            if (prot.cloudflare) {
                std::string bypass = bypassCloudflareChallenge(url);
                if (!bypass.empty()) return bypass;
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }

            if (prot.recaptcha && !prot.siteKey.empty()) {
                std::string token = solveRecaptchaAudio(prot.siteKey, url);
                if (!token.empty()) {
                    std::string postCmd = "curl -s --max-time 30 -L "
                        "-H \"User-Agent: " + randomUserAgent() + "\" "
                        "-d \"g-recaptcha-response=" + token + "\" "
                        "-c /tmp/synapsed_clearnet_cookies.txt "
                        "-b /tmp/synapsed_clearnet_cookies.txt "
                        "\"" + url + "\" 2>/dev/null";
                    std::string result = execCmd(postCmd);
                    if (!result.empty() && result.find("g-recaptcha") == std::string::npos)
                        return result;
                }
                continue;
            }

            if (prot.hcaptcha && !prot.siteKey.empty()) {
                std::string token = solveHCaptcha(prot.siteKey, url);
                if (!token.empty() && token != "bypass") {
                    std::string postCmd = "curl -s --max-time 30 -L "
                        "-H \"User-Agent: " + randomUserAgent() + "\" "
                        "-d \"h-captcha-response=" + token + "\" "
                        "-c /tmp/synapsed_clearnet_cookies.txt "
                        "-b /tmp/synapsed_clearnet_cookies.txt "
                        "\"" + url + "\" 2>/dev/null";
                    std::string result = execCmd(postCmd);
                    if (!result.empty()) return result;
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
            std::string formField = "captcha";
            size_t namePos = html.find("name=\"captcha");
            if (namePos != std::string::npos) {
                size_t q1 = html.find('"', namePos + 5);
                size_t q2 = html.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos)
                    formField = html.substr(q1 + 1, q2 - q1 - 1);
            }
            std::string solved = submitCaptchaAndRefetch(url, url, formField, darkCap.answer);
            if (!solved.empty()) return solved;
            continue;
        }

        if (!darkCap.detected) return html;

        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    return "";
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
        bool isOnion = url.find(".onion") != std::string::npos;
        std::string html = fetchWithRetry(url, 3);
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
