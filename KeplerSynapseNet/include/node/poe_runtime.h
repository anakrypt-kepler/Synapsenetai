#pragma once

#include "crypto/crypto.h"

#include <filesystem>
#include <string>
#include <vector>

namespace synapse::node {

std::filesystem::path poeDbPath(const std::string& dataDir);

crypto::Hash256 rewardIdForAcceptance(const crypto::Hash256& submitId);

crypto::Hash256 rewardIdForEpoch(
    uint64_t epochId,
    const crypto::Hash256& contentId);

std::vector<std::string> exportPoeDatabase(
    const std::filesystem::path& sourceDb,
    const std::filesystem::path& exportTarget);

std::filesystem::path importPoeDatabase(
    const std::filesystem::path& importTarget,
    const std::filesystem::path& destinationDb);

} // namespace synapse::node
