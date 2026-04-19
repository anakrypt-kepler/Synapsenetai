#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <cstdint>

namespace synapse {
namespace utils {

struct NetworkConfig {
    uint16_t port = 8333;
    uint16_t rpcPort = 8332;
    std::string rpcBindAddress = "127.0.0.1";
    bool rpcAuthRequired = true;
    std::string rpcUser;
    std::string rpcPassword;
    std::string rpcCookieFile = "rpc.cookie";
    uint32_t maxPeers = 125;
    uint32_t maxInbound = 100;
    uint32_t maxOutbound = 25;
    uint32_t connectionTimeout = 5000;
    bool enableUpnp = true;
    bool enableNatPmp = true;
    std::vector<std::string> seedNodes;
    std::vector<std::string> connectOnly;
};

struct WalletConfig {
    std::string dataDir;
    std::string walletFile = "wallet.dat";
    bool encryptByDefault = true;
    uint32_t keyPoolSize = 100;
    uint64_t dustThreshold = 546;
    uint64_t minRelayTxFee = 1000;
};

struct ModelConfig {
    std::string modelDir;
    uint32_t contextSize = 2048;
    uint32_t threads = 4;
    uint32_t gpuLayers = 0;
    uint64_t maxMemory = 4ULL * 1024 * 1024 * 1024;
    bool enableGpu = false;
};

struct ConsensusConfig {
    uint32_t minValidators = 5;
    uint64_t minStake = 100;
    uint32_t validationTimeout = 3600;
    double majorityThreshold = 0.6;
};

class Config {
public:
    static Config& instance();
    
    bool load(const std::string& path);
    bool save(const std::string& path);
    bool loadDefaults();
    void reset();
    
    std::string getString(const std::string& key, const std::string& def = "") const;
    int getInt(const std::string& key, int def = 0) const;
    int64_t getInt64(const std::string& key, int64_t def = 0) const;
    double getDouble(const std::string& key, double def = 0.0) const;
    bool getBool(const std::string& key, bool def = false) const;
    std::vector<std::string> getList(const std::string& key) const;
    
    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, const char* value);
    void set(const std::string& key, int value);
    void set(const std::string& key, int64_t value);
    void set(const std::string& key, double value);
    void set(const std::string& key, bool value);
    void setList(const std::string& key, const std::vector<std::string>& values);
    
    bool has(const std::string& key) const;
    void remove(const std::string& key);
    std::vector<std::string> keys(const std::string& prefix = "") const;
    
    NetworkConfig getNetworkConfig() const;
    WalletConfig getWalletConfig() const;
    ModelConfig getModelConfig() const;
    ConsensusConfig getConsensusConfig() const;
    
    void setNetworkConfig(const NetworkConfig& config);
    void setWalletConfig(const WalletConfig& config);
    void setModelConfig(const ModelConfig& config);
    void setConsensusConfig(const ConsensusConfig& config);
    
    void onChange(std::function<void(const std::string&)> callback);
    
    std::string getDataDir() const;
    std::string getConfigPath() const;
    void setDataDir(const std::string& path);
    
    bool hasKey(const std::string& key) const;
    std::vector<std::string> getKeys() const;
    std::vector<std::string> getKeysWithPrefix(const std::string& prefix) const;
    void clear();
    void merge(const Config& other);
    
    std::string toJson() const;
    void fromJson(const std::string& json);
    
    size_t size() const;
    bool empty() const;
    
    // Simulation mode helpers
    bool isSimulationEnabled() const { return getBool("simulation.enabled", true); }
    void setSimulationEnabled(bool enabled) { set("simulation.enabled", enabled); }
    bool isFakeWalletEnabled() const { return getBool("simulation.use_fake_wallet", true); }
    bool isFakePeersEnabled() const { return getBool("simulation.use_fake_peers", true); }
    bool isFakeNetworkEnabled() const { return getBool("simulation.use_fake_network", true); }
    
    // Model persistence helpers (Task 3)
    std::string getLastModelPath() const { return getString("model.last_path", ""); }
    void setLastModelPath(const std::string& path) { set("model.last_path", path); }
    std::string getLastModelId() const { return getString("model.last_id", ""); }
    void setLastModelId(const std::string& id) { set("model.last_id", id); }
    std::string getLastModelFormat() const { return getString("model.last_format", "GGUF"); }
    void setLastModelFormat(const std::string& fmt) { set("model.last_format", fmt); }
    bool getAutoLoadModel() const { return getBool("model.auto_load", true); }
    void setAutoLoadModel(bool enabled) { set("model.auto_load", enabled); }
    
    // Model scan paths helpers (Task 4)
    std::vector<std::string> getModelScanPaths() const { return getList("model.scan_paths"); }
    void setModelScanPaths(const std::vector<std::string>& paths) { setList("model.scan_paths", paths); }
    void addModelScanPath(const std::string& path) {
        auto paths = getModelScanPaths();
        paths.push_back(path);
        setModelScanPaths(paths);
    }
    void removeModelScanPath(const std::string& path) {
        auto paths = getModelScanPaths();
        paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
        setModelScanPaths(paths);
    }
    
private:
    Config();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
