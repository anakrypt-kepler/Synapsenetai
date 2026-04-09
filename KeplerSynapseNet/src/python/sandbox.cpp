#include "python/sandbox.h"
#include <chrono>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <set>
#include <fstream>
#include <regex>
#include <thread>
#include <atomic>
#include <cmath>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
#include <fcntl.h>
#include <cstring>

namespace synapse {
namespace python {

struct Sandbox::Impl {
    SandboxConfig config;
    bool initialized = false;
    std::set<std::string> allowedImports;
    std::set<std::string> forbiddenBuiltins;
    std::set<std::string> enabledModules;
    std::unordered_map<std::string, std::string> globals;
    std::atomic<uint64_t> executionCount{0};
    std::atomic<uint64_t> violationCount{0};
    std::atomic<uint64_t> successfulExecutions{0};
    std::atomic<uint64_t> failedExecutions{0};
    std::atomic<uint64_t> timeouts{0};
    std::atomic<uint64_t> memoryExceeded{0};
    std::atomic<uint64_t> totalExecutionTimeMs{0};
    std::atomic<uint64_t> peakMemoryUsage{0};
    std::function<void(SecurityViolation, const std::string&)> violationCallback;
    std::function<void(const std::string&)> timeoutCallback;
    std::string lastError;
    std::string pythonPath;
    
    Impl() {
        allowedImports = {"math", "string", "re", "json", "collections", 
                          "itertools", "functools", "operator", "random",
                          "hashlib", "base64", "datetime", "decimal"};
        forbiddenBuiltins = {"eval", "exec", "compile", "open", "input",
                             "__import__", "globals", "locals", "vars",
                             "dir", "getattr", "setattr", "delattr",
                             "breakpoint", "memoryview", "bytearray",
                             "__subclasses__", "__mro__", "__bases__", "__class__"};
        config.timeoutMs = 5000;
        config.memoryLimit = 64 * 1024 * 1024;
        config.maxOutputSize = 1024 * 1024;
        config.maxRecursionDepth = 100;
        
        pythonPath = "/usr/bin/python3";
        if (access(pythonPath.c_str(), X_OK) != 0) {
            pythonPath = "/usr/local/bin/python3";
        }
        if (access(pythonPath.c_str(), X_OK) != 0) {
            pythonPath = "python3";
        }
    }
    
    double calculateQualityScore(const ValidationContext& ctx);
    double calculateNoveltyScore(const ValidationContext& ctx);
    double calculateRelevanceScore(const ValidationContext& ctx);
    double calculateConsistencyScore(const ValidationContext& ctx);
    bool containsSpam(const std::string& text);
    std::vector<std::string> extractKeywords(const std::string& text);
    SecurityViolation checkImports(const std::string& code);
    SecurityViolation checkBuiltins(const std::string& code);
    SecurityViolation checkFilesystem(const std::string& code);
    SecurityViolation checkNetwork(const std::string& code);
    SecurityViolation checkShell(const std::string& code);
    
