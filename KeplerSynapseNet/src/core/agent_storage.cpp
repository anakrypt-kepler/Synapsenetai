#include "core/agent_storage.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace synapse::core {

namespace {

bool parseU64Strict(const std::string& token, uint64_t* out) {
    if (!out || token.empty()) return false;
    uint64_t value = 0;
    for (char c : token) {
        if (c < '0' || c > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10ULL) return false;
        value = value * 10ULL + digit;
    }
    *out = value;
    return true;
}

bool parseHexNibble(char c, uint8_t* out) {
    if (!out) return false;
    if (c >= '0' && c <= '9') {
        *out = static_cast<uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = static_cast<uint8_t>(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *out = static_cast<uint8_t>(c - 'A' + 10);
        return true;
    }
    return false;
}

bool parseHexBytesStrict(const std::string& hex, std::vector<uint8_t>* out) {
    if (!out) return false;
    if ((hex.size() % 2) != 0) return false;

    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!parseHexNibble(hex[i], &hi) || !parseHexNibble(hex[i + 1], &lo)) {
            return false;
        }
        bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    *out = std::move(bytes);
    return true;
}

std::string toHexString(const std::string& value) {
    return crypto::toHex(reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

bool fromHexString(const std::string& hex, std::string* out) {
    if (!out) return false;
    std::vector<uint8_t> bytes;
    if (!parseHexBytesStrict(hex, &bytes)) return false;
    out->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool parseHash256(const std::string& hex, crypto::Hash256* out) {
    if (!out) return false;
    if (hex.size() != 64) return false;

    std::vector<uint8_t> bytes;
    if (!parseHexBytesStrict(hex, &bytes)) return false;
    if (bytes.size() != out->size()) return false;
    std::copy(bytes.begin(), bytes.end(), out->begin());
    return true;
}

std::vector<std::string> splitPipe(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t pos = line.find('|', start);
        if (pos == std::string::npos) {
            out.push_back(line.substr(start));
            break;
        }
        out.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

std::string canonicalLineWithoutHash(uint64_t sequence,
                                     uint64_t timestamp,
                                     const std::string& kindHex,
                                     const std::string& objectHex,
                                     const std::string& payloadHex,
                                     const std::string& prevHex) {
    return "v1|" + std::to_string(sequence) +
           "|" + std::to_string(timestamp) +
           "|" + kindHex +
           "|" + objectHex +
           "|" + payloadHex +
           "|" + prevHex;
}

crypto::Hash256 hashCanonicalLine(const std::string& canonical) {
    return crypto::sha256(reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size());
}

std::string segmentFileName(uint64_t index) {
    std::ostringstream ss;
    ss << "segment_" << std::setw(6) << std::setfill('0') << index << ".log";
    return ss.str();
}

bool parseSegmentIndex(const std::string& fileName, uint64_t* out) {
    if (!out) return false;
    const std::string prefix = "segment_";
    const std::string suffix = ".log";
    if (fileName.size() <= prefix.size() + suffix.size()) return false;
    if (fileName.rfind(prefix, 0) != 0) return false;
    if (fileName.compare(fileName.size() - suffix.size(), suffix.size(), suffix) != 0) return false;

    const std::string token = fileName.substr(prefix.size(), fileName.size() - prefix.size() - suffix.size());
    return parseU64Strict(token, out);
}

bool writeLinesAtomically(const std::string& path,
                          const std::vector<std::string>& lines,
                          std::string* reason) {
    const std::string tmpPath = path + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::trunc);
        if (!out.good()) {
            if (reason) *reason = "open_tmp_failed";
            return false;
        }
        for (const auto& line : lines) {
            out << line << "\n";
            if (!out.good()) {
                if (reason) *reason = "write_tmp_failed";
                return false;
            }
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::filesystem::remove(tmpPath, ec);
        if (reason) *reason = "rename_failed";
        return false;
    }
    return true;
}

bool parseEventLine(const std::string& line,
                    AgentStorageAuditEvent* out,
                    std::string* reason) {
    if (!out) {
        if (reason) *reason = "null_output";
        return false;
    }

    const auto parts = splitPipe(line);
    if (parts.size() != 8) {
        if (reason) *reason = "invalid_field_count";
        return false;
    }
    if (parts[0] != "v1") {
        if (reason) *reason = "invalid_version";
        return false;
    }

    uint64_t sequence = 0;
    uint64_t timestamp = 0;
    if (!parseU64Strict(parts[1], &sequence) || !parseU64Strict(parts[2], &timestamp)) {
        if (reason) *reason = "invalid_numbers";
        return false;
    }

    std::string kind;
    std::string objectId;
    std::string payload;
    if (!fromHexString(parts[3], &kind) ||
        !fromHexString(parts[4], &objectId) ||
        !fromHexString(parts[5], &payload)) {
        if (reason) *reason = "invalid_hex_fields";
        return false;
    }

    crypto::Hash256 prevHash{};
    crypto::Hash256 hash{};
    if (!parseHash256(parts[6], &prevHash) || !parseHash256(parts[7], &hash)) {
        if (reason) *reason = "invalid_hash_fields";
        return false;
    }

    const std::string canonical = canonicalLineWithoutHash(
        sequence, timestamp, parts[3], parts[4], parts[5], parts[6]);
    const auto expectedHash = hashCanonicalLine(canonical);
    if (expectedHash != hash) {
        if (reason) *reason = "hash_mismatch";
        return false;
    }

    out->sequence = sequence;
    out->timestamp = timestamp;
    out->kind = std::move(kind);
    out->objectId = std::move(objectId);
    out->payload = std::move(payload);
    out->prevHash = prevHash;
    out->hash = hash;
    if (reason) *reason = "ok";
    return true;
}

} // namespace

AgentStorageAuditLog::AgentStorageAuditLog(const AgentStorageAuditPolicy& policy)
    : policy_(policy) {
    if (policy_.maxSegments == 0) policy_.maxSegments = 1;
    if (policy_.maxSegmentBytes < 1024) policy_.maxSegmentBytes = 1024;
}

AgentStorageAuditPolicy AgentStorageAuditLog::policy() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return policy_;
}

void AgentStorageAuditLog::setPolicy(const AgentStorageAuditPolicy& policy) {
    std::lock_guard<std::mutex> lock(mtx_);
    policy_ = policy;
    if (policy_.maxSegments == 0) policy_.maxSegments = 1;
    if (policy_.maxSegmentBytes < 1024) policy_.maxSegmentBytes = 1024;
    if (opened_) {
        enforceRetentionLocked();
    }
}

bool AgentStorageAuditLog::open(const std::string& rootDir, std::string* reason) {
    std::lock_guard<std::mutex> lock(mtx_);

    rootDir_ = rootDir;
    auditDir_ = rootDir_ + "/audit";
    segments_.clear();
    retainedEvents_ = 0;
    lastSequence_ = 0;
    lastHash_ = crypto::Hash256{};
    recoveredTruncatedLines_ = 0;
    droppedSegments_ = 0;
    opened_ = false;

    std::error_code ec;
    std::filesystem::create_directories(auditDir_, ec);
    if (ec) {
        if (reason) *reason = "mkdir_failed";
        return false;
    }

    std::vector<std::pair<uint64_t, std::string>> files;
    for (const auto& entry : std::filesystem::directory_iterator(auditDir_)) {
        if (!entry.is_regular_file()) continue;
        uint64_t index = 0;
        const std::string fileName = entry.path().filename().string();
        if (!parseSegmentIndex(fileName, &index)) continue;
        files.emplace_back(index, entry.path().string());
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    if (files.empty()) {
        const std::string firstPath = (std::filesystem::path(auditDir_) / segmentFileName(1)).string();
        std::ofstream seed(firstPath, std::ios::app);
        if (!seed.good()) {
            if (reason) *reason = "open_seed_failed";
            return false;
        }
        SegmentState seg;
        seg.index = 1;
        seg.path = firstPath;
        seg.bytes = 0;
        seg.events = 0;
        segments_.push_back(seg);
        opened_ = true;
        if (reason) *reason = "ok";
        return true;
    }

    struct ParsedSegment {
        uint64_t index = 0;
        std::string path;
        std::vector<std::string> validLines;
    };

    std::vector<ParsedSegment> parsed;
    parsed.reserve(files.size());

    bool corruptionDetected = false;
    size_t corruptionSegmentPos = 0;
    std::string corruptionReason;
    bool haveEvents = false;

    for (size_t i = 0; i < files.size(); ++i) {
        ParsedSegment seg;
        seg.index = files[i].first;
        seg.path = files[i].second;

        std::ifstream in(seg.path);
        if (!in.good()) {
            if (reason) *reason = "open_segment_failed";
            return false;
        }

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;

            AgentStorageAuditEvent event;
            std::string parseReason;
            if (!parseEventLine(line, &event, &parseReason)) {
                corruptionDetected = true;
                corruptionSegmentPos = i;
                corruptionReason = parseReason;
                break;
            }

            if (!haveEvents) {
                haveEvents = true;
            } else {
                if (event.sequence != lastSequence_ + 1 || event.prevHash != lastHash_) {
                    corruptionDetected = true;
                    corruptionSegmentPos = i;
                    corruptionReason = "chain_break";
                    break;
                }
            }

            lastSequence_ = event.sequence;
            lastHash_ = event.hash;
            retainedEvents_ += 1;
            seg.validLines.push_back(line);
        }

        parsed.push_back(std::move(seg));
        if (corruptionDetected) break;
    }

    bool repaired = false;
    if (corruptionDetected) {
        recoveredTruncatedLines_ += 1;
        repaired = true;

        if (corruptionSegmentPos < parsed.size()) {
            std::string rewriteReason;
            if (!writeLinesAtomically(parsed[corruptionSegmentPos].path,
                                      parsed[corruptionSegmentPos].validLines,
                                      &rewriteReason)) {
                if (reason) *reason = "repair_" + rewriteReason;
                return false;
            }
        }

        for (size_t i = corruptionSegmentPos + 1; i < files.size(); ++i) {
            std::error_code rmEc;
            std::filesystem::remove(files[i].second, rmEc);
            if (!rmEc) droppedSegments_ += 1;
        }

        if (parsed.size() > corruptionSegmentPos + 1) {
            parsed.resize(corruptionSegmentPos + 1);
        }

        if (!parsed.empty() && parsed.back().validLines.empty()) {
            if (parsed.size() > 1) {
                std::error_code rmEc;
                std::filesystem::remove(parsed.back().path, rmEc);
                if (!rmEc) droppedSegments_ += 1;
                parsed.pop_back();
            }
        }
    }

    for (const auto& seg : parsed) {
        SegmentState state;
        state.index = seg.index;
        state.path = seg.path;
        std::error_code szEc;
        state.bytes = std::filesystem::file_size(seg.path, szEc);
        if (szEc) state.bytes = 0;
        state.events = static_cast<uint64_t>(seg.validLines.size());
        segments_.push_back(state);
    }

    if (segments_.empty()) {
        uint64_t seedIndex = files.back().first;
        if (seedIndex == 0) seedIndex = 1;
        const std::string seedPath = (std::filesystem::path(auditDir_) / segmentFileName(seedIndex)).string();
        std::ofstream seed(seedPath, std::ios::app);
        if (!seed.good()) {
            if (reason) *reason = "open_seed_failed";
            return false;
        }
        SegmentState seg;
        seg.index = seedIndex;
        seg.path = seedPath;
        segments_.push_back(seg);
    }

    enforceRetentionLocked();
    opened_ = true;

    if (reason) {
        if (repaired) {
            *reason = "repaired_" + corruptionReason;
        } else {
            *reason = "ok";
        }
    }
    return true;
}

bool AgentStorageAuditLog::appendLocked(uint64_t atTimestamp,
                                        const std::string& kind,
                                        const std::string& objectId,
                                        const std::string& payload,
                                        bool allowRotate,
                                        std::string* reason) {
    if (segments_.empty()) {
        if (reason) *reason = "no_segment";
        return false;
    }
    if (kind.empty()) {
        if (reason) *reason = "empty_kind";
        return false;
    }

    const std::string kindHex = toHexString(kind);
    const std::string objectHex = toHexString(objectId);
    const std::string payloadHex = toHexString(payload);
    const std::string prevHex = crypto::toHex(lastHash_);
    const uint64_t nextSequence = lastSequence_ + 1;

    const std::string canonical = canonicalLineWithoutHash(
        nextSequence, atTimestamp, kindHex, objectHex, payloadHex, prevHex);
    const auto hash = hashCanonicalLine(canonical);
    const std::string line = canonical + "|" + crypto::toHex(hash);

    std::ofstream out(segments_.back().path, std::ios::app);
    if (!out.good()) {
        if (reason) *reason = "open_append_failed";
        return false;
    }
    out << line << "\n";
    if (!out.good()) {
        if (reason) *reason = "write_append_failed";
        return false;
    }

    lastSequence_ = nextSequence;
    lastHash_ = hash;
    retainedEvents_ += 1;
    segments_.back().events += 1;
    segments_.back().bytes += static_cast<uint64_t>(line.size() + 1);

    if (allowRotate && policy_.maxSegmentBytes > 0 &&
        segments_.back().bytes > policy_.maxSegmentBytes) {
        if (!rotateLocked(atTimestamp, reason)) return false;
    }

    if (reason) *reason = "ok";
    return true;
}

bool AgentStorageAuditLog::rotateLocked(uint64_t atTimestamp, std::string* reason) {
    if (segments_.empty()) {
        if (reason) *reason = "no_segment";
        return false;
    }

    const SegmentState previous = segments_.back();
    const uint64_t nextIndex = previous.index + 1;
    const std::string nextPath = (std::filesystem::path(auditDir_) / segmentFileName(nextIndex)).string();
    {
        std::ofstream out(nextPath, std::ios::app);
        if (!out.good()) {
            if (reason) *reason = "open_rotate_failed";
            return false;
        }
    }

    SegmentState next;
    next.index = nextIndex;
    next.path = nextPath;
    next.bytes = 0;
    next.events = 0;
    segments_.push_back(next);

    const std::string checkpointPayload =
        "from_segment=" + std::to_string(previous.index) +
        ",tail_seq=" + std::to_string(lastSequence_) +
        ",tail_hash=" + crypto::toHex(lastHash_);
    std::string appendReason;
    if (!appendLocked(atTimestamp,
                      "segment_checkpoint",
                      std::to_string(previous.index),
                      checkpointPayload,
                      false,
                      &appendReason)) {
        std::error_code ec;
        std::filesystem::remove(nextPath, ec);
        segments_.pop_back();
        if (reason) *reason = "rotate_" + appendReason;
        return false;
    }

    enforceRetentionLocked();
    if (reason) *reason = "ok";
    return true;
}

void AgentStorageAuditLog::enforceRetentionLocked() {
    const uint32_t maxSegments = policy_.maxSegments == 0 ? 1 : policy_.maxSegments;
    while (segments_.size() > maxSegments) {
        SegmentState drop = segments_.front();
        std::error_code ec;
        std::filesystem::remove(drop.path, ec);
        segments_.erase(segments_.begin());
        if (retainedEvents_ >= drop.events) {
            retainedEvents_ -= drop.events;
        } else {
            retainedEvents_ = 0;
        }
        droppedSegments_ += 1;
    }
}

bool AgentStorageAuditLog::append(uint64_t atTimestamp,
                                  const std::string& kind,
                                  const std::string& objectId,
                                  const std::string& payload,
                                  std::string* reason) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!opened_) {
        if (reason) *reason = "not_open";
        return false;
    }
    return appendLocked(atTimestamp, kind, objectId, payload, true, reason);
}

AgentStorageAuditStats AgentStorageAuditLog::stats() const {
    std::lock_guard<std::mutex> lock(mtx_);
    AgentStorageAuditStats out;
    out.rootDir = rootDir_;
    out.segmentCount = static_cast<uint64_t>(segments_.size());
    out.retainedEvents = retainedEvents_;
    out.lastSequence = lastSequence_;
    out.lastHash = lastHash_;
    out.recoveredTruncatedLines = recoveredTruncatedLines_;
    out.droppedSegments = droppedSegments_;
    return out;
}

} // namespace synapse::core
