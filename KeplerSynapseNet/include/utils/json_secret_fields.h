#pragma once

// First-party gate in front of nlohmann::json. String values for a wallet
// seed, an RPC cookie, or an RPC password are erased before that library
// sees the text. The saved Tor control password stays, because settings are
// written back. Numeric fields, including an inference seed, stay as they are.

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace synapse {
namespace json_containment {

inline int jsonHexNibble(char h) {
    if (h >= '0' && h <= '9') return h - '0';
    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
    return -1;
}

inline bool isSecretJsonKey(const std::string& key) {
    // tor_control_password stays. Settings are parsed and written back, and
    // that value is the saved control password, not a field to drop.
    return key == "authToken" || key == "mnemonic" || key == "seed"
        || key == "rpc_password" || key == "rpcpassword";
}

inline bool textMentionsSecretKey(const std::string& text) {
    static const char* keys[] = {
        "\"authToken\"",
        "\"mnemonic\"",
        "\"seed\"",
        "\"rpc_password\"",
        "\"rpcpassword\"",
    };
    for (const char* key : keys) {
        if (text.find(key) != std::string::npos) return true;
    }
    // A \u escape can spell one of the keys above without the literal quotes.
    return text.find("\\u") != std::string::npos;
}

inline void skipJsonWs(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

inline bool appendJsonCodePoint(std::string& out, int cp) {
    if (cp < 0 || cp > 0x10FFFF) return false;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return true;
}

inline bool readJsonString(const std::string& s, size_t& i, std::string* decoded,
                           size_t* interiorBegin, size_t* interiorEnd) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    const size_t begin = i;
    std::string out;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"') {
            if (decoded) *decoded = std::move(out);
            if (interiorBegin) *interiorBegin = begin;
            if (interiorEnd) *interiorEnd = i;
            ++i;
            return true;
        }
        if (c == '\\') {
            if (i + 1 >= s.size()) return false;
            const char e = s[i + 1];
            if (e == '"' || e == '\\' || e == '/') {
                out.push_back(e);
                i += 2;
                continue;
            }
            if (e == 'b' || e == 'f' || e == 'n' || e == 'r' || e == 't') {
                const char plain = e == 'b' ? '\b' : e == 'f' ? '\f' : e == 'n' ? '\n' : e == 'r' ? '\r' : '\t';
                out.push_back(plain);
                i += 2;
                continue;
            }
            if (e != 'u' || i + 5 >= s.size()) return false;
            int cp = 0;
            for (int k = 0; k < 4; ++k) {
                const int h = jsonHexNibble(s[i + 2 + k]);
                if (h < 0) return false;
                cp = (cp << 4) + h;
            }
            i += 6;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i + 6 > s.size() || s[i] != '\\' || s[i + 1] != 'u') return false;
                int low = 0;
                for (int k = 0; k < 4; ++k) {
                    const int h = jsonHexNibble(s[i + 2 + k]);
                    if (h < 0) return false;
                    low = (low << 4) + h;
                }
                i += 6;
                if (low < 0xDC00 || low > 0xDFFF) return false;
                cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return false;
            }
            if (!appendJsonCodePoint(out, cp)) return false;
            continue;
        }
        if (c < 0x20) return false;
        out.push_back(static_cast<char>(c));
        ++i;
    }
    return false;
}

using JsonCut = std::pair<size_t, size_t>;

inline bool skipJsonValue(const std::string& s, size_t& i, std::vector<JsonCut>& cuts,
                          bool blankString, int depth);

inline bool skipJsonNumber(const std::string& s, size_t& i) {
    const size_t start = i;
    if (i < s.size() && s[i] == '-') ++i;
    if (i >= s.size() || std::isdigit(static_cast<unsigned char>(s[i])) == 0) return false;
    if (s[i] == '0') {
        ++i;
    } else {
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        if (i >= s.size() || std::isdigit(static_cast<unsigned char>(s[i])) == 0) return false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        if (i >= s.size() || std::isdigit(static_cast<unsigned char>(s[i])) == 0) return false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    }
    return i > start;
}

inline bool skipJsonLiteral(const std::string& s, size_t& i, const char* lit, size_t n) {
    if (s.compare(i, n, lit) != 0) return false;
    i += n;
    return true;
}

inline bool skipJsonObject(const std::string& s, size_t& i, std::vector<JsonCut>& cuts, int depth) {
    if (depth > 64) return false;
    if (i >= s.size() || s[i] != '{') return false;
    ++i;
    skipJsonWs(s, i);
    if (i < s.size() && s[i] == '}') {
        ++i;
        return true;
    }
    while (i < s.size()) {
        std::string key;
        if (!readJsonString(s, i, &key, nullptr, nullptr)) return false;
        skipJsonWs(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        if (!skipJsonValue(s, i, cuts, isSecretJsonKey(key), depth + 1)) return false;
        skipJsonWs(s, i);
        if (i < s.size() && s[i] == ',') {
            ++i;
            skipJsonWs(s, i);
            continue;
        }
        if (i < s.size() && s[i] == '}') {
            ++i;
            return true;
        }
        return false;
    }
    return false;
}

inline bool skipJsonArray(const std::string& s, size_t& i, std::vector<JsonCut>& cuts, int depth) {
    if (depth > 64) return false;
    if (i >= s.size() || s[i] != '[') return false;
    ++i;
    skipJsonWs(s, i);
    if (i < s.size() && s[i] == ']') {
        ++i;
        return true;
    }
    while (i < s.size()) {
        if (!skipJsonValue(s, i, cuts, false, depth + 1)) return false;
        skipJsonWs(s, i);
        if (i < s.size() && s[i] == ',') {
            ++i;
            skipJsonWs(s, i);
            continue;
        }
        if (i < s.size() && s[i] == ']') {
            ++i;
            return true;
        }
        return false;
    }
    return false;
}

inline bool skipJsonValue(const std::string& s, size_t& i, std::vector<JsonCut>& cuts,
                          bool blankString, int depth) {
    if (depth > 64) return false;
    skipJsonWs(s, i);
    if (i >= s.size()) return false;
    const char c = s[i];
    if (c == '"') {
        size_t begin = 0;
        size_t end = 0;
        if (!readJsonString(s, i, nullptr, &begin, &end)) return false;
        if (blankString) cuts.emplace_back(begin, end);
        return true;
    }
    if (c == '{') return skipJsonObject(s, i, cuts, depth);
    if (c == '[') return skipJsonArray(s, i, cuts, depth);
    if (c == 't') return skipJsonLiteral(s, i, "true", 4);
    if (c == 'f') return skipJsonLiteral(s, i, "false", 5);
    if (c == 'n') return skipJsonLiteral(s, i, "null", 4);
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return skipJsonNumber(s, i);
    return false;
}

// True when text can be parsed. Secret string values are already removed.
inline bool blankJsonSecretStrings(std::string& text) {
    if (!textMentionsSecretKey(text)) return true;
    std::vector<JsonCut> cuts;
    size_t i = 0;
    if (!skipJsonValue(text, i, cuts, false, 0)) return false;
    skipJsonWs(text, i);
    if (i != text.size()) return false;
    for (size_t n = cuts.size(); n-- > 0;) {
        text.erase(cuts[n].first, cuts[n].second - cuts[n].first);
    }
    return true;
}

}  // namespace json_containment
}  // namespace synapse
