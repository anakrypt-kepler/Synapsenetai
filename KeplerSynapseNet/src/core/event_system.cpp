#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <mutex>
#include <functional>
#include <memory>
#include <chrono>
#include <atomic>
#include <thread>
#include <algorithm>
#include <condition_variable>

namespace synapse {
namespace core {

enum class SystemEventType {
    BlockReceived,
    BlockValidated,
    BlockRejected,
    TransactionReceived,
    TransactionValidated,
    TransactionRejected,
    TransactionConfirmed,
    PeerConnected,
    PeerDisconnected,
    PeerBanned,
    SyncStarted,
    SyncProgress,
    SyncCompleted,
    SyncFailed,
    KnowledgeSubmitted,
    KnowledgeValidated,
    KnowledgeRejected,
    ModelLoaded,
    ModelUnloaded,
    InferenceStarted,
    InferenceCompleted,
    WalletUnlocked,
    WalletLocked,
    BalanceChanged,
    NetworkAlert,
    ConsensusReached,
    ForkDetected,
    ReorgStarted,
    ReorgCompleted,
    MiningStarted,
    MiningStopped,
    BlockMined,
    Custom
};

struct SystemEvent {
    SystemEventType type;
    std::string id;
    std::string source;
    std::map<std::string, std::string> data;
    uint64_t timestamp;
    int priority;
    bool processed;
};

using EventHandler = std::function<void(const SystemEvent&)>;
using EventFilter = std::function<bool(const SystemEvent&)>;

class EventBus {
private:
    struct Subscription {
        std::string id;
        SystemEventType type;
        EventHandler handler;
        EventFilter filter;
        int priority;
        bool active;
    };

    std::vector<Subscription> subscriptions;
    std::queue<SystemEvent> eventQueue;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> running;
    std::thread processingThread;
    std::atomic<uint64_t> eventCounter;
    std::atomic<uint64_t> processedCounter;

public:
    EventBus() : running(false), eventCounter(0), processedCounter(0) {}

    ~EventBus() {
        stop();
    }

    void start() {
        running = true;
        processingThread = std::thread(&EventBus::processLoop, this);
    }

    void stop() {
        running = false;
        cv.notify_all();
        if (processingThread.joinable()) {
            processingThread.join();
        }
    }

    std::string subscribe(SystemEventType type, EventHandler handler, int priority = 0) {
        std::lock_guard<std::mutex> lock(mutex);

        Subscription sub;
        sub.id = generateId();
        sub.type = type;
        sub.handler = handler;
        sub.filter = nullptr;
        sub.priority = priority;
        sub.active = true;

        subscriptions.push_back(sub);
        sortSubscriptions();

        return sub.id;
    }

    std::string subscribeWithFilter(SystemEventType type, EventHandler handler, 
                                    EventFilter filter, int priority = 0) {
        std::lock_guard<std::mutex> lock(mutex);

        Subscription sub;
        sub.id = generateId();
        sub.type = type;
        sub.handler = handler;
        sub.filter = filter;
        sub.priority = priority;
        sub.active = true;

        subscriptions.push_back(sub);
        sortSubscriptions();

        return sub.id;
    }

    void unsubscribe(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex);

        subscriptions.erase(
            std::remove_if(subscriptions.begin(), subscriptions.end(),
                [&id](const Subscription& s) { return s.id == id; }),
            subscriptions.end());
    }

    void publish(const SystemEvent& event) {
        std::lock_guard<std::mutex> lock(mutex);
        eventQueue.push(event);
        eventCounter++;
        cv.notify_one();
    }

    void publishSync(const SystemEvent& event) {
        std::lock_guard<std::mutex> lock(mutex);
        dispatchEvent(event);
    }

    SystemEvent createEvent(SystemEventType type, const std::string& source = "") {
        SystemEvent event;
        event.type = type;
        event.id = generateEventId();
        event.source = source;
        event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        event.priority = 0;
        event.processed = false;
        return event;
    }

    uint64_t getEventCount() const { return eventCounter; }
    uint64_t getProcessedCount() const { return processedCounter; }
    size_t getQueueSize() const { return eventQueue.size(); }
    size_t getSubscriptionCount() const { return subscriptions.size(); }

private:
    void processLoop() {
        while (running) {
            SystemEvent event;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this] { return !eventQueue.empty() || !running; });

                if (!running && eventQueue.empty()) break;

                if (!eventQueue.empty()) {
                    event = eventQueue.front();
                    eventQueue.pop();
                } else {
                    continue;
                }
            }

            dispatchEvent(event);
            processedCounter++;
        }
    }

    void dispatchEvent(const SystemEvent& event) {
        for (const auto& sub : subscriptions) {
            if (!sub.active) continue;
            if (sub.type != event.type && sub.type != SystemEventType::Custom) continue;

            if (sub.filter && !sub.filter(event)) continue;

            try {
                sub.handler(event);
            } catch (...) {
            }
        }
    }

    void sortSubscriptions() {
        std::sort(subscriptions.begin(), subscriptions.end(),
            [](const Subscription& a, const Subscription& b) {
                return a.priority > b.priority;
            });
    }

    std::string generateId() {
        static std::atomic<uint64_t> counter(0);
        return "sub_" + std::to_string(counter++);
    }

    std::string generateEventId() {
        static std::atomic<uint64_t> counter(0);
        return "evt_" + std::to_string(counter++);
    }
};

