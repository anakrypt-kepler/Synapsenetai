#include "core/ledger.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <memory>
#include <filesystem>
#include <chrono>
#include <atomic>
#include <functional>
#include <algorithm>
#include <limits>
#include <sstream>
#include <iomanip>
#include <system_error>
#include <unordered_map>
#include "database/database.h"
#include "core/state_manager.h"

namespace synapse {
namespace core {

namespace {

constexpr const char* kStateSnapshotKey = "state:snapshot";
constexpr const char* kDefaultStateDbName = "state_manager.db";

std::filesystem::path resolveStateDbPath(const std::string& configuredPath) {
    std::filesystem::path candidate =
        configuredPath.empty() ? std::filesystem::path(kDefaultStateDbName)
                               : std::filesystem::path(configuredPath);

    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) &&
        std::filesystem::is_directory(candidate, ec)) {
        return candidate / kDefaultStateDbName;
    }

    if (!candidate.empty() && candidate.filename().empty()) {
        return candidate / kDefaultStateDbName;
    }

    return candidate;
}

bool ensureStateDbParentDir(const std::filesystem::path& dbPath) {
    const std::filesystem::path parent = dbPath.parent_path();
    if (parent.empty()) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    return !ec;
}

void writeU64LE(std::vector<uint8_t>& out, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFFu));
    }
}

void writeU32LE(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFFu));
    }
}

