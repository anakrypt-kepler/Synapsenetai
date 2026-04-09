#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <functional>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <atomic>
#include <queue>

namespace synapse {
namespace python {

enum class PythonValueType {
    NONE,
    BOOL,
    INT,
    FLOAT,
    STRING,
    LIST,
    DICT,
    BYTES,
    CALLABLE
};

struct PythonValue {
    PythonValueType type = PythonValueType::NONE;
    bool boolVal = false;
    int64_t intVal = 0;
    double floatVal = 0.0;
    std::string stringVal;
    std::vector<uint8_t> bytesVal;
    std::vector<PythonValue> listVal;
    std::map<std::string, PythonValue> dictVal;
    
    static PythonValue none() { return PythonValue{}; }
    static PythonValue fromBool(bool v) { PythonValue p; p.type = PythonValueType::BOOL; p.boolVal = v; return p; }
    static PythonValue fromInt(int64_t v) { PythonValue p; p.type = PythonValueType::INT; p.intVal = v; return p; }
    static PythonValue fromFloat(double v) { PythonValue p; p.type = PythonValueType::FLOAT; p.floatVal = v; return p; }
    static PythonValue fromString(const std::string& v) { PythonValue p; p.type = PythonValueType::STRING; p.stringVal = v; return p; }
    static PythonValue fromBytes(const std::vector<uint8_t>& v) { PythonValue p; p.type = PythonValueType::BYTES; p.bytesVal = v; return p; }
    static PythonValue fromList(const std::vector<PythonValue>& v) { PythonValue p; p.type = PythonValueType::LIST; p.listVal = v; return p; }
    static PythonValue fromDict(const std::map<std::string, PythonValue>& v) { PythonValue p; p.type = PythonValueType::DICT; p.dictVal = v; return p; }
    
    bool toBool() const { return type == PythonValueType::BOOL ? boolVal : (type == PythonValueType::INT ? intVal != 0 : false); }
    int64_t toInt() const { return type == PythonValueType::INT ? intVal : (type == PythonValueType::FLOAT ? static_cast<int64_t>(floatVal) : 0); }
    double toFloat() const { return type == PythonValueType::FLOAT ? floatVal : (type == PythonValueType::INT ? static_cast<double>(intVal) : 0.0); }
    std::string toString() const { return type == PythonValueType::STRING ? stringVal : ""; }
    std::vector<uint8_t> toBytes() const { return type == PythonValueType::BYTES ? bytesVal : std::vector<uint8_t>{}; }
};

struct PythonModule {
    std::string name;
    std::string path;
    std::string source;
    bool loaded = false;
    std::map<std::string, PythonValue> globals;
    std::time_t loadedAt = 0;
    std::time_t modifiedAt = 0;
};

struct PythonCallResult {
    bool success = false;
    PythonValue result;
    std::string error;
    double executionTime = 0.0;
};

struct PythonTask {
    std::string taskId;
    std::string moduleName;
    std::string functionName;
    std::vector<PythonValue> args;
    std::map<std::string, PythonValue> kwargs;
    std::function<void(const PythonCallResult&)> callback;
    std::time_t queuedAt;
    int priority;
};

class PythonBridge {
public:
    PythonBridge();
    ~PythonBridge();
    
    bool initialize(const std::string& pythonPath = "");
    void shutdown();
    bool isInitialized() const;
    
    bool loadModule(const std::string& name, const std::string& path);
    bool loadModuleFromSource(const std::string& name, const std::string& source);
    bool reloadModule(const std::string& name);
    bool unloadModule(const std::string& name);
    bool isModuleLoaded(const std::string& name) const;
    std::vector<std::string> getLoadedModules() const;
    
    PythonCallResult call(const std::string& moduleName, const std::string& functionName,
                          const std::vector<PythonValue>& args = {},
                          const std::map<std::string, PythonValue>& kwargs = {});
    
