#include "utils/logger.h"
#include <fstream>
#include <ctime>
#include <iostream>
#include <mutex>
#include <atomic>
#include <thread>
#include <cstdarg>
#include <sstream>
#include <filesystem>
#include <vector>
#include <deque>

namespace synapse {
namespace utils {

static LogLevel currentLevel = LogLevel::INFO;
static std::ofstream logFile;
static std::string logPath;
static std::string logPattern = "%Y-%m-%d %H:%M:%S";
static std::mutex logMutex;
static bool consoleEnabled = true;
static bool fileEnabled = true;
static uint64_t maxFileSize = 10 * 1024 * 1024;
static uint32_t maxFiles = 5;
static std::atomic<uint64_t> logCount{0};
static std::atomic<uint64_t> errorCount{0};
static std::function<void(const LogEntry&)> logCallback;
static std::deque<LogEntry> recentLogs;
static size_t maxRecentLogs = 1000;
static bool initialized = false;
static bool allowSensitive = false;

static const char* levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "?????";
    }
}

static uint64_t getThreadId() {
    std::hash<std::thread::id> hasher;
    return hasher(std::this_thread::get_id());
}

static void writeLog(LogLevel level, const std::string& category, const std::string& msg) {
    if (level < currentLevel) return;
    
    std::lock_guard<std::mutex> lock(logMutex);
    
    time_t now = std::time(nullptr);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), logPattern.c_str(), std::localtime(&now));
    
    std::string outMsg = msg;
    if (!allowSensitive) {
        auto sanitize = [](const std::string& in) {
            std::string s = in;
            const std::vector<std::string> keys = {"password", "pass", "pwd", "secret", "private_key", "privkey", "mnemonic", "seed"};
            for (const auto& k : keys) {
                size_t pos = 0;
                while ((pos = s.find(k, pos)) != std::string::npos) {
                    size_t sep = std::string::npos;
                    size_t i = pos + k.size();
                    while (i < s.size() && (s[i] == ' ' || s[i] == '"' || s[i] == '\'' || s[i] == ':' || s[i] == '=')) i++;
                    sep = i;
                    size_t end = sep;
                    while (end < s.size() && s[end] != '"' && s[end] != '\'' && s[end] != ' ' && s[end] != ',' && s[end] != ')' && s[end] != ';' && s[end] != '\n') end++;
                    if (sep < end) {
                        s.replace(sep, end - sep, "[REDACTED]");
                        pos = sep + 9;
                    } else pos += k.size();
                }
            }
            return s;
        };
        outMsg = sanitize(outMsg);
    }

    std::ostringstream oss;
    oss << timeBuf << " [" << levelToString(level) << "]";
    if (!category.empty()) {
        oss << " [" << category << "]";
    }
    oss << " " << outMsg << "\n";

    std::string line = oss.str();
    
    if (consoleEnabled) {
        if (level >= LogLevel::ERROR) {
            std::cerr << line;
        } else {
            std::cout << line;
        }
    }
    
    if (fileEnabled && logFile.is_open()) {
        logFile << line;
        logFile.flush();
        
        if (logFile.tellp() > static_cast<std::streampos>(maxFileSize)) {
            Logger::rotate();
        }
    }
    
    logCount++;
    if (level >= LogLevel::ERROR) errorCount++;
    
    LogEntry entry;
    entry.level = level;
    entry.message = outMsg;
    entry.category = category;
    entry.timestamp = static_cast<uint64_t>(now);
    entry.threadId = getThreadId();
    
    recentLogs.push_back(entry);
    while (recentLogs.size() > maxRecentLogs) {
        recentLogs.pop_front();
    }
    
    if (logCallback) {
        logCallback(entry);
    }
}

void Logger::init(const std::string& path) {
    std::lock_guard<std::mutex> lock(logMutex);
    logPath = path;
    
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    
    logFile.open(path, std::ios::app);
    initialized = true;
    const char* env = std::getenv("SYNAPSENET_ALLOW_SENSITIVE_LOGS");
    if (env && *env) {
        std::string v(env);
        if (v == "1" || v == "true" || v == "TRUE") allowSensitive = true;
    }
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open()) {
        logFile.flush();
        logFile.close();
    }
    initialized = false;
}

void Logger::setLevel(LogLevel level) {
    currentLevel = level;
}

void Logger::setAllowSensitiveLogging(bool allow) {
    allowSensitive = allow;
}

bool Logger::isAllowSensitiveLogging() {
    return allowSensitive;
}

LogLevel Logger::getLevel() {
    return currentLevel;
}

void Logger::setPattern(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(logMutex);
    logPattern = pattern;
}

