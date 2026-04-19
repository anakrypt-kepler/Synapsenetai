#include "utils/config.h"
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>

namespace synapse {
namespace utils {

struct Config::Impl {
    std::unordered_map<std::string, std::string> data;
    std::string configPath;
    std::string dataDir;
    std::function<void(const std::string&)> changeCallback;
    mutable std::mutex mtx;
    
    void notifyChange(const std::string& key) {
        if (changeCallback) changeCallback(key);
    }
};

Config::Config() : impl_(std::make_unique<Impl>()) {
    const char* home = std::getenv("HOME");
    if (home) {
        impl_->dataDir = std::string(home) + "/.synapsenet";
    } else {
        impl_->dataDir = ".synapsenet";
    }
    loadDefaults();
}

Config& Config::instance() {
    static Config inst;
    return inst;
}

bool Config::loadDefaults() {
    set("network.port", 8333);
    set("network.rpc_port", 8332);
    set("network.rpc_bind_address", "127.0.0.1");
    set("network.rpc_auth_required", true);
    set("network.rpc_user", "");
    set("network.rpc_password", "");
    set("network.rpc_cookie_file", "rpc.cookie");
    set("network.max_peers", 125);
    set("network.max_inbound", 100);
    set("network.max_outbound", 25);
    set("network.connection_timeout", 5000);
    set("network.enable_upnp", true);
    set("network.enable_natpmp", true);
    set("network.discovery.bootstrap_quarantine_seconds", 600);
    set("network.scale.adaptive_admission", true);
    set("network.scale.deterministic_eviction", true);
    set("network.scale.max_peers_per_ip", 8);
    set("network.scale.max_peers_per_subnet", 32);
    set("network.scale.subnet_prefix_bits", 24);
    set("network.scale.token_bucket_enabled", true);
    set("network.scale.token_bucket_bytes_per_sec", static_cast<int64_t>(4 * 1024 * 1024));
    set("network.scale.token_bucket_bytes_burst", static_cast<int64_t>(8 * 1024 * 1024));
    set("network.scale.token_bucket_msgs_per_sec", 500);
    set("network.scale.token_bucket_msgs_burst", 1000);
    set("network.scale.penalty_malformed", 20);
    set("network.scale.penalty_rate", 10);
    set("network.scale.penalty_half_life_seconds", 900);
    set("network.scale.base_ban_seconds", 120);
    set("network.scale.max_ban_seconds", 3600);
    set("network.scale.overload_mode", true);
    set("network.scale.overload_enter_peer_percent", 90);
    set("network.scale.overload_exit_peer_percent", 70);
    set("network.scale.overload_enter_buffer_bytes", static_cast<int64_t>(128 * 1024 * 1024));
    set("network.scale.overload_exit_buffer_bytes", static_cast<int64_t>(64 * 1024 * 1024));
    set("network.scale.inv_max_items", 256);
    set("network.scale.inv_overload_items", 32);
    set("network.scale.getdata_max_items", 128);
    set("network.scale.getdata_overload_items", 32);
    set("network.scale.gossip_fanout_limit", 64);
    set("network.scale.gossip_dedup_window_seconds", 5);
    set("network.scale.vote_dedup_window_seconds", 600);
    set("network.scale.vote_dedup_max_entries", 20000);
    
    set("wallet.encrypt_by_default", true);
    set("wallet.keypool_size", 100);
    set("wallet.dust_threshold", 546);
    set("wallet.min_relay_fee", 1000);
    
    set("model.context_size", 2048);
    set("model.threads", 4);
    set("model.gpu_layers", 0);
    set("model.enable_gpu", false);
    
    set("consensus.min_validators", 5);
    set("consensus.min_stake", 100);
    set("consensus.validation_timeout", 3600);
    set("consensus.majority_threshold", "0.6");
    set("knowledge.min_validations_required", 3);
    
    set("simulation.enabled", false);
    set("simulation.use_fake_wallet", false);
    set("simulation.use_fake_peers", false);
    set("simulation.use_fake_network", false);
    set("security.quantum_enabled", true);
    set("security.level", "high");
    
    // Model persistence and auto-load
    set("model.last_path", "");
    set("model.last_id", "");
    set("model.last_format", "GGUF");
    set("model.auto_load", true);
    
    // Model scan paths (comma-separated)
    set("model.scan_paths", "~/.synapsenet/models,./models,./third_party/llama.cpp/models");

    set("poe.epoch_budget", static_cast<int64_t>(100000000));
    set("poe.epoch_iterations", 20);
    set("poe.epoch.auto_require_new_finalized", true);
    set("poe.validators_adaptive", false);
    set("poe.validators_min_votes", 1);
    set("poe.self_validator_bootstrap_auto_disable", true);
    set("poe.self_validator_bootstrap_strict_connected_peers", 4);
    set("poe.self_validator_bootstrap_strict_known_peers", 8);
    set("poe.self_validator_bootstrap_strict_validator_count", 3);
    set("poe.self_validator_bootstrap_activation_checks", 5);
    set("poe.self_validator_bootstrap_locked_off", false);
    set("poe.self_validator_bootstrap_locked_at", static_cast<int64_t>(0));
    set("poe.self_validator_bootstrap_lock_reason", "");
    set("poe.self_validator_bootstrap_force_allow_until", static_cast<int64_t>(0));

    set("naan.score.initial", static_cast<int64_t>(100));
    set("naan.score.accept_weight", static_cast<int64_t>(12));
    set("naan.score.reject_weight", static_cast<int64_t>(12));
    set("naan.score.violation_weight", static_cast<int64_t>(40));
    set("naan.score.band.throttled_below_or_equal", static_cast<int64_t>(-150));
    set("naan.score.band.review_only_below_or_equal", static_cast<int64_t>(-280));
    set("naan.score.band.local_draft_only_below_or_equal", static_cast<int64_t>(-420));
    set("naan.score.band.local_draft_recovery_above", static_cast<int64_t>(-220));
    set("naan.score.band.local_draft_recovery_clean_steps", static_cast<int64_t>(2));
    set("naan.connector_abuse.policy_block_delta_threshold", static_cast<int64_t>(12));
    set("naan.connector_abuse.failure_delta_threshold", static_cast<int64_t>(60));
    set("naan.connector_abuse.cooldown_ticks", static_cast<int64_t>(30));
    set("naan.connector_abuse.violation_penalty_steps", static_cast<int64_t>(1));
    set("naan.abuse_classifier.spam_loop_penalty", static_cast<int64_t>(1));
    set("naan.abuse_classifier.invalid_citation_penalty", static_cast<int64_t>(1));
    set("naan.abuse_classifier.policy_violation_penalty", static_cast<int64_t>(1));

    set("implant.update.protocol_min", 1);
    set("implant.update.protocol_max", 1);
    set("implant.hal.version", 1);
    set("implant.intent.schema_version", 1);
    set("implant.update.require_safety_gate", true);
    set("implant.update.require_trusted_signer", true);
    set("implant.update.trusted_signers", "");

    set("web.inject.enabled", true);
    set("web.inject.onion", true);
    set("web.inject.tor_clearnet", true);

    set("agent.tor.required", true);
    set("agent.tor.mode", "auto");
    set("agent.tor.socks_host", "127.0.0.1");
    set("agent.tor.socks_port", 9050);
    set("agent.routing.allow_clearnet_fallback", false);
    set("agent.routing.allow_p2p_clearnet_fallback", false);

    set("agent.intent.min_title_bytes", 4);
    set("agent.intent.min_body_bytes", 24);
    set("agent.intent.require_citation_for_text", false);
    set("agent.intent.min_citations_for_text", 0);

    set("agent.retention.max_submitted_drafts", 0);
    set("agent.retention.max_observatory_history", 0);
    set("agent.retention.room.max_messages", 0);
    set("agent.retention.room.seconds", 0);
    
    return true;
}

void Config::reset() {
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->data.clear();
    }
    loadDefaults();
}

bool Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->configPath = path;
    std::string line;
    
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            impl_->data[key] = value;
        }
    }
    return true;
}