void writeStringLE(std::vector<uint8_t>& out, const std::string& text) {
    writeU32LE(out, static_cast<uint32_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

bool readU64LE(const std::vector<uint8_t>& in, size_t& offset, uint64_t& out) {
    if (offset + 8 > in.size()) {
        return false;
    }

    out = 0;
    for (int i = 0; i < 8; ++i) {
        out |= static_cast<uint64_t>(in[offset++]) << (8 * i);
    }
    return true;
}

bool readU32LE(const std::vector<uint8_t>& in, size_t& offset, uint32_t& out) {
    if (offset + 4 > in.size()) {
        return false;
    }

    out = 0;
    for (int i = 0; i < 4; ++i) {
        out |= static_cast<uint32_t>(in[offset++]) << (8 * i);
    }
    return true;
}

bool readStringLE(const std::vector<uint8_t>& in, size_t& offset, std::string& out) {
    uint32_t size = 0;
    if (!readU32LE(in, offset, size)) {
        return false;
    }
    if (offset + size > in.size()) {
        return false;
    }

    out.assign(reinterpret_cast<const char*>(in.data() + offset), size);
    offset += size;
    return true;
}

std::vector<uint8_t> encodeStateSnapshot(const StateSnapshot& state) {
    if (state.metadata.size() > std::numeric_limits<uint32_t>::max()) {
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve(128);
    out.push_back(static_cast<uint8_t>(state.nodeState));
    out.push_back(static_cast<uint8_t>(state.syncState));
    writeU64LE(out, state.blockHeight);
    writeU64LE(out, state.headerHeight);
    writeU32LE(out, static_cast<uint32_t>(state.peerCount));
    writeU64LE(out, state.memPoolSize);
    writeU64LE(out, state.balance);
    writeU64LE(out, state.pendingBalance);
    out.push_back(state.walletLocked ? 1u : 0u);
    out.push_back(state.miningActive ? 1u : 0u);
    out.push_back(state.modelLoaded ? 1u : 0u);
    writeU64LE(out, state.timestamp);

    writeU32LE(out, static_cast<uint32_t>(state.metadata.size()));
    for (const auto& entry : state.metadata) {
        if (entry.first.size() > std::numeric_limits<uint32_t>::max() ||
            entry.second.size() > std::numeric_limits<uint32_t>::max()) {
            return {};
        }
        writeStringLE(out, entry.first);
        writeStringLE(out, entry.second);
    }

    return out;
}

bool decodeStateSnapshot(const std::vector<uint8_t>& in, StateSnapshot& state) {
    size_t offset = 0;
    if (offset + 2 > in.size()) {
        return false;
    }

    StateSnapshot decoded;
    decoded.nodeState = static_cast<NodeState>(in[offset++]);
    decoded.syncState = static_cast<SyncState>(in[offset++]);
    if (!readU64LE(in, offset, decoded.blockHeight)) {
        return false;
    }
    if (!readU64LE(in, offset, decoded.headerHeight)) {
        return false;
    }
    uint32_t peerCount = 0;
    if (!readU32LE(in, offset, peerCount)) {
        return false;
    }
    decoded.peerCount = static_cast<int>(peerCount);
    if (!readU64LE(in, offset, decoded.memPoolSize)) {
        return false;
    }
    if (!readU64LE(in, offset, decoded.balance)) {
        return false;
    }
    if (!readU64LE(in, offset, decoded.pendingBalance)) {
        return false;
    }
    if (offset + 3 > in.size()) {
        return false;
    }
    decoded.walletLocked = in[offset++] != 0;
    decoded.miningActive = in[offset++] != 0;
    decoded.modelLoaded = in[offset++] != 0;
    if (!readU64LE(in, offset, decoded.timestamp)) {
        return false;
    }

    uint32_t metadataCount = 0;
    if (!readU32LE(in, offset, metadataCount)) {
        return false;
    }
    decoded.metadata.clear();
    for (uint32_t i = 0; i < metadataCount; ++i) {
        std::string key;
        std::string value;
        if (!readStringLE(in, offset, key)) {
            return false;
        }
        if (!readStringLE(in, offset, value)) {
            return false;
        }
        decoded.metadata[key] = value;
    }

    state = std::move(decoded);
    return true;
}

bool saveSnapshotToDb(const std::string& configuredPath, const StateSnapshot& state) {
    std::vector<uint8_t> encoded = encodeStateSnapshot(state);
    if (encoded.empty() && !state.metadata.empty()) {
        return false;
    }

    const std::filesystem::path dbPath = resolveStateDbPath(configuredPath);
    if (!ensureStateDbParentDir(dbPath)) {
        return false;
    }

    database::Database db;
    if (!db.open(dbPath.string())) {
        return false;
    }

    const bool ok = db.put(kStateSnapshotKey, encoded);
    db.close();
    return ok;
}

bool loadSnapshotFromDb(const std::string& configuredPath, StateSnapshot& state) {
    const std::filesystem::path dbPath = resolveStateDbPath(configuredPath);
    database::Database db;
    if (!db.open(dbPath.string())) {
        return false;
    }

    const std::vector<uint8_t> encoded = db.get(kStateSnapshotKey);
    db.close();
    if (encoded.empty()) {
        return false;
    }

    return decodeStateSnapshot(encoded, state);
}

bool snapshotExistsInDb(const std::string& configuredPath) {
    const std::filesystem::path dbPath = resolveStateDbPath(configuredPath);
    database::Database db;
    if (!db.open(dbPath.string())) {
        return false;
    }

    const bool exists = db.exists(kStateSnapshotKey);
    db.close();
    return exists;
}

void removeSnapshotFromDb(const std::string& configuredPath) {
    const std::filesystem::path dbPath = resolveStateDbPath(configuredPath);
    database::Database db;
    if (!db.open(dbPath.string())) {
        return;
    }

    db.del(kStateSnapshotKey);
    db.close();
}

struct StatePersistenceData {
    explicit StatePersistenceData(std::string path)
        : configuredPath(std::move(path)) {}

    std::string configuredPath;
    mutable std::mutex mutex;
};

std::mutex gStatePersistenceDataMutex;
std::unordered_map<const StatePersistence*, std::shared_ptr<StatePersistenceData>> gStatePersistenceData;

std::shared_ptr<StatePersistenceData> getStatePersistenceData(const StatePersistence* self) {
    std::lock_guard<std::mutex> lock(gStatePersistenceDataMutex);
    auto it = gStatePersistenceData.find(self);
    if (it != gStatePersistenceData.end()) {
        return it->second;
    }

    auto data = std::make_shared<StatePersistenceData>(kDefaultStateDbName);
    gStatePersistenceData[self] = data;
    return data;
}

}  // namespace

class StateObserver {
public:
    virtual ~StateObserver() = default;
    virtual void onStateChanged(const StateSnapshot& oldState, const StateSnapshot& newState) = 0;
    virtual void onNodeStateChanged(NodeState oldState, NodeState newState) = 0;
    virtual void onSyncStateChanged(SyncState oldState, SyncState newState) = 0;
    virtual void onBlockHeightChanged(uint64_t oldHeight, uint64_t newHeight) = 0;
    virtual void onPeerCountChanged(int oldCount, int newCount) = 0;
    virtual void onBalanceChanged(uint64_t oldBalance, uint64_t newBalance) = 0;
};

class StateManager {
private:
    StateSnapshot currentState;
    std::vector<StateSnapshot> stateHistory;
    std::vector<StateObserver*> observers;
    mutable std::mutex mutex;
    size_t maxHistorySize;
    std::atomic<bool> initialized;
    std::string persistencePath;

public:
    explicit StateManager(const std::string& path = kDefaultStateDbName)
        : maxHistorySize(1000), initialized(false), persistencePath(path) {
        initializeState();
        if (!load()) {
            save();
        }
    }

    void initializeState() {
        std::lock_guard<std::mutex> lock(mutex);

        currentState.nodeState = NodeState::Initializing;
        currentState.syncState = SyncState::NotStarted;
        currentState.blockHeight = 0;
        currentState.headerHeight = 0;
        currentState.peerCount = 0;
        currentState.memPoolSize = 0;
        currentState.balance = 0;
        currentState.pendingBalance = 0;
        currentState.walletLocked = true;
        currentState.miningActive = false;
        currentState.modelLoaded = false;
        currentState.timestamp = getCurrentTimestamp();

        initialized = true;
    }

    void addObserver(StateObserver* observer) {
        std::lock_guard<std::mutex> lock(mutex);
        observers.push_back(observer);
    }

    void removeObserver(StateObserver* observer) {
        std::lock_guard<std::mutex> lock(mutex);
        observers.erase(
            std::remove(observers.begin(), observers.end(), observer),
            observers.end());
    }

    void setNodeState(NodeState state) {
        std::lock_guard<std::mutex> lock(mutex);

        if (currentState.nodeState == state) return;

        StateSnapshot oldState = currentState;
        currentState.nodeState = state;
        currentState.timestamp = getCurrentTimestamp();

        recordHistory(oldState);
        notifyNodeStateChanged(oldState.nodeState, state);
        notifyStateChanged(oldState, currentState);
        saveLocked();
    }

    void setSyncState(SyncState state) {
        std::lock_guard<std::mutex> lock(mutex);

        if (currentState.syncState == state) return;

        StateSnapshot oldState = currentState;
        currentState.syncState = state;
        currentState.timestamp = getCurrentTimestamp();

        recordHistory(oldState);
        notifySyncStateChanged(oldState.syncState, state);
        notifyStateChanged(oldState, currentState);
        saveLocked();
    }

    void setBlockHeight(uint64_t height) {
        std::lock_guard<std::mutex> lock(mutex);

        if (currentState.blockHeight == height) return;

        StateSnapshot oldState = currentState;
        currentState.blockHeight = height;
        currentState.timestamp = getCurrentTimestamp();

        recordHistory(oldState);
        notifyBlockHeightChanged(oldState.blockHeight, height);
        notifyStateChanged(oldState, currentState);
        saveLocked();
    }

    void setHeaderHeight(uint64_t height) {
        std::lock_guard<std::mutex> lock(mutex);
        currentState.headerHeight = height;
        currentState.timestamp = getCurrentTimestamp();
        saveLocked();
    }

    void setPeerCount(int count) {
        std::lock_guard<std::mutex> lock(mutex);

        if (currentState.peerCount == count) return;

        StateSnapshot oldState = currentState;
        currentState.peerCount = count;
        currentState.timestamp = getCurrentTimestamp();

        recordHistory(oldState);
        notifyPeerCountChanged(oldState.peerCount, count);
        notifyStateChanged(oldState, currentState);
        saveLocked();
    }

    void setMemPoolSize(uint64_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        currentState.memPoolSize = size;
        currentState.timestamp = getCurrentTimestamp();
        saveLocked();
    }

    void setBalance(uint64_t balance) {
        std::lock_guard<std::mutex> lock(mutex);

        if (currentState.balance == balance) return;

        StateSnapshot oldState = currentState;
        currentState.balance = balance;
        currentState.timestamp = getCurrentTimestamp();

        recordHistory(oldState);
        notifyBalanceChanged(oldState.balance, balance);
        notifyStateChanged(oldState, currentState);
        saveLocked();
    }

    void setPendingBalance(uint64_t balance) {
        std::lock_guard<std::mutex> lock(mutex);
        currentState.pendingBalance = balance;
        currentState.timestamp = getCurrentTimestamp();
        saveLocked();
    }

    void setWalletLocked(bool locked) {
        std::lock_guard<std::mutex> lock(mutex);
        currentState.walletLocked = locked;
        currentState.timestamp = getCurrentTimestamp();
        saveLocked();
    }

    void setMiningActive(bool active) {
        std::lock_guard<std::mutex> lock(mutex);
        currentState.miningActive = active;
        currentState.timestamp = getCurrentTimestamp();
        saveLocked();
    }

    void setModelLoaded(bool loaded) {
        std::lock_guard<std::mutex> lock(mutex);
        currentState.modelLoaded = loaded;
        currentState.timestamp = getCurrentTimestamp();
        saveLocked();
    }

    void setMetadata(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex);
        currentState.metadata[key] = value;
        currentState.timestamp = getCurrentTimestamp();
        saveLocked();
    }

    StateSnapshot getState() const {
        std::lock_guard<std::mutex> lock(mutex);
        return currentState;
    }

    NodeState getNodeState() const {
        std::lock_guard<std::mutex> lock(mutex);
        return currentState.nodeState;
    }

    SyncState getSyncState() const {
        std::lock_guard<std::mutex> lock(mutex);
        return currentState.syncState;
    }

    uint64_t getBlockHeight() const {
        std::lock_guard<std::mutex> lock(mutex);
        return currentState.blockHeight;
    }

    int getPeerCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        return currentState.peerCount;
    }

    uint64_t getBalance() const {
        std::lock_guard<std::mutex> lock(mutex);
        return currentState.balance;
    }

    bool isWalletLocked() const {
        std::lock_guard<std::mutex> lock(mutex);
        return currentState.walletLocked;
    }

    bool isMiningActive() const {
        std::lock_guard<std::mutex> lock(mutex);
        return currentState.miningActive;
    }

    bool isModelLoaded() const {
        std::lock_guard<std::mutex> lock(mutex);
        return currentState.modelLoaded;
    }

    std::vector<StateSnapshot> getHistory(size_t count = 100) const {
        std::lock_guard<std::mutex> lock(mutex);

        size_t start = stateHistory.size() > count ? stateHistory.size() - count : 0;
        return std::vector<StateSnapshot>(stateHistory.begin() + start, stateHistory.end());
    }

    void clearHistory() {
        std::lock_guard<std::mutex> lock(mutex);
        stateHistory.clear();
    }

    bool save() {
        std::lock_guard<std::mutex> lock(mutex);
        return saveLocked();
    }

    bool load() {
        std::lock_guard<std::mutex> lock(mutex);
        return loadLocked();
    }

private:
    uint64_t getCurrentTimestamp() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void recordHistory(const StateSnapshot& state) {
        stateHistory.push_back(state);
        if (stateHistory.size() > maxHistorySize) {
            stateHistory.erase(stateHistory.begin());
        }
    }

    bool saveLocked() {
        return saveSnapshotToDb(persistencePath, currentState);
    }

    bool loadLocked() {
        StateSnapshot loaded;
        if (!loadSnapshotFromDb(persistencePath, loaded)) {
            return false;
        }
        currentState = std::move(loaded);
        initialized = true;
        return true;
    }

    void notifyStateChanged(const StateSnapshot& oldState, const StateSnapshot& newState) {
        for (auto* observer : observers) {
            observer->onStateChanged(oldState, newState);
        }
    }

    void notifyNodeStateChanged(NodeState oldState, NodeState newState) {
        for (auto* observer : observers) {
            observer->onNodeStateChanged(oldState, newState);
        }
    }

    void notifySyncStateChanged(SyncState oldState, SyncState newState) {
        for (auto* observer : observers) {
            observer->onSyncStateChanged(oldState, newState);
        }
    }

    void notifyBlockHeightChanged(uint64_t oldHeight, uint64_t newHeight) {
        for (auto* observer : observers) {
            observer->onBlockHeightChanged(oldHeight, newHeight);
        }
    }

    void notifyPeerCountChanged(int oldCount, int newCount) {
        for (auto* observer : observers) {
            observer->onPeerCountChanged(oldCount, newCount);
        }
    }

    void notifyBalanceChanged(uint64_t oldBalance, uint64_t newBalance) {
        for (auto* observer : observers) {
            observer->onBalanceChanged(oldBalance, newBalance);
        }
    }
};

