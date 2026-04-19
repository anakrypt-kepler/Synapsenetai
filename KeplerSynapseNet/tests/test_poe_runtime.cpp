#include "node/poe_runtime.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path uniqueTempRoot() {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("synapsenet_poe_runtime_" +
            std::to_string(static_cast<unsigned long long>(tick)));
}

void writeFile(const std::filesystem::path& path, const std::string& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << value;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

void testRewardIdsAreDeterministic() {
    const auto submitId = synapse::crypto::sha256("submit-id");
    const auto contentId = synapse::crypto::sha256("content-id");

    const auto acceptA = synapse::node::rewardIdForAcceptance(submitId);
    const auto acceptB = synapse::node::rewardIdForAcceptance(submitId);
    const auto epochOne = synapse::node::rewardIdForEpoch(1, contentId);
    const auto epochTwo = synapse::node::rewardIdForEpoch(2, contentId);

    assert(acceptA == acceptB);
    assert(acceptA != epochOne);
    assert(epochOne != epochTwo);
}

void testPoeDbPathUsesDataDir() {
    const auto path = synapse::node::poeDbPath("/tmp/synapsenet-data");
    assert(path == std::filesystem::path("/tmp/synapsenet-data") / "poe" / "poe.db");
}

void testExportAndImportCopiesDbAndSidecars() {
    const auto root = uniqueTempRoot();
    const auto sourceDb = root / "source" / "poe.db";
    const auto sourceWal = std::filesystem::path(sourceDb.string() + "-wal");
    const auto sourceShm = std::filesystem::path(sourceDb.string() + "-shm");

    writeFile(sourceDb, "db");
    writeFile(sourceWal, "wal");
    writeFile(sourceShm, "shm");

    const auto exportDir = root / "exported";
    const auto copiedPaths = synapse::node::exportPoeDatabase(sourceDb, exportDir);
    assert(copiedPaths.size() == 3);
    assert(readFile(exportDir / "poe.db") == "db");
    assert(readFile(std::filesystem::path((exportDir / "poe.db").string() + "-wal")) == "wal");
    assert(readFile(std::filesystem::path((exportDir / "poe.db").string() + "-shm")) == "shm");

    const auto importDest = root / "imported" / "poe.db";
    const auto importedDb = synapse::node::importPoeDatabase(exportDir, importDest);
    assert(importedDb == importDest);
    assert(readFile(importDest) == "db");
    assert(readFile(std::filesystem::path(importDest.string() + "-wal")) == "wal");
    assert(readFile(std::filesystem::path(importDest.string() + "-shm")) == "shm");

    const auto exportFile = root / "flat" / "copy.db";
    const auto copiedFilePaths = synapse::node::exportPoeDatabase(sourceDb, exportFile);
    assert(copiedFilePaths.size() == 3);
    assert(readFile(exportFile) == "db");

    std::filesystem::remove_all(root);
}

void testImportRejectsMissingDb() {
    const auto root = uniqueTempRoot();
    const auto missingSource = root / "missing";
    const auto destinationDb = root / "dest" / "poe.db";
    bool threw = false;
    try {
        (void)synapse::node::importPoeDatabase(missingSource, destinationDb);
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "source DB not found";
    }
    assert(threw);
}

} // namespace

int main() {
    testRewardIdsAreDeterministic();
    testPoeDbPathUsesDataDir();
    testExportAndImportCopiesDbAndSidecars();
    testImportRejectsMissingDb();
    return 0;
}