    std::string wrapCode(const std::string& code);
    SandboxResult executeInProcess(const std::string& code, uint32_t timeoutMs);
};

std::string Sandbox::Impl::wrapCode(const std::string& code) {
    std::ostringstream wrapped;
    wrapped << "import sys\n";
    wrapped << "import resource\n";
    wrapped << "resource.setrlimit(resource.RLIMIT_AS, (" << config.memoryLimit << ", " << config.memoryLimit << "))\n";
    wrapped << "resource.setrlimit(resource.RLIMIT_CPU, (5, 5))\n";
    wrapped << "resource.setrlimit(resource.RLIMIT_FSIZE, (0, 0))\n";
    wrapped << "resource.setrlimit(resource.RLIMIT_NOFILE, (16, 16))\n";
    wrapped << "sys.setrecursionlimit(" << config.maxRecursionDepth << ")\n";
    wrapped << "\n";
    
    wrapped << "import builtins\n";
    wrapped << "_original_import = builtins.__import__\n";
    wrapped << "_allowed = {";
    bool first = true;
    for (const auto& mod : allowedImports) {
        if (!first) wrapped << ", ";
        wrapped << "'" << mod << "'";
        first = false;
    }
    wrapped << "}\n";
    wrapped << "def _safe_import(name, *args, **kwargs):\n";
    wrapped << "    if name.split('.')[0] not in _allowed:\n";
    wrapped << "        raise ImportError(f'Import of {name} is not allowed')\n";
    wrapped << "    return _original_import(name, *args, **kwargs)\n";
    wrapped << "builtins.__import__ = _safe_import\n";
    wrapped << "\n";
    
    for (const auto& builtin : forbiddenBuiltins) {
        wrapped << "if hasattr(builtins, '" << builtin << "'): delattr(builtins, '" << builtin << "')\n";
    }
    wrapped << "\n";

    // Block class hierarchy traversal
    wrapped << "_orig_type = type\n";
    wrapped << "def _restricted_type(*args, **kwargs):\n";
    wrapped << "    if len(args) == 1:\n";
    wrapped << "        return _orig_type(args[0])\n";
    wrapped << "    raise TypeError('type() with 3 arguments is not allowed')\n";
    wrapped << "builtins.type = _restricted_type\n";
    wrapped << "\n";

    wrapped << "try:\n";
    std::istringstream codeStream(code);
    std::string line;
    while (std::getline(codeStream, line)) {
        wrapped << "    " << line << "\n";
    }
    wrapped << "except Exception as e:\n";
    wrapped << "    print(f'Error: {type(e).__name__}: {e}', file=sys.stderr)\n";
    wrapped << "    sys.exit(1)\n";
    
    return wrapped.str();
}

SandboxResult Sandbox::Impl::executeInProcess(const std::string& code, uint32_t timeoutMs) {
    SandboxResult result;
    result.violation = SecurityViolation::NONE;
    
    int pipeOut[2], pipeErr[2];
    if (pipe(pipeOut) < 0 || pipe(pipeErr) < 0) {
        result.success = false;
        result.error = "Failed to create pipes";
        return result;
    }
    
    std::string wrappedCode = wrapCode(code);
    
    pid_t pid = fork();
    if (pid < 0) {
        result.success = false;
        result.error = "Failed to fork process";
        close(pipeOut[0]); close(pipeOut[1]);
        close(pipeErr[0]); close(pipeErr[1]);
        return result;
    }
    
    if (pid == 0) {
        close(pipeOut[0]);
        close(pipeErr[0]);
        
        dup2(pipeOut[1], STDOUT_FILENO);
        dup2(pipeErr[1], STDERR_FILENO);
        close(pipeOut[1]);
        close(pipeErr[1]);
        
        struct rlimit rl;
        rl.rlim_cur = rl.rlim_max = config.memoryLimit;
        setrlimit(RLIMIT_AS, &rl);
        
        rl.rlim_cur = rl.rlim_max = 5;
        setrlimit(RLIMIT_CPU, &rl);
        
        rl.rlim_cur = rl.rlim_max = 0;
        setrlimit(RLIMIT_FSIZE, &rl);
        
        execlp(pythonPath.c_str(), "python3", "-c", wrappedCode.c_str(), nullptr);
        _exit(127);
    }
    
    close(pipeOut[1]);
    close(pipeErr[1]);
    
    fcntl(pipeOut[0], F_SETFL, O_NONBLOCK);
    fcntl(pipeErr[0], F_SETFL, O_NONBLOCK);
    
    auto startTime = std::chrono::steady_clock::now();
    auto deadline = startTime + std::chrono::milliseconds(timeoutMs);
    
    std::string output, errorOutput;
    char buffer[4096];
    bool processEnded = false;
    int status = 0;
    
    while (!processEnded) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            result.success = false;
            result.error = "Execution timeout";
            result.violation = SecurityViolation::TIMEOUT;
            timeouts++;
            close(pipeOut[0]);
            close(pipeErr[0]);
            return result;
        }
        