class SyncProgress {
private:
    uint64_t startHeight;
    uint64_t targetHeight;
    uint64_t currentHeight;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastUpdate;
    double blocksPerSecond;
    std::vector<double> speedHistory;
    size_t maxSpeedHistory;

public:
    SyncProgress() : startHeight(0), targetHeight(0), currentHeight(0),
                     blocksPerSecond(0.0), maxSpeedHistory(100) {}

    void start(uint64_t start, uint64_t target) {
        startHeight = start;
        targetHeight = target;
        currentHeight = start;
        startTime = std::chrono::steady_clock::now();
        lastUpdate = startTime;
        blocksPerSecond = 0.0;
        speedHistory.clear();
    }

    void update(uint64_t height) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastUpdate).count();

        if (elapsed > 0) {
            double speed = (height - currentHeight) * 1000.0 / elapsed;
            speedHistory.push_back(speed);

            if (speedHistory.size() > maxSpeedHistory) {
                speedHistory.erase(speedHistory.begin());
            }

            blocksPerSecond = 0.0;
            for (double s : speedHistory) {
                blocksPerSecond += s;
            }
            blocksPerSecond /= speedHistory.size();
        }

        currentHeight = height;
        lastUpdate = now;
    }

    double getProgress() const {
        if (targetHeight <= startHeight) return 0.0;
        return static_cast<double>(currentHeight - startHeight) / 
               (targetHeight - startHeight) * 100.0;
    }

    uint64_t getRemainingBlocks() const {
        return targetHeight > currentHeight ? targetHeight - currentHeight : 0;
    }

    uint64_t getEstimatedTimeRemaining() const {
        if (blocksPerSecond <= 0) return 0;
        return static_cast<uint64_t>(getRemainingBlocks() / blocksPerSecond);
    }

    double getBlocksPerSecond() const {
        return blocksPerSecond;
    }

    uint64_t getElapsedTime() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count();
    }

    bool isComplete() const {
        return currentHeight >= targetHeight;
    }

    uint64_t getCurrentHeight() const { return currentHeight; }
    uint64_t getTargetHeight() const { return targetHeight; }
    uint64_t getStartHeight() const { return startHeight; }
};

