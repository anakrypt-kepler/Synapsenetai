#pragma once

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <cstdint>

namespace synapse {

enum class ErrorCode {
    OK = 0,
    INVALID_ADDRESS,
    INSUFFICIENT_FUNDS,
    INVALID_SIGNATURE,
    NETWORK_ERROR,
    DATABASE_ERROR,
    CONSENSUS_FAILED,
    SPAM_DETECTED,
    MODEL_NOT_FOUND,
    ACCESS_DENIED,
    TIMEOUT,
    RATE_LIMITED,
    VALIDATION_FAILED,
    SERIALIZATION_ERROR,
    CRYPTO_ERROR,
    FILE_NOT_FOUND,
    PERMISSION_DENIED,
    ALREADY_EXISTS,
    NOT_FOUND,
    INVALID_STATE,
    INTERNAL_ERROR,
    CONFIG_ERROR,
    TOR_ERROR,
    WALLET_ERROR,
    PERSISTENCE_ERROR,
    BOOTSTRAP_ERROR,
    RPC_ERROR,
    UNKNOWN
};

enum class ErrorSeverity {
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

struct Error {
    ErrorCode code;
    ErrorSeverity severity;
    std::string message;
    std::string context;
    std::string file;
    int line;
    uint64_t timestamp;
    
    Error() : code(ErrorCode::OK), severity(ErrorSeverity::INFO), line(0), timestamp(0) {}
    Error(ErrorCode c, const std::string& msg) : code(c), severity(ErrorSeverity::ERROR), message(msg), line(0), timestamp(0) {}
};

template<typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)), error_(), hasValue_(true) {}
    Result(Error error) : value_(), error_(std::move(error)), hasValue_(false) {}
    
    bool ok() const { return hasValue_; }
    bool failed() const { return !hasValue_; }
    
    const T& value() const { return value_; }
    T& value() { return value_; }
    const Error& error() const { return error_; }
    
    T valueOr(const T& defaultValue) const { return hasValue_ ? value_ : defaultValue; }
    
private:
    T value_;
    Error error_;
    bool hasValue_;
};

template<>
class Result<void> {
public:
    Result() : error_(), hasValue_(true) {}
    Result(Error error) : error_(std::move(error)), hasValue_(false) {}
    
    bool ok() const { return hasValue_; }
    bool failed() const { return !hasValue_; }
    const Error& error() const { return error_; }
    
private:
    Error error_;
    bool hasValue_;
};

class ErrorHandler {
public:
    static ErrorHandler& instance();
    
    void setHandler(std::function<void(const Error&)> handler);
    void handle(const Error& error);
    void handle(ErrorCode code, const std::string& message);
    
    void pushContext(const std::string& context);
    void popContext();
    std::string getContext() const;
    
    std::vector<Error> getRecentErrors(size_t count = 10) const;
    void clearErrors();
    
    uint64_t getErrorCount() const;
    uint64_t getErrorCount(ErrorCode code) const;
    std::vector<std::pair<ErrorCode, uint64_t>> getErrorStats() const;
    
    Error getLastError() const;
    bool hasErrors() const;
    bool hasCriticalErrors() const;
    void logToFile(const std::string& path);

    struct SubsystemSummary {
        std::string subsystem;
        uint64_t errorCount;
        uint64_t warningCount;
        uint64_t criticalCount;
        std::string lastMessage;
        uint64_t lastTimestamp;
    };
    std::vector<SubsystemSummary> getSubsystemSummaries() const;
    
private:
    ErrorHandler();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class ScopedContext {
public:
    explicit ScopedContext(const std::string& ctx);
    ~ScopedContext();
    ScopedContext(const ScopedContext&) = delete;
    ScopedContext& operator=(const ScopedContext&) = delete;
};

const char* errorToString(ErrorCode code);
const char* severityToString(ErrorSeverity severity);
void throwIfError(ErrorCode code);
void throwIfError(const Error& error);

Error makeError(ErrorCode code, const std::string& message);
Error makeError(ErrorCode code, const std::string& message, const std::string& context);

#define SYNAPSE_ERROR(code, msg) synapse::Error{code, synapse::ErrorSeverity::ERROR, msg, "", __FILE__, __LINE__, 0}
#define SYNAPSE_CHECK(expr, code, msg) if (!(expr)) return synapse::Result<void>(SYNAPSE_ERROR(code, msg))
#define SYNAPSE_CONTEXT(name) synapse::ScopedContext _ctx_##__LINE__(name)

}
