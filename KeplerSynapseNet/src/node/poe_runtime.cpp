#include "node/poe_runtime.h"

#include <stdexcept>
#include <system_error>
#include <vector>

namespace synapse::node {

namespace {

std::filesystem::path resolvePoeDbFile(const std::filesystem::path& target) {
    const bool targetIsDir = std::filesystem::exists(target)
        ? std::filesystem::is_directory(target)
        : target.has_filename() ? false : true;
    return targetIsDir ? (target / "poe.db") : target;
}

std::filesystem::path walSidecar(const std::filesystem::path& dbPath) {
    auto path = dbPath;
    path += "-wal";
    return path;
}

std::filesystem::path shmSidecar(const std::filesystem::path& dbPath) {
    auto path = dbPath;
    path += "-shm";
    return path;
}

void ensureParentDirectory(const std::filesystem::path& filePath) {
    const auto parent = filePath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void copyFileOrThrow(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const char* failurePrefix) {
    std::error_code ec;
    std::filesystem::copy_file(
        source,
        destination,
        std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec) {
        throw std::runtime_error(std::string(failurePrefix) + ec.message());
    }
}

} // namespace

std::filesystem::path poeDbPath(const std::string& dataDir) {
    return std::filesystem::path(dataDir) / "poe" / "poe.db";
}

crypto::Hash256 rewardIdForAcceptance(const crypto::Hash256& submitId) {
    std::vector<uint8_t> buffer;
    const std::string tag = "poe_v1_accept";
    buffer.insert(buffer.end(), tag.begin(), tag.end());
    buffer.insert(buffer.end(), submitId.begin(), submitId.end());
    return crypto::sha256(buffer.data(), buffer.size());
}

crypto::Hash256 rewardIdForEpoch(
    uint64_t epochId,
    const crypto::Hash256& contentId) {
    std::vector<uint8_t> buffer;
    const std::string tag = "poe_v1_epoch";
    buffer.insert(buffer.end(), tag.begin(), tag.end());
    for (int i = 0; i < 8; ++i) {
        buffer.push_back(static_cast<uint8_t>((epochId >> (8 * i)) & 0xFF));
    }
    buffer.insert(buffer.end(), contentId.begin(), contentId.end());
    return crypto::sha256(buffer.data(), buffer.size());
}

std::vector<std::string> exportPoeDatabase(
    const std::filesystem::path& sourceDb,
    const std::filesystem::path& exportTarget) {
    const auto outputDb = resolvePoeDbFile(exportTarget);
    ensureParentDirectory(outputDb);

    copyFileOrThrow(sourceDb, outputDb, "copy DB failed: ");

    std::vector<std::string> copiedPaths;
    copiedPaths.push_back(outputDb.string());

    const auto sourceWal = walSidecar(sourceDb);
    if (std::filesystem::exists(sourceWal)) {
        const auto outputWal = walSidecar(outputDb);
        copyFileOrThrow(sourceWal, outputWal, "copy WAL failed: ");
        copiedPaths.push_back(outputWal.string());
    }

    const auto sourceShm = shmSidecar(sourceDb);
    if (std::filesystem::exists(sourceShm)) {
        const auto outputShm = shmSidecar(outputDb);
        copyFileOrThrow(sourceShm, outputShm, "copy SHM failed: ");
        copiedPaths.push_back(outputShm.string());
    }

    return copiedPaths;
}

std::filesystem::path importPoeDatabase(
    const std::filesystem::path& importTarget,
    const std::filesystem::path& destinationDb) {
    const auto inputDb = resolvePoeDbFile(importTarget);
    if (!std::filesystem::exists(inputDb)) {
        throw std::runtime_error("source DB not found");
    }

    ensureParentDirectory(destinationDb);
    copyFileOrThrow(inputDb, destinationDb, "copy DB failed: ");

    const auto inputWal = walSidecar(inputDb);
    if (std::filesystem::exists(inputWal)) {
        copyFileOrThrow(inputWal, walSidecar(destinationDb), "copy WAL failed: ");
    }

    const auto inputShm = shmSidecar(inputDb);
    if (std::filesystem::exists(inputShm)) {
        copyFileOrThrow(inputShm, shmSidecar(destinationDb), "copy SHM failed: ");
    }

    return destinationDb;
}

} // namespace synapse::node