bool Config::save(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string savePath = path.empty() ? impl_->configPath : path;
    if (savePath.empty()) return false;
    
    std::ofstream file(savePath);
    if (!file.is_open()) return false;
    
    file << "# SynapseNet Configuration\n\n";
    
    std::vector<std::string> sortedKeys;
    for (const auto& [key, value] : impl_->data) {
        sortedKeys.push_back(key);
    }
    std::sort(sortedKeys.begin(), sortedKeys.end());
    
    std::string lastPrefix;
    for (const auto& key : sortedKeys) {
        auto pos = key.find('.');
        std::string prefix = pos != std::string::npos ? key.substr(0, pos) : "";
        if (prefix != lastPrefix && !lastPrefix.empty()) {
            file << "\n";
        }
        lastPrefix = prefix;
        file << key << "=" << impl_->data[key] << "\n";
    }
    return true;
}

std::string Config::getString(const std::string& key, const std::string& def) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->data.find(key);
    return it != impl_->data.end() ? it->second : def;
}

int Config::getInt(const std::string& key, int def) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->data.find(key);
    if (it == impl_->data.end()) return def;
    try { return std::stoi(it->second); }
    catch (...) { return def; }
}

int64_t Config::getInt64(const std::string& key, int64_t def) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->data.find(key);
    if (it == impl_->data.end()) return def;
    try { return std::stoll(it->second); }
    catch (...) { return def; }
}

