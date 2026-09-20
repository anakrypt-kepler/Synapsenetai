#include "core/naan_task_share.h"

#include <atomic>
#include <cassert>
#include <string>
#include <thread>

using synapse::core::NaanTaskShare;

static void testExclusiveUrlClaim() {
    NaanTaskShare share;
    assert(share.claimUrl("primary", "http://a.onion/1"));
    assert(!share.claimUrl("crew-1", "http://a.onion/1"));
    assert(share.claimUrl("primary", "http://a.onion/1"));
    share.releaseUrl("crew-1", "http://a.onion/1");
    assert(!share.claimUrl("crew-1", "http://a.onion/1"));
    share.releaseUrl("primary", "http://a.onion/1");
    assert(share.claimUrl("crew-1", "http://a.onion/1"));
}

static void testTopicAndHashDedup() {
    NaanTaskShare share;
    assert(share.claimTopic("primary", "zero-day"));
    assert(share.claimTopic("crew-1", "zero-day"));
    share.releaseTopic("primary", "zero-day");
    assert(share.claimTopic("crew-1", "zero-day"));
    assert(share.noteHash("abc"));
    assert(!share.noteHash("abc"));
    assert(share.noteHash("def"));
    assert(share.filedHashCount() == 2);
}

static void testConcurrentClaims() {
    NaanTaskShare share;
    std::atomic<int> wins{0};
    auto worker = [&](const std::string& id) {
        if (share.claimUrl(id, "http://shared.onion/q")) wins.fetch_add(1);
    };
    std::thread a(worker, "a");
    std::thread b(worker, "b");
    a.join();
    b.join();
    assert(wins.load() == 1);
}

int main() {
    testExclusiveUrlClaim();
    testTopicAndHashDedup();
    testConcurrentClaims();
    return 0;
}