        ssize_t n;
        while ((n = read(pipeOut[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[n] = '\0';
            output += buffer;
            if (output.size() > config.maxOutputSize) {
                output = output.substr(0, config.maxOutputSize);
                break;
            }
        }
        
        while ((n = read(pipeErr[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[n] = '\0';
            errorOutput += buffer;
        }
        
        int waitResult = waitpid(pid, &status, WNOHANG);
        if (waitResult > 0) {
            processEnded = true;
        } else if (waitResult < 0) {
            processEnded = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    while (true) {
        ssize_t n = read(pipeOut[0], buffer, sizeof(buffer) - 1);
        if (n <= 0) break;
        buffer[n] = '\0';
        output += buffer;
    }
    while (true) {
        ssize_t n = read(pipeErr[0], buffer, sizeof(buffer) - 1);
        if (n <= 0) break;
        buffer[n] = '\0';
        errorOutput += buffer;
    }
    
    close(pipeOut[0]);
    close(pipeErr[0]);
    
    auto endTime = std::chrono::steady_clock::now();
    result.executionTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    if (WIFEXITED(status)) {
        int exitCode = WEXITSTATUS(status);
        if (exitCode == 0) {
            result.success = true;
            result.output = output;
        } else if (exitCode == 127) {
            result.success = false;
            result.error = "Python interpreter not found";
        } else {
            result.success = false;
            result.error = errorOutput.empty() ? "Execution failed" : errorOutput;
        }
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        result.success = false;
        if (sig == SIGKILL || sig == SIGXCPU) {
            result.error = "Resource limit exceeded";
            result.violation = SecurityViolation::TIMEOUT;
        } else {
            result.error = "Process terminated by signal " + std::to_string(sig);
        }
    }
    
    return result;
}

SecurityViolation Sandbox::Impl::checkImports(const std::string& code) {
    std::regex importRe(R"((?:^|\n)\s*(?:import|from)\s+([a-zA-Z_][a-zA-Z0-9_]*))");
    std::smatch match;
    std::string::const_iterator searchStart(code.cbegin());
    while (std::regex_search(searchStart, code.cend(), match, importRe)) {
        std::string module = match[1].str();
        if (allowedImports.find(module) == allowedImports.end()) {
            return SecurityViolation::FORBIDDEN_IMPORT;
        }
        searchStart = match.suffix().first;
    }
    return SecurityViolation::NONE;
}

SecurityViolation Sandbox::Impl::checkBuiltins(const std::string& code) {
    for (const auto& builtin : forbiddenBuiltins) {
        std::regex callRe("\\b" + builtin + "\\s*\\(");
        std::regex attrRe("\\." + builtin + "\\b");
        if (std::regex_search(code, callRe) || std::regex_search(code, attrRe)) {
            return SecurityViolation::FORBIDDEN_BUILTIN;
        }
    }
    return SecurityViolation::NONE;
}

SecurityViolation Sandbox::Impl::checkFilesystem(const std::string& code) {
    std::vector<std::string> fsPatterns = {
        R"(open\s*\()", R"(\.read\s*\()", R"(\.write\s*\()",
        R"(os\.)", R"(pathlib\.)", R"(Path\()", R"(shutil\.)", R"(glob\.)", R"(tempfile\.)"
    };
    for (const auto& pattern : fsPatterns) {
        std::regex re(pattern);
        if (std::regex_search(code, re)) return SecurityViolation::FILESYSTEM_ACCESS;
    }
    return SecurityViolation::NONE;
}

SecurityViolation Sandbox::Impl::checkNetwork(const std::string& code) {
    std::vector<std::string> netPatterns = {
        R"(socket\.)", R"(urllib\.)", R"(requests\.)", R"(http\.)", R"(ftplib\.)"
    };
    for (const auto& pattern : netPatterns) {
        std::regex re(pattern);
        if (std::regex_search(code, re)) return SecurityViolation::NETWORK_ACCESS;
    }
    return SecurityViolation::NONE;
}

SecurityViolation Sandbox::Impl::checkShell(const std::string& code) {
    std::vector<std::string> shellPatterns = {
        R"(subprocess\.)", R"(os\.system)", R"(os\.popen)", R"(os\.exec)", R"(popen\s*\()"
    };
    for (const auto& pattern : shellPatterns) {
        std::regex re(pattern);
        if (std::regex_search(code, re)) return SecurityViolation::SHELL_EXEC;
    }
    return SecurityViolation::NONE;
}

double Sandbox::Impl::calculateQualityScore(const ValidationContext& ctx) {
    double score = 0.5;
    if (ctx.answer.length() > 50) score += 0.1;
    if (ctx.answer.length() > 200) score += 0.1;
    if (!ctx.source.empty()) score += 0.1;
    if (!ctx.tags.empty()) score += 0.1;
    return std::min(1.0, score);
}

double Sandbox::Impl::calculateConsistencyScore(const ValidationContext& ctx) {
    double score = 0.7;
    if (!ctx.authorId.empty()) score += 0.1;
    if (ctx.timestamp > 0) score += 0.1;
    return std::min(1.0, score);
}

double Sandbox::Impl::calculateNoveltyScore(const ValidationContext& ctx) {
    double score = 0.7;
    auto keywords = extractKeywords(ctx.question + " " + ctx.answer);
    if (keywords.size() > 5) score += 0.1;
    if (keywords.size() > 10) score += 0.1;
    return std::min(1.0, score);
}

double Sandbox::Impl::calculateRelevanceScore(const ValidationContext& ctx) {
    double score = 0.6;
    std::string lowerQ = ctx.question, lowerA = ctx.answer;
    std::transform(lowerQ.begin(), lowerQ.end(), lowerQ.begin(), ::tolower);
    std::transform(lowerA.begin(), lowerA.end(), lowerA.begin(), ::tolower);
    auto qKeywords = extractKeywords(lowerQ);
    for (const auto& kw : qKeywords) {
        if (lowerA.find(kw) != std::string::npos) score += 0.05;
    }
    return std::min(1.0, score);
}

bool Sandbox::Impl::containsSpam(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    std::vector<std::string> spamWords = {"buy now", "click here", "free money"};
    for (const auto& spam : spamWords) {
        if (lower.find(spam) != std::string::npos) return true;
    }
    return false;
}

std::vector<std::string> Sandbox::Impl::extractKeywords(const std::string& text) {
    std::vector<std::string> keywords;
    std::string word;
    std::istringstream iss(text);
    std::set<std::string> stopWords = {"the", "a", "an", "is", "are", "was", "were", "be", "to", "of", "in", "for", "on", "with", "at", "by", "and", "or", "but", "if", "this", "that", "it"};
    while (iss >> word) {
        std::transform(word.begin(), word.end(), word.begin(), ::tolower);
        word.erase(std::remove_if(word.begin(), word.end(), [](char c) { return !std::isalnum(c); }), word.end());
        if (word.length() > 2 && stopWords.find(word) == stopWords.end()) {
            keywords.push_back(word);
        }
    }
    return keywords;
}

Sandbox::Sandbox() : impl_(std::make_unique<Impl>()) {}
Sandbox::~Sandbox() { shutdown(); }

bool Sandbox::init() { impl_->initialized = true; return true; }
void Sandbox::shutdown() { impl_->initialized = false; }
bool Sandbox::isInitialized() const { return impl_->initialized; }
void Sandbox::setConfig(const SandboxConfig& config) { impl_->config = config; }

CodeAnalysis Sandbox::analyzeCode(const std::string& code) {
    CodeAnalysis analysis;
    analysis.safe = true;
    
    std::regex importRe(R"((?:^|\n)\s*(?:import|from)\s+([a-zA-Z_][a-zA-Z0-9_]*))");
    std::smatch match;
    std::string::const_iterator searchStart(code.cbegin());
    while (std::regex_search(searchStart, code.cend(), match, importRe)) {
        analysis.imports.push_back(match[1].str());
        searchStart = match.suffix().first;
    }
    
    for (const auto& builtin : impl_->forbiddenBuiltins) {
        std::regex builtinRe("\\b" + builtin + "\\b");
        if (std::regex_search(code, builtinRe)) analysis.builtins.push_back(builtin);
    }
    
    auto v1 = impl_->checkImports(code);
    if (v1 != SecurityViolation::NONE) { analysis.safe = false; analysis.violations.push_back(v1); analysis.violationDetail = "Forbidden import"; }
    auto v2 = impl_->checkBuiltins(code);
    if (v2 != SecurityViolation::NONE) { analysis.safe = false; analysis.violations.push_back(v2); analysis.violationDetail = "Forbidden builtin"; }
    auto v3 = impl_->checkFilesystem(code);
    if (v3 != SecurityViolation::NONE) { analysis.safe = false; analysis.violations.push_back(v3); analysis.violationDetail = "Filesystem access"; }
    auto v4 = impl_->checkNetwork(code);
    if (v4 != SecurityViolation::NONE) { analysis.safe = false; analysis.violations.push_back(v4); analysis.violationDetail = "Network access"; }
    auto v5 = impl_->checkShell(code);
    if (v5 != SecurityViolation::NONE) { analysis.safe = false; analysis.violations.push_back(v5); analysis.violationDetail = "Shell execution"; }
    
    return analysis;
}

bool Sandbox::isCodeSafe(const std::string& code) { return analyzeCode(code).safe; }

std::string Sandbox::sanitizeCode(const std::string& code) {
    std::string sanitized = code;
    for (const auto& builtin : impl_->forbiddenBuiltins) {
        std::regex re("\\b" + builtin + "\\s*\\(");
        sanitized = std::regex_replace(sanitized, re, "_blocked_" + builtin + "(");
    }
    return sanitized;
}

SandboxResult Sandbox::execute(const std::string& code, const std::string& input) {
    SandboxResult result;
    result.violation = SecurityViolation::NONE;
    auto start = std::chrono::high_resolution_clock::now();
    impl_->executionCount++;
    
    auto analysis = analyzeCode(code);
    if (!analysis.safe) {
        result.success = false;
        result.error = "Security violation: " + analysis.violationDetail;
        result.violation = analysis.violations.empty() ? SecurityViolation::NONE : analysis.violations[0];
        impl_->violationCount++;
        impl_->failedExecutions++;
        if (impl_->violationCallback) impl_->violationCallback(result.violation, analysis.violationDetail);
        return result;
    }
    
    result = impl_->executeInProcess(code, impl_->config.timeoutMs);
    
    if (result.success) {
        impl_->successfulExecutions++;
    } else {
        impl_->failedExecutions++;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    result.executionTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    impl_->totalExecutionTimeMs += result.executionTimeMs;
    
    return result;
}

SandboxResult Sandbox::executeFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SandboxResult result;
        result.success = false;
        result.error = "Could not open file";
        result.violation = SecurityViolation::FILESYSTEM_ACCESS;
        return result;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return execute(buffer.str());
}

SandboxResult Sandbox::executeWithTimeout(const std::string& code, uint32_t timeoutMs) {
    auto analysis = analyzeCode(code);
    if (!analysis.safe) {
        SandboxResult result;
        result.success = false;
        result.error = "Security violation: " + analysis.violationDetail;
        result.violation = analysis.violations.empty() ? SecurityViolation::NONE : analysis.violations[0];
        return result;
    }
    
    return impl_->executeInProcess(code, timeoutMs);
}

PoEResult Sandbox::validateKnowledge(const ValidationContext& ctx) {
    PoEResult result;
    if (ctx.question.empty() || ctx.answer.empty()) {
        result.valid = false; result.reason = "Empty content"; result.overallScore = 0.0; return result;
    }
    if (ctx.question.length() < 10 || ctx.answer.length() < 20) {
        result.valid = false; result.reason = "Content too short"; result.overallScore = 0.0; return result;
    }
    if (impl_->containsSpam(ctx.question) || impl_->containsSpam(ctx.answer)) {
        result.valid = false; result.reason = "Spam detected"; result.overallScore = 0.0; return result;
    }
    
    result.qualityScore = impl_->calculateQualityScore(ctx);
    result.noveltyScore = impl_->calculateNoveltyScore(ctx);
    result.relevanceScore = impl_->calculateRelevanceScore(ctx);
    result.consistencyScore = impl_->calculateConsistencyScore(ctx);
    result.overallScore = result.qualityScore * 0.3 + result.noveltyScore * 0.25 + result.relevanceScore * 0.25 + result.consistencyScore * 0.2;
    result.valid = result.overallScore >= 0.5;
    result.detectedTopics = detectTopics(ctx.question + " " + ctx.answer);
    result.reason = result.valid ? "Validated" : "Below threshold";
    return result;
}

std::vector<std::string> Sandbox::detectTopics(const std::string& text) {
    std::vector<std::string> topics;
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    std::unordered_map<std::string, std::vector<std::string>> topicKeywords = {
        {"technology", {"computer", "software", "programming", "code"}},
        {"science", {"research", "experiment", "theory", "data"}},
        {"mathematics", {"equation", "formula", "calculate", "number"}}
    };
    
    for (const auto& [topic, keywords] : topicKeywords) {
        for (const auto& kw : keywords) {
            if (lower.find(kw) != std::string::npos) { topics.push_back(topic); break; }
        }
    }
    return topics;
}

double Sandbox::calculateSimilarity(const std::string& text1, const std::string& text2) {
    auto kw1 = impl_->extractKeywords(text1);
    auto kw2 = impl_->extractKeywords(text2);
    if (kw1.empty() || kw2.empty()) return 0.0;
    
    std::set<std::string> set1(kw1.begin(), kw1.end()), set2(kw2.begin(), kw2.end());
    std::vector<std::string> intersection, unionSet;
    std::set_intersection(set1.begin(), set1.end(), set2.begin(), set2.end(), std::back_inserter(intersection));
    std::set_union(set1.begin(), set1.end(), set2.begin(), set2.end(), std::back_inserter(unionSet));
    return unionSet.empty() ? 0.0 : static_cast<double>(intersection.size()) / unionSet.size();
}

double Sandbox::scoreKnowledge(const ValidationContext& ctx) { return validateKnowledge(ctx).overallScore; }
bool Sandbox::checkDuplicate(const std::string& question, const std::string& answer) { return false; }

std::vector<std::string> Sandbox::suggestTags(const std::string& question, const std::string& answer) {
    auto keywords = impl_->extractKeywords(question + " " + answer);
    std::vector<std::string> tags;
    for (size_t i = 0; i < std::min(keywords.size(), size_t(5)); i++) tags.push_back(keywords[i]);
    return tags;
}

void Sandbox::setTimeout(uint32_t ms) { impl_->config.timeoutMs = ms; }
void Sandbox::setMemoryLimit(uint64_t bytes) { impl_->config.memoryLimit = bytes; }
void Sandbox::setCpuLimit(double percentage) { impl_->config.cpuLimit = percentage; }
void Sandbox::addAllowedImport(const std::string& module) { impl_->allowedImports.insert(module); }
void Sandbox::removeAllowedImport(const std::string& module) { impl_->allowedImports.erase(module); }
std::vector<std::string> Sandbox::getAllowedImports() const { return std::vector<std::string>(impl_->allowedImports.begin(), impl_->allowedImports.end()); }
void Sandbox::setGlobal(const std::string& name, const std::string& value) { impl_->globals[name] = value; }
std::string Sandbox::getGlobal(const std::string& name) const { auto it = impl_->globals.find(name); return it != impl_->globals.end() ? it->second : ""; }
void Sandbox::clearGlobals() { impl_->globals.clear(); }
void Sandbox::addForbiddenBuiltin(const std::string& builtin) { impl_->forbiddenBuiltins.insert(builtin); }
std::vector<std::string> Sandbox::getForbiddenBuiltins() const { return std::vector<std::string>(impl_->forbiddenBuiltins.begin(), impl_->forbiddenBuiltins.end()); }
void Sandbox::onViolation(std::function<void(SecurityViolation, const std::string&)> callback) { impl_->violationCallback = callback; }
void Sandbox::onTimeout(std::function<void(const std::string&)> callback) { impl_->timeoutCallback = callback; }
uint64_t Sandbox::getExecutionCount() const { return impl_->executionCount; }
uint64_t Sandbox::getViolationCount() const { return impl_->violationCount; }

SandboxStats Sandbox::getStats() const {
    SandboxStats stats{};
    stats.totalExecutions = impl_->executionCount;
    stats.successfulExecutions = impl_->successfulExecutions;
    stats.failedExecutions = impl_->failedExecutions;
    stats.timeouts = impl_->timeouts;
    stats.memoryExceeded = impl_->memoryExceeded;
    stats.securityViolations = impl_->violationCount;
    stats.totalExecutionTimeMs = impl_->totalExecutionTimeMs;
    stats.avgExecutionTimeMs = impl_->executionCount > 0 ? impl_->totalExecutionTimeMs / impl_->executionCount : 0;
    stats.peakMemoryUsage = impl_->peakMemoryUsage;
    return stats;
}

void Sandbox::resetStats() {
    impl_->executionCount = 0;
    impl_->violationCount = 0;
    impl_->successfulExecutions = 0;
    impl_->failedExecutions = 0;
    impl_->timeouts = 0;
    impl_->memoryExceeded = 0;
    impl_->totalExecutionTimeMs = 0;
    impl_->peakMemoryUsage = 0;
}

bool Sandbox::isCodeSafe(const std::string& code) const { return const_cast<Sandbox*>(this)->analyzeCode(code).safe; }

std::vector<std::string> Sandbox::getUnsafePatterns(const std::string& code) const {
    std::vector<std::string> patterns;
    std::vector<std::pair<std::string, std::string>> checks = {
        {"forbidden_builtin", R"((eval|exec|compile|__import__|open|input)\s*\()"},
        {"filesystem_access", R"((?:open\s*\(|os\.|pathlib\.|shutil\.|tempfile\.|Path\())"},
        {"network_access", R"((?:socket\.|requests\.|urllib\.|http\.|ftplib\.))"},
        {"shell_exec", R"((?:subprocess\.|popen\s*\(|system\s*\()))"}
    };

    for (const auto& [name, reStr] : checks) {
        std::regex re(reStr);
        std::smatch m;
        if (std::regex_search(code, m, re)) {
            std::string snippet = m.size() > 0 ? m.str(0) : "";
            patterns.push_back(name + ": " + snippet);
        }
    }
    return patterns;
}

void Sandbox::setResourceLimits(uint64_t maxMemory, uint64_t maxCpu) {
    impl_->config.memoryLimit = maxMemory;
}

void Sandbox::enableModule(const std::string& module) { impl_->enabledModules.insert(module); }
void Sandbox::disableModule(const std::string& module) { impl_->enabledModules.erase(module); }
std::vector<std::string> Sandbox::getEnabledModules() const { return std::vector<std::string>(impl_->enabledModules.begin(), impl_->enabledModules.end()); }
bool Sandbox::isModuleEnabled(const std::string& module) const { return impl_->enabledModules.find(module) != impl_->enabledModules.end(); }
void Sandbox::setMaxOutputSize(size_t bytes) { impl_->config.maxOutputSize = bytes; }
void Sandbox::setMaxRecursionDepth(uint32_t depth) { impl_->config.maxRecursionDepth = depth; }
std::string Sandbox::getLastError() const { return impl_->lastError; }
void Sandbox::clearLastError() { impl_->lastError.clear(); }

}
}
