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

// The PoE database lives at <dataDir>/poe/poe.db. Caller paths are confined to
// that data directory after canonicalization, including WAL and SHM sidecars.
std::filesystem::path canonicalPath(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec || canonical.empty()) {
        throw std::runtime_error("path escapes data directory");
    }
    canonical = canonical.lexically_normal();
    if (canonical.empty() || !canonical.is_absolute()) {
        throw std::runtime_error("path escapes data directory");
    }
    return canonical;
}

bool isInsideDataDirectory(
    const std::filesystem::path& path,
    const std::filesystem::path& dataDir) {
    const auto relative = path.lexically_normal().lexically_relative(
        dataDir.lexically_normal());
    if (relative.empty()) {
        return false;
    }
    for (const auto& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

std::filesystem::path pathInsideDataDirectory(
    const std::filesystem::path& path,
    const std::filesystem::path& dataDir) {
    const auto canonical = canonicalPath(path);
    if (!isInsideDataDirectory(canonical, dataDir)) {
        throw std::runtime_error("path escapes data directory");
    }
    return canonical;
}

std::filesystem::path dataDirectoryForDb(const std::filesystem::path& dbPath) {
    const auto parent = dbPath.parent_path();
    const auto filename = dbPath.filename();
    if (parent.empty() || filename.empty() || filename == "." || filename == "..") {
        throw std::runtime_error("path escapes data directory");
    }
    const auto canonicalParent = canonicalPath(parent);
    const auto dataDir = canonicalParent.parent_path().lexically_normal();
    if (dataDir.empty() || dataDir == canonicalParent ||
        !isInsideDataDirectory(canonicalParent / filename, dataDir)) {
        throw std::runtime_error("path escapes data directory");
    }
    return dataDir;
}

bool pathExists(const std::filesystem::path& path) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        throw std::runtime_error("path escapes data directory");
    }
    return exists && !ec;
}

void ensureParentDirectory(
    const std::filesystem::path& filePath,
    const std::filesystem::path& dataDir) {
    const auto parent = canonicalPath(filePath.parent_path());
    if (!isInsideDataDirectory(parent, dataDir)) {
        throw std::runtime_error("path escapes data directory");
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(dataDir, ec) || ec) {
        throw std::runtime_error("path escapes data directory");
    }
    std::filesystem::create_directories(parent);
    if (!isInsideDataDirectory(canonicalPath(parent), dataDir)) {
        throw std::runtime_error("path escapes data directory");
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

std::filesystem::path copyInsideDataDirectory(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const std::filesystem::path& dataDir,
    const char* failurePrefix) {
    const auto sourcePath = pathInsideDataDirectory(source, dataDir);
    const auto destinationPath = pathInsideDataDirectory(destination, dataDir);
    copyFileOrThrow(sourcePath, destinationPath, failurePrefix);
    return destinationPath;
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
    const auto dataDir = dataDirectoryForDb(sourceDb);
    const auto source = pathInsideDataDirectory(sourceDb, dataDir);
    const auto resolvedOutput = resolvePoeDbFile(exportTarget);
    const auto outputDb = pathInsideDataDirectory(resolvedOutput, dataDir);

    const auto sourceWal = walSidecar(sourceDb);
    const auto sourceShm = shmSidecar(sourceDb);
    const bool hasWal = pathExists(sourceWal);
    const bool hasShm = pathExists(sourceShm);
    std::filesystem::path resolvedSourceWal;
    std::filesystem::path resolvedSourceShm;
    std::filesystem::path outputWal;
    std::filesystem::path outputShm;
    if (hasWal) {
        resolvedSourceWal = pathInsideDataDirectory(sourceWal, dataDir);
        outputWal = pathInsideDataDirectory(walSidecar(resolvedOutput), dataDir);
    }
    if (hasShm) {
        resolvedSourceShm = pathInsideDataDirectory(sourceShm, dataDir);
        outputShm = pathInsideDataDirectory(shmSidecar(resolvedOutput), dataDir);
    }

    ensureParentDirectory(outputDb, dataDir);

    std::vector<std::string> copiedPaths;
    copiedPaths.push_back(
        copyInsideDataDirectory(source, outputDb, dataDir, "copy DB failed: ").string());
    if (hasWal) {
        copiedPaths.push_back(
            copyInsideDataDirectory(
                resolvedSourceWal,
                outputWal,
                dataDir,
                "copy WAL failed: ").string());
    }
    if (hasShm) {
        copiedPaths.push_back(
            copyInsideDataDirectory(
                resolvedSourceShm,
                outputShm,
                dataDir,
                "copy SHM failed: ").string());
    }
    return copiedPaths;
}

std::filesystem::path importPoeDatabase(
    const std::filesystem::path& importTarget,
    const std::filesystem::path& destinationDb) {
    const auto dataDir = dataDirectoryForDb(destinationDb);
    const auto destination = pathInsideDataDirectory(destinationDb, dataDir);
    const auto resolvedInput = resolvePoeDbFile(importTarget);
    const auto inputDb = pathInsideDataDirectory(resolvedInput, dataDir);
    if (!std::filesystem::exists(inputDb)) {
        throw std::runtime_error("source DB not found");
    }

    const auto inputWal = walSidecar(resolvedInput);
    const auto inputShm = shmSidecar(resolvedInput);
    const bool hasWal = pathExists(inputWal);
    const bool hasShm = pathExists(inputShm);
    std::filesystem::path resolvedInputWal;
    std::filesystem::path resolvedInputShm;
    std::filesystem::path outputWal;
    std::filesystem::path outputShm;
    if (hasWal) {
        resolvedInputWal = pathInsideDataDirectory(inputWal, dataDir);
        outputWal = pathInsideDataDirectory(walSidecar(destination), dataDir);
    }
    if (hasShm) {
        resolvedInputShm = pathInsideDataDirectory(inputShm, dataDir);
        outputShm = pathInsideDataDirectory(shmSidecar(destination), dataDir);
    }

    ensureParentDirectory(destination, dataDir);
    copyInsideDataDirectory(inputDb, destination, dataDir, "copy DB failed: ");
    if (hasWal) {
        copyInsideDataDirectory(resolvedInputWal, outputWal, dataDir, "copy WAL failed: ");
    }
    if (hasShm) {
        copyInsideDataDirectory(resolvedInputShm, outputShm, dataDir, "copy SHM failed: ");
    }
    return destinationDb;
}

} // namespace synapse::node