    std::string queueCall(const std::string& moduleName, const std::string& functionName,
                          const std::vector<PythonValue>& args,
                          const std::map<std::string, PythonValue>& kwargs,
                          std::function<void(const PythonCallResult&)> callback,
                          int priority = 0);
    
    bool cancelQueuedCall(const std::string& taskId);
    size_t getQueueSize() const;
    
    PythonValue getGlobal(const std::string& moduleName, const std::string& name);
    bool setGlobal(const std::string& moduleName, const std::string& name, const PythonValue& value);
    
    void registerCallback(const std::string& name, std::function<PythonValue(const std::vector<PythonValue>&)> callback);
    void unregisterCallback(const std::string& name);
    
    void setMaxExecutionTime(double seconds);
    void setMaxMemory(size_t bytes);
    void setMaxOutputSize(size_t bytes);
    
    std::string getLastError() const;
    void clearError();
    
    struct Stats {
        uint64_t totalCalls = 0;
        uint64_t successfulCalls = 0;
        uint64_t failedCalls = 0;
        double totalExecutionTime = 0.0;
        double avgExecutionTime = 0.0;
        size_t modulesLoaded = 0;
        size_t callbacksRegistered = 0;
    };
    
    Stats getStats() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct PythonBridge::Impl {
    std::map<std::string, PythonModule> modules;
    std::map<std::string, std::function<PythonValue(const std::vector<PythonValue>&)>> callbacks;
    std::queue<PythonTask> taskQueue;
    mutable std::mutex mtx;
    std::atomic<bool> initialized{false};
    std::atomic<bool> running{false};
    std::thread workerThread;
    std::string lastError;
    std::string pythonPath;
    
    double maxExecutionTime = 30.0;
    size_t maxMemory = 256 * 1024 * 1024;
    size_t maxOutputSize = 10 * 1024 * 1024;
    
    Stats stats;
    
    void workerLoop();
    PythonCallResult executeCall(const std::string& moduleName, const std::string& functionName,
                                  const std::vector<PythonValue>& args,
                                  const std::map<std::string, PythonValue>& kwargs);
    std::string generateTaskId();
    bool validateModule(const PythonModule& module);
    PythonValue parseResult(const std::string& output);
    std::string serializeArgs(const std::vector<PythonValue>& args,
                              const std::map<std::string, PythonValue>& kwargs);
};

void PythonBridge::Impl::workerLoop() {
    while (running) {
        PythonTask task;
        bool hasTask = false;
        
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (!taskQueue.empty()) {
                task = taskQueue.front();
                taskQueue.pop();
                hasTask = true;
            }
        }
        
        if (hasTask) {
            PythonCallResult result = executeCall(task.moduleName, task.functionName,
                                                   task.args, task.kwargs);
            if (task.callback) {
                task.callback(result);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

PythonCallResult PythonBridge::Impl::executeCall(const std::string& moduleName,
                                                  const std::string& functionName,
                                                  const std::vector<PythonValue>& args,
                                                  const std::map<std::string, PythonValue>& kwargs) {
    PythonCallResult result;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    auto it = modules.find(moduleName);
    if (it == modules.end()) {
        result.error = "Module not found: " + moduleName;
        return result;
    }
    
    const PythonModule& module = it->second;
    if (!module.loaded) {
        result.error = "Module not loaded: " + moduleName;
        return result;
    }
    
    result.success = true;
    result.result = PythonValue::none();
    
    auto endTime = std::chrono::high_resolution_clock::now();
    result.executionTime = std::chrono::duration<double>(endTime - startTime).count();
    
    stats.totalCalls++;
    if (result.success) {
        stats.successfulCalls++;
    } else {
        stats.failedCalls++;
    }
    stats.totalExecutionTime += result.executionTime;
    stats.avgExecutionTime = stats.totalExecutionTime / stats.totalCalls;
    
    return result;
}

std::string PythonBridge::Impl::generateTaskId() {
    static std::atomic<uint64_t> counter{0};
    return "task_" + std::to_string(++counter);
}

bool PythonBridge::Impl::validateModule(const PythonModule& module) {
    if (module.name.empty()) return false;
    if (module.source.empty() && module.path.empty()) return false;
    return true;
}

PythonValue PythonBridge::Impl::parseResult(const std::string& output) {
    if (output.empty()) return PythonValue::none();
    
    if (output == "True") return PythonValue::fromBool(true);
    if (output == "False") return PythonValue::fromBool(false);
    if (output == "None") return PythonValue::none();
    
    try {
        size_t pos;
        int64_t intVal = std::stoll(output, &pos);
        if (pos == output.size()) {
            return PythonValue::fromInt(intVal);
        }
    } catch (...) {}
    
    try {
        size_t pos;
        double floatVal = std::stod(output, &pos);
        if (pos == output.size()) {
            return PythonValue::fromFloat(floatVal);
        }
    } catch (...) {}
    
    return PythonValue::fromString(output);
}

std::string PythonBridge::Impl::serializeArgs(const std::vector<PythonValue>& args,
                                               const std::map<std::string, PythonValue>& kwargs) {
    std::ostringstream oss;
    oss << "(";
    
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) oss << ", ";
        const auto& arg = args[i];
        switch (arg.type) {
            case PythonValueType::NONE: oss << "None"; break;
            case PythonValueType::BOOL: oss << (arg.boolVal ? "True" : "False"); break;
            case PythonValueType::INT: oss << arg.intVal; break;
            case PythonValueType::FLOAT: oss << arg.floatVal; break;
            case PythonValueType::STRING: oss << "\"" << arg.stringVal << "\""; break;
            default: oss << "None"; break;
        }
    }
    
    for (const auto& kv : kwargs) {
        if (!args.empty() || &kv != &*kwargs.begin()) oss << ", ";
        oss << kv.first << "=";
        const auto& arg = kv.second;
        switch (arg.type) {
            case PythonValueType::NONE: oss << "None"; break;
            case PythonValueType::BOOL: oss << (arg.boolVal ? "True" : "False"); break;
            case PythonValueType::INT: oss << arg.intVal; break;
            case PythonValueType::FLOAT: oss << arg.floatVal; break;
            case PythonValueType::STRING: oss << "\"" << arg.stringVal << "\""; break;
            default: oss << "None"; break;
        }
    }
    
    oss << ")";
    return oss.str();
}

PythonBridge::PythonBridge() : impl_(std::make_unique<Impl>()) {}

PythonBridge::~PythonBridge() {
    shutdown();
}

bool PythonBridge::initialize(const std::string& pythonPath) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->initialized) return true;
    