class EventAggregator {
private:
    struct AggregatedEvent {
        SystemEventType type;
        std::vector<SystemEvent> events;
        std::chrono::steady_clock::time_point firstEvent;
        std::chrono::milliseconds window;
    };

    std::map<SystemEventType, AggregatedEvent> aggregations;
    std::mutex mutex;
    EventBus* eventBus;
    std::function<void(SystemEventType, const std::vector<SystemEvent>&)> aggregateHandler;

public:
    EventAggregator(EventBus* bus) : eventBus(bus) {}

    void setAggregateHandler(std::function<void(SystemEventType, const std::vector<SystemEvent>&)> handler) {
        aggregateHandler = handler;
    }

    void startAggregation(SystemEventType type, std::chrono::milliseconds window) {
        std::lock_guard<std::mutex> lock(mutex);

        AggregatedEvent agg;
        agg.type = type;
        agg.window = window;
        agg.firstEvent = std::chrono::steady_clock::now();

        aggregations[type] = agg;

        eventBus->subscribe(type, [this, type](const SystemEvent& e) {
            addToAggregation(type, e);
        });
    }

    void stopAggregation(SystemEventType type) {
        std::lock_guard<std::mutex> lock(mutex);
        flushAggregation(type);
        aggregations.erase(type);
    }

    void addToAggregation(SystemEventType type, const SystemEvent& event) {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = aggregations.find(type);
        if (it == aggregations.end()) return;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.firstEvent);

        if (elapsed >= it->second.window) {
            flushAggregation(type);
            it->second.firstEvent = now;
            it->second.events.clear();
        }

        it->second.events.push_back(event);
    }

    void flushAggregation(SystemEventType type) {
        auto it = aggregations.find(type);
        if (it == aggregations.end() || it->second.events.empty()) return;

        if (aggregateHandler) {
            aggregateHandler(type, it->second.events);
        }
    }

    void flushAll() {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& pair : aggregations) {
            flushAggregation(pair.first);
            pair.second.events.clear();
            pair.second.firstEvent = std::chrono::steady_clock::now();
        }
    }
};

class EventLogger {
private:
    std::vector<SystemEvent> eventLog;
    size_t maxLogSize;
    mutable std::mutex mutex;
    bool enabled;
    std::set<SystemEventType> filteredTypes;

public:
    EventLogger(size_t maxSize = 10000) : maxLogSize(maxSize), enabled(true) {}

    void enable() { enabled = true; }
    void disable() { enabled = false; }

    void addFilter(SystemEventType type) {
        filteredTypes.insert(type);
    }

    void removeFilter(SystemEventType type) {
        filteredTypes.erase(type);
    }

    void clearFilters() {
        filteredTypes.clear();
    }

    void log(const SystemEvent& event) {
        if (!enabled) return;
        if (!filteredTypes.empty() && filteredTypes.find(event.type) == filteredTypes.end()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);

        eventLog.push_back(event);

        if (eventLog.size() > maxLogSize) {
            eventLog.erase(eventLog.begin());
        }
    }

    std::vector<SystemEvent> getEvents(size_t count = 100) const {
        std::lock_guard<std::mutex> lock(mutex);

        size_t start = eventLog.size() > count ? eventLog.size() - count : 0;
        return std::vector<SystemEvent>(eventLog.begin() + start, eventLog.end());
    }

    std::vector<SystemEvent> getEventsByType(SystemEventType type, size_t count = 100) const {
        std::lock_guard<std::mutex> lock(mutex);

        std::vector<SystemEvent> result;
        for (auto it = eventLog.rbegin(); it != eventLog.rend() && result.size() < count; ++it) {
            if (it->type == type) {
                result.push_back(*it);
            }
        }

        std::reverse(result.begin(), result.end());
        return result;
    }

    std::vector<SystemEvent> getEventsBySource(const std::string& source, size_t count = 100) const {
        std::lock_guard<std::mutex> lock(mutex);

        std::vector<SystemEvent> result;
        for (auto it = eventLog.rbegin(); it != eventLog.rend() && result.size() < count; ++it) {
            if (it->source == source) {
                result.push_back(*it);
            }
        }

        std::reverse(result.begin(), result.end());
        return result;
    }

    std::vector<SystemEvent> getEventsInRange(uint64_t startTime, uint64_t endTime) const {
        std::lock_guard<std::mutex> lock(mutex);

        std::vector<SystemEvent> result;
        for (const auto& event : eventLog) {
            if (event.timestamp >= startTime && event.timestamp <= endTime) {
                result.push_back(event);
            }
        }

        return result;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        eventLog.clear();
    }

