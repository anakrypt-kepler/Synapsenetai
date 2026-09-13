#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Owned Tor child process (agent.tor.mode=managed). Only kill PIDs whose
// cmdline contains our managedTorDataDir — never a user's Tor Browser.

namespace synapse::core {

bool isOwnedManagedTorCommandLine(const std::string& cmdline, const std::string& managedTorDataDir);

std::vector<int64_t> parseOwnedManagedTorPidsFromPsOutput(const std::string& psOutput,
                                                          const std::string& managedTorDataDir);

}
