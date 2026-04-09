#include "web/connector_audit.h"
#include "crypto/crypto.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::string readFileText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    assert(in.good());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text;
}

static std::string readHexField(const std::string& text, const std::string& key) {
    const std::string token = key + "=";
    const size_t pos = text.find(token);
    assert(pos != std::string::npos);
    const size_t start = pos + token.size();
    const size_t end = text.find('\n', start);
    assert(end != std::string::npos);
    return text.substr(start, end - start);
}

static std::string decodeHex(const std::string& hex) {
    auto bytes = synapse::crypto::fromHex(hex);
    return std::string(bytes.begin(), bytes.end());
}

static void testConnectorAuditWritesContentAddressedArtifacts() {
    const auto uniq = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path root = fs::temp_directory_path() / ("synapsenet_connector_audit_" + uniq);

    synapse::web::ExtractedContent extracted;
    extracted.title = "Example";
    extracted.mainText = "Deterministic connector audit payload.";
    extracted.codeBlocks = {"int x = 1;"};
    extracted.riskScore = 12;
    extracted.riskBlocked = false;
    extracted.riskSignals = {};
    extracted.extractedSize = extracted.title.size() + extracted.mainText.size() + extracted.codeBlocks[0].size();

    const std::string body = "<html><body><h1>Example</h1><p>Deterministic connector audit payload.</p></body></html>";
    auto first = synapse::web::writeConnectorAuditArtifact(
        root.string(),
        "https://example.org/test",
        body,
        extracted,
        100
    );

    assert(first.ok);
    assert(!first.contentHashHex.empty());
    assert(!first.extractHashHex.empty());

    const fs::path fetchPath = root / (first.contentHashHex + ".fetch.bin");
    const fs::path extractPath = root / (first.contentHashHex + ".extract.audit");
    assert(fs::exists(fetchPath));
    assert(fs::exists(extractPath));

    auto second = synapse::web::writeConnectorAuditArtifact(
        root.string(),
        "https://example.org/test",
        body,
        extracted,
        101
    );
    assert(second.ok);
    assert(second.contentHashHex == first.contentHashHex);

    std::error_code ec;
    fs::remove_all(root, ec);
}

static void testConnectorAuditRedactsPotentialSecrets() {
    const auto uniq = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path root = fs::temp_directory_path() / ("synapsenet_connector_audit_redaction_" + uniq);

    synapse::web::ExtractedContent extracted;
    extracted.title = "private key=abcd1234";
    extracted.mainText = "token=my_secret_token password=very_secret_password";
    extracted.codeBlocks = {"authorization=BearerValue"};
    extracted.metadata["auth"] = "api_key=synapse-token";
    extracted.metadata["pwd"] = "password=abc123";
    extracted.extractedSize = extracted.title.size() + extracted.mainText.size() + extracted.codeBlocks[0].size();

    const std::string sourceUrl = "https://example.org/page?api_key=source-secret";
    const std::string body = "<html><body><p>public page</p></body></html>";
    auto write = synapse::web::writeConnectorAuditArtifact(
        root.string(),
        sourceUrl,
        body,
        extracted,
        200
    );

    assert(write.ok);
    const fs::path extractPath = root / (write.contentHashHex + ".extract.audit");
    assert(fs::exists(extractPath));

    const std::string payload = readFileText(extractPath);
    const std::string source = decodeHex(readHexField(payload, "source_url_hex"));
    const std::string title = decodeHex(readHexField(payload, "title_hex"));
    const std::string mainText = decodeHex(readHexField(payload, "main_text_hex"));
    const std::string codeBlock = decodeHex(readHexField(payload, "code_block_0_hex"));
    const std::string metadataValue = decodeHex(readHexField(payload, "metadata_0_value_hex"));
    const std::string metadataValue2 = decodeHex(readHexField(payload, "metadata_1_value_hex"));

    assert(source.find("source-secret") == std::string::npos);
    assert(title.find("abcd1234") == std::string::npos);
    assert(mainText.find("my_secret_token") == std::string::npos);
    assert(mainText.find("very_secret_password") == std::string::npos);
    assert(codeBlock.find("BearerValue") == std::string::npos);
    assert(metadataValue.find("synapse-token") == std::string::npos);
    assert(metadataValue2.find("abc123") == std::string::npos);

    assert(source.find("[REDACTED]") != std::string::npos);
    assert(title.find("[REDACTED]") != std::string::npos);
    assert(mainText.find("[REDACTED]") != std::string::npos);
    assert(codeBlock.find("[REDACTED]") != std::string::npos);
    assert(metadataValue.find("[REDACTED]") != std::string::npos);
    assert(metadataValue2.find("[REDACTED]") != std::string::npos);

    std::error_code ec;
    fs::remove_all(root, ec);
}

int main() {
    testConnectorAuditWritesContentAddressedArtifacts();
    testConnectorAuditRedactsPotentialSecrets();
    return 0;
}