double Config::getDouble(const std::string& key, double def) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->data.find(key);
    if (it == impl_->data.end()) return def;
    try { return std::stod(it->second); }
    catch (...) { return def; }
}

bool Config::getBool(const std::string& key, bool def) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->data.find(key);
    if (it == impl_->data.end()) return def;
    std::string val = it->second;
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    return val == "true" || val == "1" || val == "yes" || val == "on";
}

std::vector<std::string> Config::getList(const std::string& key) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::string> result;
    auto it = impl_->data.find(key);
    if (it == impl_->data.end()) return result;
    
    std::istringstream iss(it->second);
    std::string item;
    while (std::getline(iss, item, ',')) {
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

void Config::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data[key] = value;
    impl_->notifyChange(key);
}

void Config::set(const std::string& key, const char* value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data[key] = value ? std::string(value) : std::string();
    impl_->notifyChange(key);
}

void Config::set(const std::string& key, int value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data[key] = std::to_string(value);
    impl_->notifyChange(key);
}

void Config::set(const std::string& key, int64_t value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data[key] = std::to_string(value);
    impl_->notifyChange(key);
}

void Config::set(const std::string& key, double value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data[key] = std::to_string(value);
    impl_->notifyChange(key);
}

void Config::set(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data[key] = value ? "true" : "false";
    impl_->notifyChange(key);
}

void Config::setList(const std::string& key, const std::vector<std::string>& values) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string joined;
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) joined += ",";
        joined += values[i];
    }
    impl_->data[key] = joined;
    impl_->notifyChange(key);
}

bool Config::has(const std::string& key) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->data.find(key) != impl_->data.end();
}

void Config::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data.erase(key);
    impl_->notifyChange(key);
}

std::vector<std::string> Config::keys(const std::string& prefix) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::string> result;
    for (const auto& [key, value] : impl_->data) {
        if (prefix.empty() || key.substr(0, prefix.size()) == prefix) {
            result.push_back(key);
        }
    }
    return result;
}

NetworkConfig Config::getNetworkConfig() const {
    NetworkConfig cfg;
    cfg.port = getInt("network.port", 8333);
    cfg.rpcPort = getInt("network.rpc_port", 8332);
    cfg.rpcBindAddress = getString("network.rpc_bind_address", "127.0.0.1");
    cfg.rpcAuthRequired = getBool("network.rpc_auth_required", true);
    cfg.rpcUser = getString("network.rpc_user", "");
    cfg.rpcPassword = getString("network.rpc_password", "");
    cfg.rpcCookieFile = getString("network.rpc_cookie_file", "rpc.cookie");
    cfg.maxPeers = getInt("network.max_peers", 125);
    cfg.maxInbound = getInt("network.max_inbound", 100);
    cfg.maxOutbound = getInt("network.max_outbound", 25);
    cfg.connectionTimeout = getInt("network.connection_timeout", 5000);
    cfg.enableUpnp = getBool("network.enable_upnp", true);
    cfg.enableNatPmp = getBool("network.enable_natpmp", true);
    cfg.seedNodes = getList("network.seed_nodes");
    cfg.connectOnly = getList("network.connect_only");
    return cfg;
}

WalletConfig Config::getWalletConfig() const {
    WalletConfig cfg;
    cfg.dataDir = getString("wallet.data_dir", impl_->dataDir);
    cfg.walletFile = getString("wallet.file", "wallet.dat");
    cfg.encryptByDefault = getBool("wallet.encrypt_by_default", true);
    cfg.keyPoolSize = getInt("wallet.keypool_size", 100);
    cfg.dustThreshold = getInt64("wallet.dust_threshold", 546);
    cfg.minRelayTxFee = getInt64("wallet.min_relay_fee", 1000);
    return cfg;
}

ModelConfig Config::getModelConfig() const {
    ModelConfig cfg;
    cfg.modelDir = getString("model.dir", impl_->dataDir + "/models");
    cfg.contextSize = getInt("model.context_size", 2048);
    cfg.threads = getInt("model.threads", 4);
    cfg.gpuLayers = getInt("model.gpu_layers", 0);
    cfg.maxMemory = getInt64("model.max_memory", 4ULL * 1024 * 1024 * 1024);
    cfg.enableGpu = getBool("model.enable_gpu", false);
    return cfg;
}

