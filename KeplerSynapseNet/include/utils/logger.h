#pragma once

#include <string>
#include <functional>
#include <cstdint>

namespace synapse {
namespace utils {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5,
    OFF = 6
};

struct LogEntry {
    LogLevel level;
    std::string message;
    std::string category;
    std::string file;
    int line;
    uint64_t timestamp;
    uint64_t threadId;
};

class Logger {
public:
    static void init(const std::string& path);
    static void shutdown();
    static void setLevel(LogLevel level);
    static LogLevel getLevel();
    static void setPattern(const std::string& pattern);
    static void enableConsole(bool enable);
    static void enableFile(bool enable);
    static void setMaxFileSize(uint64_t bytes);
    static void setMaxFiles(uint32_t count);
    
    static void trace(const std::string& msg);
    static void debug(const std::string& msg);
    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void fatal(const std::string& msg);
    
    static void log(LogLevel level, const std::string& msg);
    static void log(LogLevel level, const std::string& category, const std::string& msg);
    static void logf(LogLevel level, const char* fmt, ...);
    
    static void flush();
    static void rotate();
    
    static void onLog(std::function<void(const LogEntry&)> callback);
    
    static uint64_t getLogCount();
    static uint64_t getErrorCount();
    
    static std::string getLogPath();
    static std::vector<LogEntry> getRecentLogs(size_t count = 100);
    static void clearLogs();
    static bool isInitialized();
    static std::string formatMessage(LogLevel level, const std::string& message);
    static void logWithSource(LogLevel level, const std::string& message, 
                              const std::string& file, int line);
    static void setAllowSensitiveLogging(bool allow);
    static bool isAllowSensitiveLogging();
    
    // Sensitive data redaction
    static std::string redactPrivateKey(const std::string& data);
    static std::string redactAddress(const std::string& address);
    static std::string redactSeed(const std::string& seed);
    static std::string redactPassword(const std::string& password);
    static std::string redactSensitive(const std::string& data, const std::string& type = "secret");
};

#define LOG_TRACE(msg) synapse::utils::Logger::trace(msg)
#define LOG_DEBUG(msg) do { if (synapse::utils::Logger::getLevel() <= synapse::utils::LogLevel::DEBUG) synapse::utils::Logger::debug(msg); } while(0)
#define LOG_INFO(msg) synapse::utils::Logger::info(msg)
#define LOG_WARN(msg) synapse::utils::Logger::warn(msg)
#define LOG_ERROR(msg) synapse::utils::Logger::error(msg)
#define LOG_FATAL(msg) synapse::utils::Logger::fatal(msg)
#define LOG_SOURCE(level, msg) synapse::utils::Logger::logWithSource(level, msg, __FILE__, __LINE__)
#define LOG_SUBSYSTEM(level, subsystem, msg) synapse::utils::Logger::log(level, subsystem, msg)
#define LOG_WALLET_ERROR(msg) synapse::utils::Logger::log(synapse::utils::LogLevel::ERROR, "wallet", msg)
#define LOG_NETWORK_ERROR(msg) synapse::utils::Logger::log(synapse::utils::LogLevel::ERROR, "network", msg)
#define LOG_TOR_ERROR(msg) synapse::utils::Logger::log(synapse::utils::LogLevel::ERROR, "tor", msg)
#define LOG_RPC_ERROR(msg) synapse::utils::Logger::log(synapse::utils::LogLevel::ERROR, "rpc", msg)
#define LOG_CONFIG_ERROR(msg) synapse::utils::Logger::log(synapse::utils::LogLevel::ERROR, "config", msg)
#define LOG_PERSISTENCE_ERROR(msg) synapse::utils::Logger::log(synapse::utils::LogLevel::ERROR, "persistence", msg)

}
}
