#include "config/config_loader.h"

#include "crypto/crypto.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace synapse {

namespace {

std::string trimAsciiString(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::unordered_map<std::string, std::string> readSimpleConfigFile(const std::string& path) {
    std::unordered_map<std::string, std::string> values;
    std::ifstream file(path);
    if (!file.is_open()) {
        return values;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trimAsciiString(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = trimAsciiString(line.substr(0, pos));
        std::string value = trimAsciiString(line.substr(pos + 1));
        values[key] = value;
    }
    return values;
}

std::string configValueOrDefault(const std::unordered_map<std::string, std::string>& values,
                                 const std::string& key,
                                 const std::string& fallback) {
    auto it = values.find(key);
    if (it == values.end()) {
        return fallback;
    }
    return it->second;
}

std::string readFileTrimmed(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return trimAsciiString(buffer.str());
}

} // namespace

std::string resolveRpcCookiePath(const std::string& dataDir, const std::string& cookieFile) {
    if (cookieFile.empty()) {
        return (std::filesystem::path(dataDir) / "rpc.cookie").string();
    }
    std::filesystem::path path(cookieFile);
    if (path.is_absolute()) {
        return path.string();
    }
    return (std::filesystem::path(dataDir) / path).string();
}

std::string makeBasicAuthorizationValue(const std::string& user, const std::string& password) {
    const std::string raw = user + ":" + password;
    std::vector<uint8_t> input(raw.begin(), raw.end());
    const auto encoded = crypto::base64Encode(input);
    return "Basic " + std::string(encoded.begin(), encoded.end());
}

std::string buildRpcClientAuthHeader(const NodeConfig& config) {
    const std::string configPath = config.configPath.empty()
        ? (std::filesystem::path(config.dataDir) / "synapsenet.conf").string()
        : config.configPath;
    const auto values = readSimpleConfigFile(configPath);

    const std::string rpcUser = configValueOrDefault(
        values,
        "network.rpc_user",
        configValueOrDefault(values, "rpcuser", ""));
    const std::string rpcPassword = configValueOrDefault(
        values,
        "network.rpc_password",
        configValueOrDefault(values, "rpcpassword", ""));
    if (!rpcUser.empty() && !rpcPassword.empty()) {
        return makeBasicAuthorizationValue(rpcUser, rpcPassword);
    }

    const std::string cookieFile = configValueOrDefault(values, "network.rpc_cookie_file", "rpc.cookie");
    const std::string token = readFileTrimmed(resolveRpcCookiePath(config.dataDir, cookieFile));
    if (!token.empty()) {
        return "Bearer " + token;
    }
    return "";
}

} // namespace synapse
