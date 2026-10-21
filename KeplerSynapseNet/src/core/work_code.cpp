// In-process compiler and interpreter for PoE work bundles.
// The language is integer expressions: functions, if/then/else, asserts, emit.
// Two cells hashing the transcript must get the same bytes.

#include "core/work_code.h"

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace synapse::core {
namespace {

constexpr int64_t kOpPush = 1;
constexpr int64_t kOpLoad = 2;
constexpr int64_t kOpDup = 3;
constexpr int64_t kOpPop = 4;
constexpr int64_t kOpAdd = 5;
constexpr int64_t kOpSub = 6;
constexpr int64_t kOpMul = 7;
constexpr int64_t kOpDiv = 8;
constexpr int64_t kOpMod = 9;
constexpr int64_t kOpNeg = 10;
constexpr int64_t kOpNot = 11;
constexpr int64_t kOpEq = 12;
constexpr int64_t kOpNe = 13;
constexpr int64_t kOpLt = 14;
constexpr int64_t kOpLe = 15;
constexpr int64_t kOpGt = 16;
constexpr int64_t kOpGe = 17;
constexpr int64_t kOpJmp = 20;
constexpr int64_t kOpJz = 21;
constexpr int64_t kOpJnz = 22;
constexpr int64_t kOpCall = 23;
constexpr int64_t kOpRet = 24;
constexpr int64_t kOpEmit = 25;

constexpr uint32_t kMaxSteps = 100000;
constexpr uint32_t kMaxDepth = 32;
constexpr uint32_t kMaxFuncs = 32;
constexpr uint32_t kMaxParams = 8;
constexpr uint32_t kMaxCode = 4096;
constexpr uint32_t kMaxOutput = 1024;
constexpr size_t kMaxBundle = 65536;

struct Func {
    std::string name;
    std::vector<std::string> params;
    std::vector<int64_t> code;
};

struct TestOp {
    bool assertTrue = true;
    std::vector<int64_t> code;
};

struct Program {
    std::vector<Func> funcs;
    std::vector<TestOp> tests;
};

struct Fail : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Parser {
public:
    explicit Parser(std::string text) : text_(std::move(text)) {}

    Program parse() {
        Program prog;
        skip();
        while (!eof()) {
            if (take("fn")) parseFn(prog);
            else if (take("assert")) parseTest(prog, true);
            else if (take("emit")) parseTest(prog, false);
            else throw Fail("compile_failed");
            skip();
        }
        if (prog.funcs.empty() || prog.tests.empty()) throw Fail("work_tests_required");
        return prog;
    }

private:
    std::string text_;
    size_t i_ = 0;
    std::unordered_map<std::string, uint32_t> funcs_;
    std::vector<std::string> locals_;

    bool eof() const { return i_ >= text_.size(); }

    void skip() {
        for (;;) {
            while (!eof() && std::isspace(static_cast<unsigned char>(text_[i_]))) ++i_;
            if (i_ + 1 < text_.size() && text_[i_] == '/' && text_[i_ + 1] == '/') {
                while (!eof() && text_[i_] != '\n') ++i_;
                continue;
            }
            break;
        }
    }

    bool take(const char* kw) {
        skip();
        size_t n = std::char_traits<char>::length(kw);
        if (text_.compare(i_, n, kw) != 0) return false;
        if (i_ + n < text_.size()) {
            unsigned char c = static_cast<unsigned char>(text_[i_ + n]);
            if (std::isalnum(c) || c == '_') return false;
        }
        i_ += n;
        return true;
    }

    bool takeChar(char c) {
        skip();
        if (eof() || text_[i_] != c) return false;
        ++i_;
        return true;
    }

    void expect(const char* kw) {
        if (!take(kw)) throw Fail("compile_failed");
    }

    void expectChar(char c) {
        if (!takeChar(c)) throw Fail("compile_failed");
    }

    std::string ident() {
        skip();
        if (eof()) throw Fail("compile_failed");
        unsigned char c = static_cast<unsigned char>(text_[i_]);
        if (!(std::isalpha(c) || c == '_')) throw Fail("compile_failed");
        size_t start = i_++;
        while (!eof()) {
            unsigned char d = static_cast<unsigned char>(text_[i_]);
            if (!(std::isalnum(d) || d == '_')) break;
            ++i_;
        }
        std::string name = text_.substr(start, i_ - start);
        if (name == "fn" || name == "assert" || name == "emit" || name == "if" ||
            name == "then" || name == "else") {
            throw Fail("compile_failed");
        }
        return name;
    }

    bool startsIdent() const {
        if (i_ >= text_.size()) return false;
        unsigned char c = static_cast<unsigned char>(text_[i_]);
        return std::isalpha(c) || c == '_';
    }

    void parseFn(Program& prog) {
        if (prog.funcs.size() >= kMaxFuncs) throw Fail("compile_failed");
        std::string name = ident();
        if (funcs_.count(name)) throw Fail("compile_failed");
        expectChar('(');
        std::vector<std::string> params;
        skip();
        if (!takeChar(')')) {
            for (;;) {
                if (params.size() >= kMaxParams) throw Fail("compile_failed");
                params.push_back(ident());
                if (takeChar(')')) break;
                expectChar(',');
            }
        }
        expectChar('=');
        locals_ = params;
        std::vector<int64_t> code;
        compileExpr(prog, code);
        code.push_back(kOpRet);
        if (code.size() > kMaxCode) throw Fail("compile_failed");
        funcs_[name] = static_cast<uint32_t>(prog.funcs.size());
        Func fn;
        fn.name = std::move(name);
        fn.params = std::move(params);
        fn.code = std::move(code);
        prog.funcs.push_back(std::move(fn));
        locals_.clear();
    }

    void parseTest(Program& prog, bool assertTrue) {
        if (prog.tests.size() >= 64) throw Fail("compile_failed");
        locals_.clear();
        TestOp op;
        op.assertTrue = assertTrue;
        compileExpr(prog, op.code);
        if (assertTrue) op.code.push_back(kOpRet);
        else op.code.push_back(kOpEmit);
        if (op.code.size() > kMaxCode) throw Fail("compile_failed");
        prog.tests.push_back(std::move(op));
    }

    int localIndex(const std::string& name) const {
        for (size_t i = 0; i < locals_.size(); ++i) {
            if (locals_[i] == name) return static_cast<int>(i);
        }
        return -1;
    }

    void emit(std::vector<int64_t>& code, int64_t op) {
        if (code.size() >= kMaxCode) throw Fail("compile_failed");
        code.push_back(op);
    }

    void compileExpr(const Program& prog, std::vector<int64_t>& code) { compileOr(prog, code); }

    void compileOr(const Program& prog, std::vector<int64_t>& code) {
        compileAnd(prog, code);
        while (take("||") || takeChar('|')) {
            emit(code, kOpDup);
            emit(code, kOpJnz);
            size_t hole = code.size();
            emit(code, 0);
            emit(code, kOpPop);
            compileAnd(prog, code);
            code[hole] = static_cast<int64_t>(code.size());
        }
    }

    void compileAnd(const Program& prog, std::vector<int64_t>& code) {
        compileCmp(prog, code);
        while (take("&&") || takeChar('&')) {
            emit(code, kOpDup);
            emit(code, kOpJz);
            size_t hole = code.size();
            emit(code, 0);
            emit(code, kOpPop);
            compileCmp(prog, code);
            code[hole] = static_cast<int64_t>(code.size());
        }
    }

    void compileCmp(const Program& prog, std::vector<int64_t>& code) {
        compileAdd(prog, code);
        for (;;) {
            int64_t op = 0;
            if (take("==")) op = kOpEq;
            else if (take("!=")) op = kOpNe;
            else if (take("<=")) op = kOpLe;
            else if (take(">=")) op = kOpGe;
            else if (takeChar('<')) op = kOpLt;
            else if (takeChar('>')) op = kOpGt;
            else break;
            compileAdd(prog, code);
            emit(code, op);
        }
    }

    void compileAdd(const Program& prog, std::vector<int64_t>& code) {
        compileMul(prog, code);
        for (;;) {
            int64_t op = 0;
            if (takeChar('+')) op = kOpAdd;
            else if (takeChar('-')) op = kOpSub;
            else break;
            compileMul(prog, code);
            emit(code, op);
        }
    }

    void compileMul(const Program& prog, std::vector<int64_t>& code) {
        compileUnary(prog, code);
        for (;;) {
            int64_t op = 0;
            if (takeChar('*')) op = kOpMul;
            else if (takeChar('/')) op = kOpDiv;
            else if (takeChar('%')) op = kOpMod;
            else break;
            compileUnary(prog, code);
            emit(code, op);
        }
    }

    void compileUnary(const Program& prog, std::vector<int64_t>& code) {
        if (takeChar('!')) {
            compileUnary(prog, code);
            emit(code, kOpNot);
            return;
        }
        if (takeChar('-')) {
            compileUnary(prog, code);
            emit(code, kOpNeg);
            return;
        }
        compilePrimary(prog, code);
    }

    void compilePrimary(const Program& prog, std::vector<int64_t>& code) {
        skip();
        if (take("if")) {
            compileExpr(prog, code);
            emit(code, kOpJz);
            size_t elseHole = code.size();
            emit(code, 0);
            expect("then");
            compileExpr(prog, code);
            emit(code, kOpJmp);
            size_t endHole = code.size();
            emit(code, 0);
            code[elseHole] = static_cast<int64_t>(code.size());
            expect("else");
            compileExpr(prog, code);
            code[endHole] = static_cast<int64_t>(code.size());
            return;
        }
        if (takeChar('(')) {
            compileExpr(prog, code);
            expectChar(')');
            return;
        }
        if (!eof() && std::isdigit(static_cast<unsigned char>(text_[i_]))) {
            size_t start = i_;
            while (!eof() && std::isdigit(static_cast<unsigned char>(text_[i_]))) ++i_;
            std::string digits = text_.substr(start, i_ - start);
            errno = 0;
            char* end = nullptr;
            long long v = std::strtoll(digits.c_str(), &end, 10);
            if (errno != 0 || end == digits.c_str()) throw Fail("compile_failed");
            emit(code, kOpPush);
            emit(code, static_cast<int64_t>(v));
            return;
        }
        if (startsIdent()) {
            std::string name = ident();
            if (takeChar('(')) {
                auto it = funcs_.find(name);
                if (it == funcs_.end()) throw Fail("compile_failed");
                const Func& fn = prog.funcs[it->second];
                uint32_t argc = 0;
                skip();
                if (!takeChar(')')) {
                    for (;;) {
                        compileExpr(prog, code);
                        ++argc;
                        if (takeChar(')')) break;
                        expectChar(',');
                    }
                }
                if (argc != fn.params.size()) throw Fail("compile_failed");
                emit(code, kOpCall);
                emit(code, static_cast<int64_t>(it->second));
                return;
            }
            int idx = localIndex(name);
            if (idx < 0) throw Fail("compile_failed");
            emit(code, kOpLoad);
            emit(code, idx);
            return;
        }
        throw Fail("compile_failed");
    }
};

bool addOverflow(int64_t a, int64_t b, int64_t* out) {
    if ((b > 0 && a > std::numeric_limits<int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<int64_t>::min() - b)) {
        return true;
    }
    *out = a + b;
    return false;
}

bool subOverflow(int64_t a, int64_t b, int64_t* out) {
    if ((b < 0 && a > std::numeric_limits<int64_t>::max() + b) ||
        (b > 0 && a < std::numeric_limits<int64_t>::min() + b)) {
        return true;
    }
    *out = a - b;
    return false;
}

bool mulOverflow(int64_t a, int64_t b, int64_t* out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return false;
    }
    if (a == std::numeric_limits<int64_t>::min() && b == -1) return true;
    if (b == std::numeric_limits<int64_t>::min() && a == -1) return true;
    if (a > 0 && b > 0 && a > std::numeric_limits<int64_t>::max() / b) return true;
    if (a < 0 && b < 0 && a < std::numeric_limits<int64_t>::max() / b) return true;
    if (a > 0 && b < 0 && b < std::numeric_limits<int64_t>::min() / a) return true;
    if (a < 0 && b > 0 && a < std::numeric_limits<int64_t>::min() / b) return true;
    *out = a * b;
    return false;
}

struct VmFail : std::runtime_error {
    using std::runtime_error::runtime_error;
};

int64_t runCode(const Program& prog, const std::vector<int64_t>& code, bool emitMode,
                std::string* output, uint32_t* steps) {
    struct Frame {
        const std::vector<int64_t>* code = nullptr;
        size_t ip = 0;
        std::vector<int64_t> locals;
    };
    std::vector<Frame> stack;
    std::vector<int64_t> values;
    Frame root;
    root.code = &code;
    stack.push_back(root);

    auto pop = [&]() -> int64_t {
        if (values.empty()) throw VmFail("compile_failed");
        int64_t v = values.back();
        values.pop_back();
        return v;
    };

    while (!stack.empty()) {
        Frame& fr = stack.back();
        if (fr.ip >= fr.code->size()) throw VmFail("compile_failed");
        if (*steps >= kMaxSteps) throw VmFail("step_limit");
        *steps += 1;
        int64_t op = (*fr.code)[fr.ip++];
        auto operand = [&]() -> int64_t {
            if (fr.ip >= fr.code->size()) throw VmFail("compile_failed");
            return (*fr.code)[fr.ip++];
        };
        switch (op) {
        case kOpPush:
            values.push_back(operand());
            break;
        case kOpLoad: {
            int64_t idx = operand();
            if (idx < 0 || static_cast<size_t>(idx) >= fr.locals.size()) throw VmFail("compile_failed");
            values.push_back(fr.locals[static_cast<size_t>(idx)]);
            break;
        }
        case kOpDup:
            if (values.empty()) throw VmFail("compile_failed");
            values.push_back(values.back());
            break;
        case kOpPop:
            pop();
            break;
        case kOpAdd:
        case kOpSub:
        case kOpMul:
        case kOpDiv:
        case kOpMod:
        case kOpEq:
        case kOpNe:
        case kOpLt:
        case kOpLe:
        case kOpGt:
        case kOpGe: {
            int64_t b = pop();
            int64_t a = pop();
            int64_t r = 0;
            if (op == kOpAdd) {
                if (addOverflow(a, b, &r)) throw VmFail("overflow");
            } else if (op == kOpSub) {
                if (subOverflow(a, b, &r)) throw VmFail("overflow");
            } else if (op == kOpMul) {
                if (mulOverflow(a, b, &r)) throw VmFail("overflow");
            } else if (op == kOpDiv || op == kOpMod) {
                if (b == 0) throw VmFail("divide_by_zero");
                if (a == std::numeric_limits<int64_t>::min() && b == -1) throw VmFail("overflow");
                r = (op == kOpDiv) ? (a / b) : (a % b);
            } else if (op == kOpEq) r = a == b ? 1 : 0;
            else if (op == kOpNe) r = a != b ? 1 : 0;
            else if (op == kOpLt) r = a < b ? 1 : 0;
            else if (op == kOpLe) r = a <= b ? 1 : 0;
            else if (op == kOpGt) r = a > b ? 1 : 0;
            else r = a >= b ? 1 : 0;
            values.push_back(r);
            break;
        }
        case kOpNeg: {
            int64_t a = pop();
            if (a == std::numeric_limits<int64_t>::min()) throw VmFail("overflow");
            values.push_back(-a);
            break;
        }
        case kOpNot:
            values.push_back(pop() == 0 ? 1 : 0);
            break;
        case kOpJmp:
            fr.ip = static_cast<size_t>(operand());
            break;
        case kOpJz: {
            size_t dest = static_cast<size_t>(operand());
            if (pop() == 0) fr.ip = dest;
            break;
        }
        case kOpJnz: {
            size_t dest = static_cast<size_t>(operand());
            if (pop() != 0) fr.ip = dest;
            break;
        }
        case kOpCall: {
            int64_t idx = operand();
            if (idx < 0 || static_cast<size_t>(idx) >= prog.funcs.size()) throw VmFail("compile_failed");
            if (stack.size() >= kMaxDepth) throw VmFail("step_limit");
            const Func& fn = prog.funcs[static_cast<size_t>(idx)];
            if (values.size() < fn.params.size()) throw VmFail("compile_failed");
            Frame next;
            next.code = &fn.code;
            next.locals.resize(fn.params.size());
            for (size_t n = fn.params.size(); n > 0; --n) {
                next.locals[n - 1] = pop();
            }
            stack.push_back(std::move(next));
            break;
        }
        case kOpRet: {
            int64_t r = pop();
            const bool root = stack.size() == 1 && stack.back().code == &code;
            stack.pop_back();
            values.push_back(r);
            if (root) return r;
            break;
        }
        case kOpEmit: {
            if (!emitMode) throw VmFail("compile_failed");
            int64_t r = pop();
            std::string line = std::to_string(r) + "\n";
            if (output->size() + line.size() > kMaxOutput) throw VmFail("output_limit");
            output->append(line);
            stack.pop_back();
            return r;
        }
        default:
            throw VmFail("compile_failed");
        }
        if (stack.empty()) break;
    }
    throw VmFail("compile_failed");
}

WorkExecution finish(bool passed, const std::string& reason, uint32_t asserts, uint32_t steps,
                     const std::string& output) {
    WorkExecution out;
    out.passed = passed;
    out.reason = reason;
    out.asserts = asserts;
    out.steps = steps;
    std::ostringstream transcript;
    transcript << "status=" << (passed ? "pass" : "fail") << "\n"
               << "reason=" << reason << "\n"
               << "asserts=" << asserts << "\n"
               << "steps=" << steps << "\n"
               << "output=" << output;
    out.transcript = transcript.str();
    out.resultHash = crypto::sha256(out.transcript);
    return out;
}

bool splitBundle(const std::string& bundle, std::string* patch, std::string* tests) {
    const std::string header = "SYNAPSE_WORK_V1\n";
    const std::string patchMark = "--- patch\n";
    const std::string testMark = "--- tests\n";
    if (bundle.compare(0, header.size(), header) != 0) return false;
    size_t p = bundle.find(patchMark);
    size_t t = bundle.find(testMark);
    if (p == std::string::npos || t == std::string::npos || t < p) return false;
    size_t patchStart = p + patchMark.size();
    if (patchStart > t) return false;
    *patch = bundle.substr(patchStart, t - patchStart);
    *tests = bundle.substr(t + testMark.size());
    return true;
}

uint16_t hashSlice(const crypto::Hash256& hash, size_t byte) {
    return static_cast<uint16_t>((static_cast<uint16_t>(hash[byte]) << 8) |
                                 static_cast<uint16_t>(hash[byte + 1]));
}

} // namespace

