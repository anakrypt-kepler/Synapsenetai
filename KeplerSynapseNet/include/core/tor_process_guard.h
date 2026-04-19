#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace synapse::core {

bool isOwnedManagedTorCommandLine(const std::string& cmdline, const std::string& managedTorDataDir);

std::vector<int64_t> parseOwnedManagedTorPidsFromPsOutput(const std::string& psOutput,
                                                          const std::string& managedTorDataDir);

}