    impl_->pythonPath = pythonPath;
    impl_->initialized = true;
    impl_->running = true;
    
    impl_->workerThread = std::thread([this]() { impl_->workerLoop(); });
    
    return true;
}

void PythonBridge::shutdown() {
    impl_->running = false;
    
    if (impl_->workerThread.joinable()) {
        impl_->workerThread.join();
    }
    
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->modules.clear();
    impl_->callbacks.clear();
    impl_->initialized = false;
}

bool PythonBridge::isInitialized() const {
    return impl_->initialized;
}

bool PythonBridge::loadModule(const std::string& name, const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::ifstream file(path);
    if (!file.is_open()) {
        impl_->lastError = "Cannot open file: " + path;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    PythonModule module;
    module.name = name;
    module.path = path;
    module.source = buffer.str();
    module.loaded = true;
    module.loadedAt = std::time(nullptr);
    
    impl_->modules[name] = module;
    impl_->stats.modulesLoaded = impl_->modules.size();
    
    return true;
}

bool PythonBridge::loadModuleFromSource(const std::string& name, const std::string& source) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    PythonModule module;
    module.name = name;
    module.source = source;
    module.loaded = true;
    module.loadedAt = std::time(nullptr);
    
    impl_->modules[name] = module;
    impl_->stats.modulesLoaded = impl_->modules.size();
    
    return true;
}

bool PythonBridge::reloadModule(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->modules.find(name);
    if (it == impl_->modules.end()) {
        impl_->lastError = "Module not found: " + name;
        return false;
    }
    
    if (!it->second.path.empty()) {
        std::ifstream file(it->second.path);
        if (!file.is_open()) {
            impl_->lastError = "Cannot open file: " + it->second.path;
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        it->second.source = buffer.str();
    }
    
    it->second.loadedAt = std::time(nullptr);
    return true;
}

bool PythonBridge::unloadModule(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->modules.find(name);
    if (it == impl_->modules.end()) {
        return false;
    }
    
    impl_->modules.erase(it);
    impl_->stats.modulesLoaded = impl_->modules.size();
    return true;
}

bool PythonBridge::isModuleLoaded(const std::string& name) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->modules.find(name);
    return it != impl_->modules.end() && it->second.loaded;
}

std::vector<std::string> PythonBridge::getLoadedModules() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::string> result;
    for (const auto& pair : impl_->modules) {
        if (pair.second.loaded) {
            result.push_back(pair.first);
        }
    }
    return result;
}

PythonCallResult PythonBridge::call(const std::string& moduleName, const std::string& functionName,
                                     const std::vector<PythonValue>& args,
                                     const std::map<std::string, PythonValue>& kwargs) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->executeCall(moduleName, functionName, args, kwargs);
}

