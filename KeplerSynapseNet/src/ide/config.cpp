#include "ide/config.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace synapse {
namespace ide {

struct IdeConfig::Impl {
    mutable std::mutex mtx;
    std::unordered_map<std::string, std::string> data;
    std::string workingDir;
    std::vector<std::string> contextPaths;
    std::vector<std::string> skillsPaths;
};

IdeConfig::IdeConfig() : impl_(std::make_unique<Impl>()) {}
IdeConfig::~IdeConfig() = default;

bool IdeConfig::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::ifstream file(path);
    if (!file) return false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        auto trimStart = key.find_first_not_of(" \t");
        auto trimEnd = key.find_last_not_of(" \t");
        if (trimStart != std::string::npos)
            key = key.substr(trimStart, trimEnd - trimStart + 1);

        trimStart = value.find_first_not_of(" \t");
        trimEnd = value.find_last_not_of(" \t");
        if (trimStart != std::string::npos)
            value = value.substr(trimStart, trimEnd - trimStart + 1);

        impl_->data[key] = value;
    }

    auto it = impl_->data.find("ide.working_dir");
    if (it != impl_->data.end()) {
        impl_->workingDir = it->second;
    }

    return true;
}

bool IdeConfig::save(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::ofstream file(path, std::ios::trunc);
    if (!file) return false;

    std::vector<std::string> sortedKeys;
    sortedKeys.reserve(impl_->data.size());
    for (const auto& kv : impl_->data) {
        sortedKeys.push_back(kv.first);
    }
    std::sort(sortedKeys.begin(), sortedKeys.end());

    std::string lastPrefix;
    for (const auto& key : sortedKeys) {
        auto pos = key.find('.');
        std::string prefix = (pos != std::string::npos) ? key.substr(0, pos) : "";
        if (!lastPrefix.empty() && prefix != lastPrefix) {
            file << "\n";
        }
        lastPrefix = prefix;
        file << key << "=" << impl_->data.at(key) << "\n";
    }
    return file.good();
}

std::string IdeConfig::getString(const std::string& key, const std::string& def) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->data.find(key);
    if (it == impl_->data.end()) return def;
    return it->second;
}

int IdeConfig::getInt(const std::string& key, int def) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->data.find(key);
    if (it == impl_->data.end()) return def;
    try { return std::stoi(it->second); }
    catch (...) { return def; }
}

bool IdeConfig::getBool(const std::string& key, bool def) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->data.find(key);
    if (it == impl_->data.end()) return def;
    std::string val = it->second;
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    return val == "true" || val == "1" || val == "yes" || val == "on";
}

std::vector<std::string> IdeConfig::getList(const std::string& key) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->data.find(key);
    if (it == impl_->data.end()) return {};
    std::vector<std::string> result;
    std::istringstream iss(it->second);
    std::string item;
    while (std::getline(iss, item, ',')) {
        auto start = item.find_first_not_of(" \t");
        auto end = item.find_last_not_of(" \t");
        if (start != std::string::npos) {
            result.push_back(item.substr(start, end - start + 1));
        }
    }
    return result;
}

void IdeConfig::setString(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data[key] = value;
}

void IdeConfig::setInt(const std::string& key, int value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data[key] = std::to_string(value);
}

void IdeConfig::setBool(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data[key] = value ? "true" : "false";
}

void IdeConfig::setList(const std::string& key, const std::vector<std::string>& values) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string joined;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) joined += ",";
        joined += values[i];
    }
    impl_->data[key] = joined;
}

std::string IdeConfig::workingDir() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->workingDir;
}

void IdeConfig::setWorkingDir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->workingDir = dir;
    impl_->data["ide.working_dir"] = dir;
}

std::vector<std::string> IdeConfig::contextPaths() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->contextPaths;
}

void IdeConfig::setContextPaths(const std::vector<std::string>& paths) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->contextPaths = paths;
}

std::vector<std::string> IdeConfig::skillsPaths() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->skillsPaths;
}

void IdeConfig::setSkillsPaths(const std::vector<std::string>& paths) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->skillsPaths = paths;
}

void IdeConfig::reset() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data.clear();
    impl_->workingDir.clear();
    impl_->contextPaths.clear();
    impl_->skillsPaths.clear();
}

}
}
