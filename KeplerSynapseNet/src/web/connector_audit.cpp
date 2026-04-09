#include "web/connector_audit.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "crypto/crypto.h"
#include "utils/config.h"

namespace synapse {
namespace web {

namespace {

static std::string redactPotentialSecrets(const std::string& input) {
    std::string out = input;
    static const std::regex pattern(
        "(api[_-]?key|token|authorization|bearer|private[ _-]?key|mnemonic|seed[ _-]?phrase|secret|password)\\s*[:=]\\s*[^\\s\\\"'<>]+",
        std::regex::icase
    );
    out = std::regex_replace(out, pattern, "$1=[REDACTED]");
    return out;
}

static std::string toHexString(const std::string& value) {
    return crypto::toHex(reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

static void appendHexField(std::ostringstream& ss, const std::string& key, const std::string& value) {
    ss << key << "=" << toHexString(value) << "\n";
}

static bool writeBytes(const std::filesystem::path& path,
                       const char* data,
                       size_t size,
                       std::string* reason) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        if (reason) *reason = "open_failed";
        return false;
    }
    if (size > 0) out.write(data, static_cast<std::streamsize>(size));
    if (!out.good()) {
        if (reason) *reason = "write_failed";
        return false;
    }
    return true;
}

static bool writeText(const std::filesystem::path& path,
                      const std::string& text,
                      std::string* reason) {
    return writeBytes(path, text.data(), text.size(), reason);
}

}

std::string defaultConnectorAuditDir() {
    std::string base = utils::Config::instance().getDataDir();
    if (base.empty()) base = ".synapsenet";
    return base + "/audit/connectors";
}

ConnectorAuditWriteResult writeConnectorAuditArtifact(const std::string& auditDir,
                                                      const std::string& sourceUrl,
                                                      const std::string& fetchedBody,
                                                      const ExtractedContent& extracted,
                                                      uint64_t atTimestamp) {
    ConnectorAuditWriteResult out;
    if (fetchedBody.empty()) {
        out.reason = "empty_body";
        return out;
    }

    std::string root = auditDir.empty() ? defaultConnectorAuditDir() : auditDir;
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) {
        out.reason = "mkdir_failed";
        return out;
    }

    const crypto::Hash256 bodyHash = crypto::sha256(
        reinterpret_cast<const uint8_t*>(fetchedBody.data()),
        fetchedBody.size()
    );
    out.contentHashHex = crypto::toHex(bodyHash);

    const std::filesystem::path fetchPath = std::filesystem::path(root) / (out.contentHashHex + ".fetch.bin");
    if (!std::filesystem::exists(fetchPath) &&
        !writeBytes(fetchPath, fetchedBody.data(), fetchedBody.size(), &out.reason)) {
        return out;
    }

    std::ostringstream payload;
    payload << "version=v1\n";
    payload << "timestamp=" << atTimestamp << "\n";
    appendHexField(payload, "source_url_hex", redactPotentialSecrets(sourceUrl));
    payload << "content_hash=" << out.contentHashHex << "\n";
    appendHexField(payload, "fetch_file_hex", fetchPath.filename().string());
    payload << "fetched_bytes=" << fetchedBody.size() << "\n";
    payload << "extracted_bytes=" << extracted.extractedSize << "\n";
    payload << "risk_score=" << extracted.riskScore << "\n";
    payload << "risk_blocked=" << (extracted.riskBlocked ? 1 : 0) << "\n";

    payload << "risk_signal_count=" << extracted.riskSignals.size() << "\n";
    for (size_t i = 0; i < extracted.riskSignals.size(); ++i) {
        appendHexField(payload, "risk_signal_" + std::to_string(i) + "_hex", extracted.riskSignals[i]);
    }

    appendHexField(payload, "title_hex", redactPotentialSecrets(extracted.title));
    appendHexField(payload, "main_text_hex", redactPotentialSecrets(extracted.mainText));

    payload << "code_block_count=" << extracted.codeBlocks.size() << "\n";
    for (size_t i = 0; i < extracted.codeBlocks.size(); ++i) {
        appendHexField(payload,
                       "code_block_" + std::to_string(i) + "_hex",
                       redactPotentialSecrets(extracted.codeBlocks[i]));
    }

    payload << "onion_link_count=" << extracted.onionLinks.size() << "\n";
    for (size_t i = 0; i < extracted.onionLinks.size(); ++i) {
        appendHexField(payload, "onion_link_" + std::to_string(i) + "_hex", extracted.onionLinks[i]);
    }

    payload << "clearnet_link_count=" << extracted.clearnetLinks.size() << "\n";
    for (size_t i = 0; i < extracted.clearnetLinks.size(); ++i) {
        appendHexField(payload, "clearnet_link_" + std::to_string(i) + "_hex", extracted.clearnetLinks[i]);
    }

    payload << "metadata_count=" << extracted.metadata.size() << "\n";
    size_t mdIndex = 0;
    for (const auto& [k, v] : extracted.metadata) {
        appendHexField(payload, "metadata_" + std::to_string(mdIndex) + "_key_hex", k);
        appendHexField(payload,
                       "metadata_" + std::to_string(mdIndex) + "_value_hex",
                       redactPotentialSecrets(v));
        ++mdIndex;
    }

    const std::string payloadNoHash = payload.str();
    const crypto::Hash256 extractHash = crypto::sha256(payloadNoHash);
    out.extractHashHex = crypto::toHex(extractHash);
    std::string payloadWithHash = payloadNoHash;
    payloadWithHash += "extract_hash=" + out.extractHashHex + "\n";

    const std::filesystem::path extractPath = std::filesystem::path(root) / (out.contentHashHex + ".extract.audit");
    if (!writeText(extractPath, payloadWithHash, &out.reason)) {
        return out;
    }

    out.ok = true;
    out.reason = "ok";
    return out;
}

}
}
