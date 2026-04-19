#include "utils/config.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

using synapse::utils::Config;

static bool containsLine(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static bool testDefaultsKeepStringLiterals() {
    auto& cfg = Config::instance();
    cfg.reset();

    if (cfg.getString("security.level", "") != "high") return false;
    if (cfg.getString("agent.tor.socks_host", "") != "127.0.0.1") return false;
    if (cfg.getString("model.last_format", "") != "GGUF") return false;
    if (cfg.getString("consensus.majority_threshold", "") != "0.6") return false;

    return true;
}

static bool testEpochAndNaanDefaults() {
    auto& cfg = Config::instance();
    cfg.reset();

    if (!cfg.getBool("poe.epoch.auto_require_new_finalized", false)) return false;
    if (cfg.getInt64("naan.score.initial", 0) != 100) return false;
    if (cfg.getInt64("naan.score.reject_weight", 0) != 12) return false;
    if (cfg.getInt64("naan.score.violation_weight", 0) != 40) return false;
    if (cfg.getInt64("naan.connector_abuse.violation_penalty_steps", 0) != 1) return false;
    if (cfg.getInt64("naan.abuse_classifier.policy_violation_penalty", 0) != 1) return false;

    return true;
}

static bool testExplicitCStringSetSavesVerbatim() {
    auto& cfg = Config::instance();
    cfg.reset();
    cfg.set("test.string.literal", "abc");
    cfg.set("test.empty.literal", "");

    const std::string path = "/tmp/synapsenet_test_config_literals.conf";
    if (!cfg.save(path)) return false;

    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::remove(path.c_str());

    if (!containsLine(content, "test.string.literal=abc")) return false;
    if (!containsLine(content, "test.empty.literal=")) return false;
    if (containsLine(content, "test.string.literal=true")) return false;

    return true;
}

int main() {
    if (!testDefaultsKeepStringLiterals()) {
        std::cerr << "testDefaultsKeepStringLiterals failed\n";
        return 1;
    }
    if (!testExplicitCStringSetSavesVerbatim()) {
        std::cerr << "testExplicitCStringSetSavesVerbatim failed\n";
        return 1;
    }
    if (!testEpochAndNaanDefaults()) {
        std::cerr << "testEpochAndNaanDefaults failed\n";
        return 1;
    }
    return 0;
}