std::string PythonBridge::queueCall(const std::string& moduleName, const std::string& functionName,
                                     const std::vector<PythonValue>& args,
                                     const std::map<std::string, PythonValue>& kwargs,
                                     std::function<void(const PythonCallResult&)> callback,
                                     int priority) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    PythonTask task;
    task.taskId = impl_->generateTaskId();
    task.moduleName = moduleName;
    task.functionName = functionName;
    task.args = args;
    task.kwargs = kwargs;
    task.callback = callback;
    task.queuedAt = std::time(nullptr);
    task.priority = priority;
    
    impl_->taskQueue.push(task);
    
    return task.taskId;
}

bool PythonBridge::cancelQueuedCall(const std::string& taskId) {
    return false;
}

size_t PythonBridge::getQueueSize() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->taskQueue.size();
}

PythonValue PythonBridge::getGlobal(const std::string& moduleName, const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->modules.find(moduleName);
    if (it == impl_->modules.end()) {
        return PythonValue::none();
    }
    
    auto git = it->second.globals.find(name);
    if (git == it->second.globals.end()) {
        return PythonValue::none();
    }
    
    return git->second;
}

bool PythonBridge::setGlobal(const std::string& moduleName, const std::string& name,
                              const PythonValue& value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->modules.find(moduleName);
    if (it == impl_->modules.end()) {
        return false;
    }
    
    it->second.globals[name] = value;
    return true;
}

void PythonBridge::registerCallback(const std::string& name,
                                     std::function<PythonValue(const std::vector<PythonValue>&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->callbacks[name] = callback;
    impl_->stats.callbacksRegistered = impl_->callbacks.size();
}

void PythonBridge::unregisterCallback(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->callbacks.erase(name);
    impl_->stats.callbacksRegistered = impl_->callbacks.size();
}

void PythonBridge::setMaxExecutionTime(double seconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->maxExecutionTime = seconds;
}

void PythonBridge::setMaxMemory(size_t bytes) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->maxMemory = bytes;
}

void PythonBridge::setMaxOutputSize(size_t bytes) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->maxOutputSize = bytes;
}

std::string PythonBridge::getLastError() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->lastError;
}

void PythonBridge::clearError() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->lastError.clear();
}

PythonBridge::Stats PythonBridge::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->stats;
}

}
}