ConsensusConfig Config::getConsensusConfig() const {
    ConsensusConfig cfg;
    cfg.minValidators = getInt("consensus.min_validators", 5);
    cfg.minStake = getInt64("consensus.min_stake", 100);
    cfg.validationTimeout = getInt("consensus.validation_timeout", 3600);
    cfg.majorityThreshold = getDouble("consensus.majority_threshold", 0.6);
    return cfg;
}

void Config::setNetworkConfig(const NetworkConfig& cfg) {
    set("network.port", static_cast<int>(cfg.port));
    set("network.rpc_port", static_cast<int>(cfg.rpcPort));
    set("network.rpc_bind_address", cfg.rpcBindAddress);
    set("network.rpc_auth_required", cfg.rpcAuthRequired);
    set("network.rpc_user", cfg.rpcUser);
    set("network.rpc_password", cfg.rpcPassword);
    set("network.rpc_cookie_file", cfg.rpcCookieFile);
    set("network.max_peers", static_cast<int>(cfg.maxPeers));
    set("network.max_inbound", static_cast<int>(cfg.maxInbound));
    set("network.max_outbound", static_cast<int>(cfg.maxOutbound));
    set("network.connection_timeout", static_cast<int>(cfg.connectionTimeout));
    set("network.enable_upnp", cfg.enableUpnp);
    set("network.enable_natpmp", cfg.enableNatPmp);
    setList("network.seed_nodes", cfg.seedNodes);
    setList("network.connect_only", cfg.connectOnly);
}

void Config::setWalletConfig(const WalletConfig& cfg) {
    set("wallet.data_dir", cfg.dataDir);
    set("wallet.file", cfg.walletFile);
    set("wallet.encrypt_by_default", cfg.encryptByDefault);
    set("wallet.keypool_size", static_cast<int>(cfg.keyPoolSize));
    set("wallet.dust_threshold", static_cast<int64_t>(cfg.dustThreshold));
    set("wallet.min_relay_fee", static_cast<int64_t>(cfg.minRelayTxFee));
}

void Config::setModelConfig(const ModelConfig& cfg) {
    set("model.dir", cfg.modelDir);
    set("model.context_size", static_cast<int>(cfg.contextSize));
    set("model.threads", static_cast<int>(cfg.threads));
    set("model.gpu_layers", static_cast<int>(cfg.gpuLayers));
    set("model.max_memory", static_cast<int64_t>(cfg.maxMemory));
    set("model.enable_gpu", cfg.enableGpu);
}

void Config::setConsensusConfig(const ConsensusConfig& cfg) {
    set("consensus.min_validators", static_cast<int>(cfg.minValidators));
    set("consensus.min_stake", static_cast<int64_t>(cfg.minStake));
    set("consensus.validation_timeout", static_cast<int>(cfg.validationTimeout));
    set("consensus.majority_threshold", cfg.majorityThreshold);
}

void Config::onChange(std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->changeCallback = callback;
}

std::string Config::getDataDir() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->dataDir;
}

std::string Config::getConfigPath() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->configPath;
}

void Config::setDataDir(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->dataDir = path;
}

bool Config::hasKey(const std::string& key) const {
    return has(key);
}

std::vector<std::string> Config::getKeys() const {
    return keys("");
}

std::vector<std::string> Config::getKeysWithPrefix(const std::string& prefix) const {
    return keys(prefix);
}

void Config::clear() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->data.clear();
}

void Config::merge(const Config& other) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    for (const auto& key : other.getKeys()) {
        impl_->data[key] = other.getString(key);
    }
}

std::string Config::toJson() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string json = "{\n";
    bool first = true;
    for (const auto& [k, v] : impl_->data) {
        if (!first) json += ",\n";
        json += "  \"" + k + "\": \"" + v + "\"";
        first = false;
    }
    json += "\n}";
    return json;
}

void Config::fromJson(const std::string& json) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    size_t pos = 0;
    while ((pos = json.find("\"", pos)) != std::string::npos) {
        size_t keyStart = pos + 1;
        size_t keyEnd = json.find("\"", keyStart);
        if (keyEnd == std::string::npos) break;
        
        std::string key = json.substr(keyStart, keyEnd - keyStart);
        
        size_t valStart = json.find("\"", keyEnd + 1);
        if (valStart == std::string::npos) break;
        valStart++;
        size_t valEnd = json.find("\"", valStart);
        if (valEnd == std::string::npos) break;
        
        std::string val = json.substr(valStart, valEnd - valStart);
        impl_->data[key] = val;
        
        pos = valEnd + 1;
    }
}

size_t Config::size() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->data.size();
}

bool Config::empty() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->data.empty();
}

}
}