std::string workBundle(const std::string& patch, const std::string& tests) {
    std::string body = "SYNAPSE_WORK_V1\n--- patch\n";
    body += patch;
    if (body.empty() || body.back() != '\n') body.push_back('\n');
    body += "--- tests\n";
    body += tests;
    if (body.empty() || body.back() != '\n') body.push_back('\n');
    return body;
}

WorkExecution executeWork(const std::string& bundle) {
    SandboxPolicy policy;
    policy.allowlist = {AgentCapability::PROPOSE_KNOWLEDGE};
    policy.allowSideEffects = false;
    AgentRuntimeSandbox sandbox(policy);
    return executeWork(bundle, sandbox);
}

WorkExecution executeWork(const std::string& bundle, const AgentRuntimeSandbox& sandbox) {
    if (sandbox.authorize(AgentCapability::PROPOSE_KNOWLEDGE, false, false) != RuntimeActionResult::ALLOWED) {
        return finish(false, "sandbox_denied", 0, 0, "");
    }
    if (sandbox.authorize(AgentCapability::PROPOSE_KNOWLEDGE, true, true) == RuntimeActionResult::ALLOWED) {
        return finish(false, "sandbox_denied", 0, 0, "");
    }
    if (bundle.size() > kMaxBundle) return finish(false, "work_bundle_required", 0, 0, "");

    std::string patch;
    std::string tests;
    if (!splitBundle(bundle, &patch, &tests)) return finish(false, "work_bundle_required", 0, 0, "");

    Program prog;
    try {
        Parser parser(patch + "\n" + tests);
        prog = parser.parse();
    } catch (const Fail& fail) {
        return finish(false, fail.what(), 0, 0, "");
    }

    uint32_t steps = 0;
    uint32_t asserts = 0;
    std::string output;
    try {
        for (const auto& test : prog.tests) {
            int64_t value = runCode(prog, test.code, !test.assertTrue, &output, &steps);
            if (test.assertTrue) {
                ++asserts;
                if (value == 0) return finish(false, "work_tests_failed", asserts, steps, output);
            }
        }
    } catch (const VmFail& fail) {
        return finish(false, fail.what(), asserts, steps, output);
    } catch (const Fail& fail) {
        return finish(false, fail.what(), asserts, steps, output);
    }
    return finish(true, "ok", asserts, steps, output);
}