class HealthMonitor {
private:
    struct HealthMetric {
        std::string name;
        double value;
        double threshold;
        bool healthy;
        std::chrono::steady_clock::time_point lastUpdate;
    };

    std::map<std::string, HealthMetric> metrics;
    mutable std::mutex mutex;
    bool overallHealthy;

public:
    HealthMonitor() : overallHealthy(true) {
        initializeMetrics();
    }

    void initializeMetrics() {
        addMetric("peer_count", 0, 1);
        addMetric("sync_progress", 0, 0);
        addMetric("memory_usage", 0, 90);
        addMetric("cpu_usage", 0, 90);
        addMetric("disk_usage", 0, 90);
        addMetric("network_latency", 0, 5000);
        addMetric("block_time", 0, 120);
    }

    void addMetric(const std::string& name, double initialValue, double threshold) {
        std::lock_guard<std::mutex> lock(mutex);

        HealthMetric metric;
        metric.name = name;
        metric.value = initialValue;
        metric.threshold = threshold;
        metric.healthy = true;
        metric.lastUpdate = std::chrono::steady_clock::now();

        metrics[name] = metric;
    }

    void updateMetric(const std::string& name, double value) {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = metrics.find(name);
        if (it == metrics.end()) return;

        it->second.value = value;
        it->second.lastUpdate = std::chrono::steady_clock::now();

        if (name == "peer_count") {
            it->second.healthy = value >= it->second.threshold;
        } else {
            it->second.healthy = value <= it->second.threshold;
        }

        updateOverallHealth();
    }

