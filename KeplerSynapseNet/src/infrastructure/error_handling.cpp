#include "error_handling.h"
#include <stdexcept>
#include <mutex>
#include <deque>
#include <unordered_map>
#include <ctime>
#include <cstdio>

namespace synapse {

const char* errorToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::OK: return "OK";
        case ErrorCode::INVALID_ADDRESS: return "Invalid address";
        case ErrorCode::INSUFFICIENT_FUNDS: return "Insufficient funds";
        case ErrorCode::INVALID_SIGNATURE: return "Invalid signature";
        case ErrorCode::NETWORK_ERROR: return "Network error";
        case ErrorCode::DATABASE_ERROR: return "Database error";
        case ErrorCode::CONSENSUS_FAILED: return "Consensus failed";
        case ErrorCode::SPAM_DETECTED: return "Spam detected";
        case ErrorCode::MODEL_NOT_FOUND: return "Model not found";
        case ErrorCode::ACCESS_DENIED: return "Access denied";
        case ErrorCode::TIMEOUT: return "Timeout";
        case ErrorCode::RATE_LIMITED: return "Rate limited";
        case ErrorCode::VALIDATION_FAILED: return "Validation failed";
        case ErrorCode::SERIALIZATION_ERROR: return "Serialization error";
        case ErrorCode::CRYPTO_ERROR: return "Cryptographic error";
        case ErrorCode::FILE_NOT_FOUND: return "File not found";
        case ErrorCode::PERMISSION_DENIED: return "Permission denied";
        case ErrorCode::ALREADY_EXISTS: return "Already exists";
        case ErrorCode::NOT_FOUND: return "Not found";
        case ErrorCode::INVALID_STATE: return "Invalid state";
        case ErrorCode::INTERNAL_ERROR: return "Internal error";
        case ErrorCode::CONFIG_ERROR: return "Configuration error";
        case ErrorCode::TOR_ERROR: return "Tor subsystem error";
        case ErrorCode::WALLET_ERROR: return "Wallet error";
        case ErrorCode::PERSISTENCE_ERROR: return "Persistence error";
        case ErrorCode::BOOTSTRAP_ERROR: return "Bootstrap error";
        case ErrorCode::RPC_ERROR: return "RPC error";
        default: return "Unknown error";
    }
}

const char* severityToString(ErrorSeverity severity) {
    switch (severity) {
        case ErrorSeverity::INFO: return "INFO";
        case ErrorSeverity::WARNING: return "WARNING";
        case ErrorSeverity::ERROR: return "ERROR";
        case ErrorSeverity::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

void throwIfError(ErrorCode code) {
    if (code != ErrorCode::OK) {
        throw std::runtime_error(errorToString(code));
    }
}

void throwIfError(const Error& error) {
    if (error.code != ErrorCode::OK) {
        std::string msg = error.message;
        if (!error.context.empty()) {
            msg += " [" + error.context + "]";
        }
        throw std::runtime_error(msg);
    }
}

Error makeError(ErrorCode code, const std::string& message) {
    Error err;
    err.code = code;
    err.severity = ErrorSeverity::ERROR;
    err.message = message;
    err.timestamp = static_cast<uint64_t>(std::time(nullptr));
    return err;
}

Error makeError(ErrorCode code, const std::string& message, const std::string& context) {
    Error err = makeError(code, message);
    err.context = context;
    return err;
}

struct ErrorHandler::Impl {
    std::function<void(const Error&)> handler;
    std::deque<Error> recentErrors;
    std::vector<std::string> contextStack;
    std::unordered_map<int, uint64_t> errorCounts;
    uint64_t totalErrors = 0;
    mutable std::mutex mtx;
    static constexpr size_t MAX_RECENT_ERRORS = 100;
};

ErrorHandler::ErrorHandler() : impl_(std::make_unique<Impl>()) {}

ErrorHandler& ErrorHandler::instance() {
    static ErrorHandler inst;
    return inst;
}

void ErrorHandler::setHandler(std::function<void(const Error&)> handler) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->handler = handler;
}

void ErrorHandler::handle(const Error& error) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    Error err = error;
    err.timestamp = static_cast<uint64_t>(std::time(nullptr));
    
    if (err.context.empty() && !impl_->contextStack.empty()) {
        std::string ctx;
        for (const auto& c : impl_->contextStack) {
            if (!ctx.empty()) ctx += " > ";
            ctx += c;
        }
        err.context = ctx;
    }
    
    impl_->recentErrors.push_back(err);
    if (impl_->recentErrors.size() > Impl::MAX_RECENT_ERRORS) {
        impl_->recentErrors.pop_front();
    }
    
    impl_->totalErrors++;
    impl_->errorCounts[static_cast<int>(error.code)]++;
    
    if (impl_->handler) {
        impl_->handler(err);
    }
}