std::string workVoteNote(const crypto::Hash256& resultHash) {
    return "powc:v1:" + crypto::toHex(resultHash);
}

bool parseWorkVoteNote(const std::string& note, crypto::Hash256* out) {
    const std::string prefix = "powc:v1:";
    if (!out) return false;
    if (note.size() != prefix.size() + 64) return false;
    if (note.compare(0, prefix.size(), prefix) != 0) return false;
    auto bytes = crypto::fromHex(note.substr(prefix.size()));
    if (bytes.size() != crypto::SHA256_SIZE) return false;
    std::memcpy(out->data(), bytes.data(), out->size());
    return true;
}

void bindWorkVote(poe_v1::ValidationVoteV1& vote, const crypto::Hash256& resultHash) {
    vote.note = workVoteNote(resultHash);
    vote.scores[0] = hashSlice(resultHash, 0);
    vote.scores[1] = hashSlice(resultHash, 2);
    vote.scores[2] = hashSlice(resultHash, 4);
}

bool workVoteMatches(const poe_v1::ValidationVoteV1& vote, const crypto::Hash256& resultHash) {
    crypto::Hash256 parsed{};
    if (!parseWorkVoteNote(vote.note, &parsed)) return false;
    if (parsed != resultHash) return false;
    return vote.scores[0] == hashSlice(resultHash, 0) &&
           vote.scores[1] == hashSlice(resultHash, 2) &&
           vote.scores[2] == hashSlice(resultHash, 4);
}

} // namespace synapse::core