    void updateOverallHealth() {
        overallHealthy = true;
        for (const auto& pair : metrics) {
            if (!pair.second.healthy) {
                overallHealthy = false;
                break;
            }
        }
    }

    bool isHealthy() const {
        return overallHealthy;
    }

    bool isMetricHealthy(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = metrics.find(name);
        return (it != metrics.end()) ? it->second.healthy : true;
    }

    double getMetricValue(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = metrics.find(name);
        return (it != metrics.end()) ? it->second.value : 0.0;
    }

    std::vector<std::string> getUnhealthyMetrics() const {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string> unhealthy;

        for (const auto& pair : metrics) {
            if (!pair.second.healthy) {
                unhealthy.push_back(pair.first);
            }
        }

        return unhealthy;
    }

    std::map<std::string, double> getAllMetrics() const {
        std::lock_guard<std::mutex> lock(mutex);
        std::map<std::string, double> result;

        for (const auto& pair : metrics) {
            result[pair.first] = pair.second.value;
        }

        return result;
    }
};

StatePersistence::StatePersistence(const std::string& path) {
    auto data = std::make_shared<StatePersistenceData>(path);
    std::lock_guard<std::mutex> lock(gStatePersistenceDataMutex);
    gStatePersistenceData[this] = std::move(data);
}

