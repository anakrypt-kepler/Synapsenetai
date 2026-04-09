#include "core/poe_v1.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <sstream>
#include <string_view>

namespace synapse::core::poe_v1 {

std::string canonicalizeText(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    bool inSpace = false;
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c == '\r') continue;
        if (std::isspace(c)) {
            if (!out.empty()) inSpace = true;
            continue;
        }
        if (inSpace) {
            out.push_back(' ');
            inSpace = false;
        }
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        out.push_back(static_cast<char>(c));
    }

    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string canonicalizeCode(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c == '\r') continue;
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string canonicalizeCodeForSimhash(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    bool inSpace = false;
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c == '\r') continue;
        if (std::isspace(c)) {
            if (!out.empty()) inSpace = true;
            continue;
        }
        if (inSpace) {
            out.push_back(' ');
            inSpace = false;
        }
        out.push_back(static_cast<char>(c));
    }

    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static uint64_t readU64BE(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<uint64_t>(p[i]);
    return v;
}

uint64_t simhash64(const std::string& canonicalText) {
    if (canonicalText.empty()) return 0;

    std::array<int32_t, 64> acc{};
    size_t start = 0;
    while (start < canonicalText.size()) {
        size_t end = canonicalText.find(' ', start);
        if (end == std::string::npos) end = canonicalText.size();
        if (end > start) {
            std::string token = canonicalText.substr(start, end - start);
            auto h = crypto::sha256(reinterpret_cast<const uint8_t*>(token.data()), token.size());
            uint64_t hv = readU64BE(h.data());
            for (int b = 0; b < 64; ++b) {
                if ((hv >> b) & 1ULL) acc[b] += 1;
                else acc[b] -= 1;
            }
        }
        start = end + 1;
    }

    uint64_t out = 0;
    for (int b = 0; b < 64; ++b) {
        if (acc[b] > 0) out |= (1ULL << b);
    }
    return out;
}

uint32_t hammingDistance64(uint64_t a, uint64_t b) {
    uint64_t x = a ^ b;
    uint32_t count = 0;
    while (x) {
        x &= (x - 1);
        ++count;
    }
    return count;
}

static uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static std::vector<std::string_view> splitWords(const std::string& s) {
    std::vector<std::string_view> words;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        if (i >= s.size()) break;
        size_t j = i;
        while (j < s.size() && s[j] != ' ') ++j;
        words.emplace_back(s.data() + i, j - i);
        i = j;
    }
    return words;
}

MinHash16 minhash16(const std::string& canonicalText, uint32_t shingleWords) {
    MinHash16 sig{};
    sig.fill(std::numeric_limits<uint64_t>::max());

    if (canonicalText.empty()) {
        sig.fill(0);
        return sig;
    }

    if (shingleWords == 0) shingleWords = 1;
    auto words = splitWords(canonicalText);
    if (words.empty()) {
        sig.fill(0);
        return sig;
    }
    if (words.size() < shingleWords) shingleWords = static_cast<uint32_t>(words.size());

    static constexpr std::array<uint64_t, 16> SEEDS = {
        0x243f6a8885a308d3ULL, 0x13198a2e03707344ULL, 0xa4093822299f31d0ULL, 0x082efa98ec4e6c89ULL,
        0x452821e638d01377ULL, 0xbe5466cf34e90c6cULL, 0xc0ac29b7c97c50ddULL, 0x3f84d5b5b5470917ULL,
        0x9216d5d98979fb1bULL, 0xd1310ba698dfb5acULL, 0x2ffd72dbd01adfb7ULL, 0xb8e1afed6a267e96ULL,
        0xba7c9045f12c7f99ULL, 0x24a19947b3916cf7ULL, 0x0801f2e2858efc16ULL, 0x636920d871574e69ULL
    };

    const size_t total = words.size();
    const size_t k = static_cast<size_t>(shingleWords);

    std::string shingle;
    for (size_t i = 0; i + k <= total; ++i) {
        size_t cap = 0;
        for (size_t j = 0; j < k; ++j) cap += words[i + j].size() + 1;
        shingle.clear();
        shingle.reserve(cap);
        for (size_t j = 0; j < k; ++j) {
            if (j) shingle.push_back('\n');
            auto w = words[i + j];
            shingle.append(w.data(), w.size());
        }

        auto h = crypto::sha256(reinterpret_cast<const uint8_t*>(shingle.data()), shingle.size());
        uint64_t base = readU64BE(h.data());
        for (size_t s = 0; s < sig.size(); ++s) {
            uint64_t mixed = splitmix64(base ^ SEEDS[s]);
            if (mixed < sig[s]) sig[s] = mixed;
        }
    }

    for (auto& v : sig) {
        if (v == std::numeric_limits<uint64_t>::max()) v = 0;
    }
    return sig;
}

uint32_t minhashEqualCount(const MinHash16& a, const MinHash16& b) {
    uint32_t eq = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == b[i]) ++eq;
    }
    return eq;
}

bool hasLeadingZeroBits(const crypto::Hash256& hash, uint32_t zeroBits) {
    if (zeroBits == 0) return true;
    if (zeroBits > 256) return false;

    uint32_t fullBytes = zeroBits / 8;
    uint32_t remBits = zeroBits % 8;

    for (uint32_t i = 0; i < fullBytes; ++i) {
        if (hash[i] != 0) return false;
    }
    if (remBits == 0) return true;

    uint8_t mask = static_cast<uint8_t>(0xFFu << (8 - remBits));
    return (hash[fullBytes] & mask) == 0;
}

static void writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static bool pubKeyLess(const crypto::PublicKey& a, const crypto::PublicKey& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

std::vector<crypto::PublicKey> selectValidators(
    const crypto::Hash256& prevBlockHash,
    const crypto::Hash256& submitId,
    const std::vector<crypto::PublicKey>& validatorSet,
    uint32_t count
) {
    if (count == 0) return {};
    if (validatorSet.empty()) return {};

    std::vector<crypto::PublicKey> sorted = validatorSet;
    std::sort(sorted.begin(), sorted.end(), pubKeyLess);
    if (sorted.size() <= count) return sorted;

    std::vector<uint8_t> seedBuf;
    seedBuf.insert(seedBuf.end(), prevBlockHash.begin(), prevBlockHash.end());
    seedBuf.insert(seedBuf.end(), submitId.begin(), submitId.end());
    crypto::Hash256 seed = crypto::sha256(seedBuf.data(), seedBuf.size());

    std::vector<crypto::PublicKey> selected;
    selected.reserve(count);
    std::vector<uint8_t> used(sorted.size(), 0);

    for (uint32_t i = 0; i < count; ++i) {
        bool picked = false;
        for (uint32_t j = 0; j < static_cast<uint32_t>(sorted.size()); ++j) {
            std::vector<uint8_t> pickBuf;
            pickBuf.insert(pickBuf.end(), seed.begin(), seed.end());
            writeU32LE(pickBuf, i);
            writeU32LE(pickBuf, j);
            crypto::Hash256 pick = crypto::sha256(pickBuf.data(), pickBuf.size());
            uint64_t r = readU64BE(pick.data());
            size_t idx = static_cast<size_t>(r % static_cast<uint64_t>(sorted.size()));
            if (used[idx]) continue;
            used[idx] = 1;
            selected.push_back(sorted[idx]);
            picked = true;
            break;
        }
        if (!picked) {
            for (size_t idx = 0; idx < sorted.size(); ++idx) {
                if (used[idx]) continue;
                used[idx] = 1;
                selected.push_back(sorted[idx]);
                picked = true;
                break;
            }
        }
        if (!picked) break;
    }

    return selected;
}

}
