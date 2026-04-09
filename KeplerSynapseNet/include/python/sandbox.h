#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <unordered_map>

namespace synapse {
namespace python {

enum class SecurityViolation {
    NONE = 0,
    FORBIDDEN_IMPORT,
    FILESYSTEM_ACCESS,
    NETWORK_ACCESS,
    SHELL_EXEC,
    EVAL_EXEC,
    TIMEOUT,
    MEMORY_EXCEEDED,
    FORBIDDEN_BUILTIN
};

struct SandboxResult {
    bool success;
    std::string output;
    std::string error;
    double score;
    uint64_t executionTimeMs;
    uint64_t memoryUsedBytes;
    SecurityViolation violation;
};

struct ValidationContext {
    std::string question;
    std::string answer;
    std::string source;
    std::vector<std::string> tags;
    std::unordered_map<std::string, std::string> metadata;
    std::string authorId;
    uint64_t timestamp;
};

struct PoEResult {
    bool valid;
    double qualityScore;
    double noveltyScore;
    double relevanceScore;
    double consistencyScore;
    double overallScore;
    std::string reason;
    std::vector<std::string> suggestions;
    std::vector<std::string> detectedTopics;
};

struct CodeAnalysis {
    bool safe;
    std::vector<std::string> imports;
    std::vector<std::string> builtins;
    std::vector<SecurityViolation> violations;
    std::string violationDetail;
};

struct SandboxConfig {
    uint32_t timeoutMs = 1000;
    uint64_t memoryLimit = 50 * 1024 * 1024;
    double cpuLimit = 0.5;
    uint32_t maxOutputSize = 1024 * 1024;
    uint32_t maxRecursionDepth = 100;
    bool allowFileRead = false;
    bool allowNetworkRead = false;
};

struct SandboxStats {
    uint64_t totalExecutions;
    uint64_t successfulExecutions;
    uint64_t failedExecutions;
    uint64_t timeouts;
    uint64_t memoryExceeded;
    uint64_t securityViolations;
    uint64_t totalExecutionTimeMs;
    uint64_t avgExecutionTimeMs;
    uint64_t peakMemoryUsage;
};

class Sandbox {
public:
    Sandbox();
    ~Sandbox();
    
    bool init();
    void shutdown();
    bool isInitialized() const;
    void setConfig(const SandboxConfig& config);
    
    SandboxResult execute(const std::string& code, const std::string& input = "");
    SandboxResult executeFile(const std::string& path);
    SandboxResult executeWithTimeout(const std::string& code, uint32_t timeoutMs);
    
    CodeAnalysis analyzeCode(const std::string& code);
    bool isCodeSafe(const std::string& code);
    std::string sanitizeCode(const std::string& code);
    
    PoEResult validateKnowledge(const ValidationContext& ctx);
    double scoreKnowledge(const ValidationContext& ctx);
    bool checkDuplicate(const std::string& question, const std::string& answer);
    std::vector<std::string> suggestTags(const std::string& question, const std::string& answer);
    std::vector<std::string> detectTopics(const std::string& text);
    double calculateSimilarity(const std::string& text1, const std::string& text2);
    
    void setTimeout(uint32_t ms);
    void setMemoryLimit(uint64_t bytes);
    void setCpuLimit(double percentage);
    
    void addAllowedImport(const std::string& module);
    void removeAllowedImport(const std::string& module);
    std::vector<std::string> getAllowedImports() const;
    void addForbiddenBuiltin(const std::string& builtin);
    std::vector<std::string> getForbiddenBuiltins() const;
    
    void setGlobal(const std::string& name, const std::string& value);
    std::string getGlobal(const std::string& name) const;
    void clearGlobals();
    
    void onViolation(std::function<void(SecurityViolation, const std::string&)> callback);
    void onTimeout(std::function<void(const std::string&)> callback);
    
    uint64_t getExecutionCount() const;
    uint64_t getViolationCount() const;
    
    SandboxStats getStats() const;
    void resetStats();
    
    bool isCodeSafe(const std::string& code) const;
    std::vector<std::string> getUnsafePatterns(const std::string& code) const;
    
    void setResourceLimits(uint64_t maxMemory, uint64_t maxCpu);
    void enableModule(const std::string& module);
    void disableModule(const std::string& module);
    std::vector<std::string> getEnabledModules() const;
    bool isModuleEnabled(const std::string& module) const;
    
    void setMaxOutputSize(size_t bytes);
    void setMaxRecursionDepth(uint32_t depth);
    
    std::string getLastError() const;
    void clearLastError();
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