    size_t size() const { return eventLog.size(); }
};

class EventMetrics {
private:
    struct TypeMetrics {
        uint64_t count;
        uint64_t totalProcessingTime;
        uint64_t minProcessingTime;
        uint64_t maxProcessingTime;
        std::chrono::steady_clock::time_point lastEvent;
    };

    std::map<SystemEventType, TypeMetrics> metrics;
    mutable std::mutex mutex;
    std::chrono::steady_clock::time_point startTime;

public:
    EventMetrics() : startTime(std::chrono::steady_clock::now()) {}

    void recordEvent(SystemEventType type, uint64_t processingTimeUs) {
        std::lock_guard<std::mutex> lock(mutex);

        auto& m = metrics[type];
        m.count++;
        m.totalProcessingTime += processingTimeUs;
        m.lastEvent = std::chrono::steady_clock::now();

        if (m.count == 1) {
            m.minProcessingTime = processingTimeUs;
            m.maxProcessingTime = processingTimeUs;
        } else {
            m.minProcessingTime = std::min(m.minProcessingTime, processingTimeUs);
            m.maxProcessingTime = std::max(m.maxProcessingTime, processingTimeUs);
        }
    }

    uint64_t getEventCount(SystemEventType type) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = metrics.find(type);
        return (it != metrics.end()) ? it->second.count : 0;
    }

    double getAverageProcessingTime(SystemEventType type) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = metrics.find(type);
        if (it == metrics.end() || it->second.count == 0) return 0.0;
        return static_cast<double>(it->second.totalProcessingTime) / it->second.count;
    }

    uint64_t getTotalEventCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        uint64_t total = 0;
        for (const auto& pair : metrics) {
            total += pair.second.count;
        }
        return total;
    }

    double getEventsPerSecond() const {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsed == 0) return 0.0;
        return static_cast<double>(getTotalEventCount()) / elapsed;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex);
        metrics.clear();
        startTime = std::chrono::steady_clock::now();
    }
};

class EventReplay {
private:
    std::vector<SystemEvent> recordedEvents;
    size_t currentIndex;
    bool recording;
    bool replaying;
    EventBus* eventBus;
    double replaySpeed;

public:
    EventReplay(EventBus* bus) : currentIndex(0), recording(false), 
                                  replaying(false), eventBus(bus), replaySpeed(1.0) {}

    void startRecording() {
        recordedEvents.clear();
        currentIndex = 0;
        recording = true;
    }

    void stopRecording() {
        recording = false;
    }

    void record(const SystemEvent& event) {
        if (recording) {
            recordedEvents.push_back(event);
        }
    }

    void startReplay() {
        if (recordedEvents.empty()) return;
        currentIndex = 0;
        replaying = true;
    }

    void stopReplay() {
        replaying = false;
    }

    void setReplaySpeed(double speed) {
        replaySpeed = speed;
    }

    bool replayNext() {
        if (!replaying || currentIndex >= recordedEvents.size()) {
            replaying = false;
            return false;
        }

        eventBus->publish(recordedEvents[currentIndex]);
        currentIndex++;

        return true;
    }

    void replayAll() {
        startReplay();
        while (replayNext()) {
        }
    }

    size_t getRecordedCount() const { return recordedEvents.size(); }
    size_t getCurrentIndex() const { return currentIndex; }
    bool isRecording() const { return recording; }
    bool isReplaying() const { return replaying; }

    void clear() {
        recordedEvents.clear();
        currentIndex = 0;
        recording = false;
        replaying = false;
    }
};

class EventDispatcher {
private:
    EventBus eventBus;
    EventLogger logger;
    EventMetrics metrics;
    std::unique_ptr<EventAggregator> aggregator;
    std::unique_ptr<EventReplay> replay;

public:
    EventDispatcher() {
        aggregator = std::make_unique<EventAggregator>(&eventBus);
        replay = std::make_unique<EventReplay>(&eventBus);
    }

    void start() {
        eventBus.start();
    }

    void stop() {
        eventBus.stop();
    }

    void emit(SystemEventType type, const std::string& source = "",
              const std::map<std::string, std::string>& data = {}) {
        SystemEvent event = eventBus.createEvent(type, source);
        event.data = data;

        logger.log(event);
        replay->record(event);

        auto startTime = std::chrono::steady_clock::now();
        eventBus.publish(event);
        auto endTime = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count();
        metrics.recordEvent(type, duration);
    }

    std::string on(SystemEventType type, EventHandler handler, int priority = 0) {
        return eventBus.subscribe(type, handler, priority);
    }

    void off(const std::string& subscriptionId) {
        eventBus.unsubscribe(subscriptionId);
    }

    EventBus& getBus() { return eventBus; }
    EventLogger& getLogger() { return logger; }
    EventMetrics& getMetrics() { return metrics; }
    EventAggregator& getAggregator() { return *aggregator; }
    EventReplay& getReplay() { return *replay; }
};

}
}