void ErrorHandler::handle(ErrorCode code, const std::string& message) {
    handle(makeError(code, message));
}

void ErrorHandler::pushContext(const std::string& context) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->contextStack.push_back(context);
}

void ErrorHandler::popContext() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->contextStack.empty()) {
        impl_->contextStack.pop_back();
    }
}

std::string ErrorHandler::getContext() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string ctx;
    for (const auto& c : impl_->contextStack) {
        if (!ctx.empty()) ctx += " > ";
        ctx += c;
    }
    return ctx;
}

std::vector<Error> ErrorHandler::getRecentErrors(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<Error> result;
    size_t start = impl_->recentErrors.size() > count ? 
                   impl_->recentErrors.size() - count : 0;
    for (size_t i = impl_->recentErrors.size(); i > start; i--) {
        result.push_back(impl_->recentErrors[i - 1]);
    }
    return result;
}

void ErrorHandler::clearErrors() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->recentErrors.clear();
    impl_->errorCounts.clear();
    impl_->totalErrors = 0;
}

uint64_t ErrorHandler::getErrorCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->totalErrors;
}

uint64_t ErrorHandler::getErrorCount(ErrorCode code) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->errorCounts.find(static_cast<int>(code));
    return it != impl_->errorCounts.end() ? it->second : 0;
}

std::vector<std::pair<ErrorCode, uint64_t>> ErrorHandler::getErrorStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::pair<ErrorCode, uint64_t>> stats;
    for (const auto& [code, count] : impl_->errorCounts) {
        stats.emplace_back(static_cast<ErrorCode>(code), count);
    }
    return stats;
}

Error ErrorHandler::getLastError() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->recentErrors.empty()) {
        return Error{};
    }
    return impl_->recentErrors.back();
}

bool ErrorHandler::hasErrors() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return !impl_->recentErrors.empty();
}

bool ErrorHandler::hasCriticalErrors() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    for (const auto& err : impl_->recentErrors) {
        if (err.severity == ErrorSeverity::CRITICAL) {
            return true;
        }
    }
    return false;
}

void ErrorHandler::logToFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    FILE* f = fopen(path.c_str(), "a");
    if (!f) return;
    
    for (const auto& err : impl_->recentErrors) {
        fprintf(f, "[%lu] [%s] [%s] %s", 
                static_cast<unsigned long>(err.timestamp),
                severityToString(err.severity),
                errorToString(err.code),
                err.message.c_str());
        if (!err.context.empty()) {
            fprintf(f, " [%s]", err.context.c_str());
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

std::vector<ErrorHandler::SubsystemSummary> ErrorHandler::getSubsystemSummaries() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::unordered_map<std::string, SubsystemSummary> map;
    for (const auto& err : impl_->recentErrors) {
        std::string subsys = err.context.empty() ? "general" : err.context;
        auto dot = subsys.find(" > ");
        if (dot != std::string::npos) subsys = subsys.substr(0, dot);
        auto& s = map[subsys];
        s.subsystem = subsys;
        if (err.severity == ErrorSeverity::ERROR) s.errorCount++;
        if (err.severity == ErrorSeverity::WARNING) s.warningCount++;
        if (err.severity == ErrorSeverity::CRITICAL) s.criticalCount++;
        if (err.timestamp >= s.lastTimestamp) {
            s.lastTimestamp = err.timestamp;
            s.lastMessage = err.message;
        }
    }
    std::vector<SubsystemSummary> out;
    out.reserve(map.size());
    for (auto& [k, v] : map) out.push_back(std::move(v));
    return out;
}

ScopedContext::ScopedContext(const std::string& ctx) {
    ErrorHandler::instance().pushContext(ctx);
}

ScopedContext::~ScopedContext() {
    ErrorHandler::instance().popContext();
}

}
