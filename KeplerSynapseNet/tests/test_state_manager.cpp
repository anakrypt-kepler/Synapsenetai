#include "core/state_manager.h"
#include "database/database.h"

#include <cassert>
#include <filesystem>
#include <iostream>

using namespace synapse::core;
namespace fs = std::filesystem;

static void testSnapshotSaveLoad() {
    StateSnapshot s;
    s.nodeState = NodeState::Running;
    s.syncState = SyncState::Complete;
    s.blockHeight = 12345;
    s.headerHeight = 12346;
    s.peerCount = 8;
    s.memPoolSize = 42;
    s.balance = 1000000;
    s.pendingBalance = 5000;
    s.walletLocked = false;
    s.miningActive = true;
    s.modelLoaded = true;
    s.timestamp = 1610000000000ULL;
    s.metadata["foo"] = "bar";
    s.metadata["baz"] = "quux";

    const auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path tmpDir = fs::temp_directory_path() / ("synapsenet_state_" + uniq);
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);

    StatePersistence p(tmpDir.string());
    bool ok = p.save(s);
    assert(ok);
    assert(p.exists());

    StateSnapshot loaded;
    bool L = p.load(loaded);
    assert(L);

    assert(loaded.nodeState == s.nodeState);
    assert(loaded.syncState == s.syncState);
    assert(loaded.blockHeight == s.blockHeight);
    assert(loaded.headerHeight == s.headerHeight);
    assert(loaded.peerCount == s.peerCount);
    assert(loaded.memPoolSize == s.memPoolSize);
    assert(loaded.balance == s.balance);
    assert(loaded.pendingBalance == s.pendingBalance);
    assert(loaded.walletLocked == s.walletLocked);
    assert(loaded.miningActive == s.miningActive);
    assert(loaded.modelLoaded == s.modelLoaded);
    assert(loaded.timestamp == s.timestamp);
    assert(loaded.metadata == s.metadata);

    p.remove();
    assert(!p.exists());

    fs::remove_all(tmpDir, ec);
}

int main() {
    std::cout << "Running state_manager snapshot tests...\n";
    testSnapshotSaveLoad();
    std::cout << "State snapshot tests passed!\n";
    return 0;
}