void Logger::enableConsole(bool enable) {
    consoleEnabled = enable;
}

void Logger::enableFile(bool enable) {
    fileEnabled = enable;
}

void Logger::setMaxFileSize(uint64_t bytes) {
    maxFileSize = bytes;
}

void Logger::setMaxFiles(uint32_t count) {
    maxFiles = count;
}

void Logger::trace(const std::string& msg) {
    writeLog(LogLevel::TRACE, "", msg);
}

void Logger::debug(const std::string& msg) {
    writeLog(LogLevel::DEBUG, "", msg);
}

void Logger::info(const std::string& msg) {
    writeLog(LogLevel::INFO, "", msg);
}

void Logger::warn(const std::string& msg) {
    writeLog(LogLevel::WARN, "", msg);
}

void Logger::error(const std::string& msg) {
    writeLog(LogLevel::ERROR, "", msg);
}

void Logger::fatal(const std::string& msg) {
    writeLog(LogLevel::FATAL, "", msg);
}

void Logger::log(LogLevel level, const std::string& msg) {
    writeLog(level, "", msg);
}

void Logger::log(LogLevel level, const std::string& category, const std::string& msg) {
    writeLog(level, category, msg);
}

void Logger::logf(LogLevel level, const char* fmt, ...) {
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    writeLog(level, "", buf);
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open()) {
        logFile.flush();
    }
}

void Logger::rotate() {
    if (logPath.empty()) return;
    
    if (logFile.is_open()) {
        logFile.close();
    }
    
    for (int i = maxFiles - 1; i >= 1; i--) {
        std::string oldPath = logPath + "." + std::to_string(i);
        std::string newPath = logPath + "." + std::to_string(i + 1);
        if (std::filesystem::exists(oldPath)) {
            if (i == static_cast<int>(maxFiles) - 1) {
                std::filesystem::remove(oldPath);
            } else {
                std::filesystem::rename(oldPath, newPath);
            }
        }
    }
    
    if (std::filesystem::exists(logPath)) {
        std::filesystem::rename(logPath, logPath + ".1");
    }
    
    logFile.open(logPath, std::ios::app);
}

void Logger::onLog(std::function<void(const LogEntry&)> callback) {
    std::lock_guard<std::mutex> lock(logMutex);
    logCallback = callback;
}

uint64_t Logger::getLogCount() {
    return logCount;
}

uint64_t Logger::getErrorCount() {
    return errorCount;
}

std::string Logger::getLogPath() {
    std::lock_guard<std::mutex> lock(logMutex);
    return logPath;
}

std::vector<LogEntry> Logger::getRecentLogs(size_t count) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::vector<LogEntry> result;
    size_t start = recentLogs.size() > count ? recentLogs.size() - count : 0;
    for (size_t i = start; i < recentLogs.size(); i++) {
        result.push_back(recentLogs[i]);
    }
    return result;
}

void Logger::clearLogs() {
    std::lock_guard<std::mutex> lock(logMutex);
    recentLogs.clear();
    logCount = 0;
    errorCount = 0;
}

bool Logger::isInitialized() {
    return initialized;
}

std::string Logger::formatMessage(LogLevel level, const std::string& message) {
    time_t now = std::time(nullptr);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), logPattern.c_str(), std::localtime(&now));
    
    std::ostringstream oss;
    oss << "[" << timeBuf << "] [" << levelToString(level) << "] " << message;
    return oss.str();
}

void Logger::logWithSource(LogLevel level, const std::string& message, 
                           const std::string& file, int line) {
    std::string fullMsg = message + " (" + file + ":" + std::to_string(line) + ")";
    log(level, fullMsg);
}

std::string Logger::redactPrivateKey(const std::string& data) {
    if (!allowSensitive) {
        if (data.length() > 8) {
            return data.substr(0, 4) + "..." + data.substr(data.length() - 4);
        }
        return "[REDACTED_KEY]";
    }
    return data;
}

std::string Logger::redactAddress(const std::string& address) {
    if (!allowSensitive) {
        if (address.length() > 8) {
            return address.substr(0, 4) + "..." + address.substr(address.length() - 4);
        }
        return "[REDACTED_ADDR]";
    }
    return address;
}

std::string Logger::redactSeed(const std::string& seed) {
    if (!allowSensitive) {
        return "[REDACTED_SEED]";
    }
    return seed;
}

std::string Logger::redactPassword(const std::string& password) {
    if (!allowSensitive) {
        return "[REDACTED_PASSWORD]";
    }
    return password;
}

std::string Logger::redactSensitive(const std::string& data, const std::string& type) {
    if (!allowSensitive) {
        return "[REDACTED_" + type + "]";
    }
    return data;
}

}
}