StatePersistence::~StatePersistence() {
    std::lock_guard<std::mutex> lock(gStatePersistenceDataMutex);
    auto it = gStatePersistenceData.find(this);
    if (it != gStatePersistenceData.end()) {
        gStatePersistenceData.erase(it);
    }
}

bool StatePersistence::save(const StateSnapshot& state) {
    auto data = getStatePersistenceData(this);
    std::lock_guard<std::mutex> lock(data->mutex);
    return saveSnapshotToDb(data->configuredPath, state);
}

bool StatePersistence::load(StateSnapshot& state) {
    auto data = getStatePersistenceData(this);
    std::lock_guard<std::mutex> lock(data->mutex);
    return loadSnapshotFromDb(data->configuredPath, state);
}

bool StatePersistence::exists() const {
    auto data = getStatePersistenceData(this);
    std::lock_guard<std::mutex> lock(data->mutex);
    return snapshotExistsInDb(data->configuredPath);
}

void StatePersistence::remove() {
    auto data = getStatePersistenceData(this);
    std::lock_guard<std::mutex> lock(data->mutex);
    removeSnapshotFromDb(data->configuredPath);
}

class StateTransitionValidator {
private:
    std::map<NodeState, std::set<NodeState>> validTransitions;

public:
    StateTransitionValidator() {
        initializeTransitions();
    }

    void initializeTransitions() {
        validTransitions[NodeState::Initializing] = {
            NodeState::Connecting, NodeState::Error
        };

        validTransitions[NodeState::Connecting] = {
            NodeState::Syncing, NodeState::Running, NodeState::Error
        };

        validTransitions[NodeState::Syncing] = {
            NodeState::Running, NodeState::Error, NodeState::Paused
        };

        validTransitions[NodeState::Running] = {
            NodeState::Paused, NodeState::ShuttingDown, NodeState::Error, NodeState::Syncing
        };

        validTransitions[NodeState::Paused] = {
            NodeState::Running, NodeState::ShuttingDown
        };

        validTransitions[NodeState::Error] = {
            NodeState::Initializing, NodeState::ShuttingDown
        };

        validTransitions[NodeState::ShuttingDown] = {};
    }

    bool isValidTransition(NodeState from, NodeState to) const {
        auto it = validTransitions.find(from);
        if (it == validTransitions.end()) return false;
        return it->second.find(to) != it->second.end();
    }

    std::set<NodeState> getValidNextStates(NodeState current) const {
        auto it = validTransitions.find(current);
        return (it != validTransitions.end()) ? it->second : std::set<NodeState>();
    }
};

class StateLogger {
private:
    std::vector<std::pair<uint64_t, std::string>> logs;
    size_t maxLogs;
    mutable std::mutex mutex;

public:
    StateLogger(size_t max = 10000) : maxLogs(max) {}

    void log(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex);

        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        logs.push_back({timestamp, message});

        if (logs.size() > maxLogs) {
            logs.erase(logs.begin());
        }
    }

    void logStateChange(NodeState from, NodeState to) {
        std::stringstream ss;
        ss << "Node state: " << static_cast<int>(from) << " -> " << static_cast<int>(to);
        log(ss.str());
    }

    void logSyncProgress(uint64_t current, uint64_t target) {
        std::stringstream ss;
        ss << "Sync progress: " << current << "/" << target;
        log(ss.str());
    }

    void logPeerChange(int oldCount, int newCount) {
        std::stringstream ss;
        ss << "Peer count: " << oldCount << " -> " << newCount;
        log(ss.str());
    }

    std::vector<std::pair<uint64_t, std::string>> getLogs(size_t count = 100) const {
        std::lock_guard<std::mutex> lock(mutex);

        size_t start = logs.size() > count ? logs.size() - count : 0;
        return std::vector<std::pair<uint64_t, std::string>>(
            logs.begin() + start, logs.end());
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        logs.clear();
    }
};

std::string nodeStateToString(NodeState state) {
    switch (state) {
        case NodeState::Initializing: return "Initializing";
        case NodeState::Connecting: return "Connecting";
        case NodeState::Syncing: return "Syncing";
        case NodeState::Running: return "Running";
        case NodeState::Paused: return "Paused";
        case NodeState::ShuttingDown: return "Shutting Down";
        case NodeState::Error: return "Error";
        default: return "Unknown";
    }
}

std::string syncStateToString(SyncState state) {
    switch (state) {
        case SyncState::NotStarted: return "Not Started";
        case SyncState::DownloadingHeaders: return "Downloading Headers";
        case SyncState::DownloadingBlocks: return "Downloading Blocks";
        case SyncState::VerifyingBlocks: return "Verifying Blocks";
        case SyncState::Indexing: return "Indexing";
        case SyncState::Complete: return "Complete";
        case SyncState::Failed: return "Failed";
        default: return "Unknown";
    }
}

}
}
