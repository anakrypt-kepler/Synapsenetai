#include "core/agent_coordination.h"
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <iostream>

#ifdef NDEBUG
#undef assert
#define assert(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while (0)
#endif

using namespace synapse::core;

static void testIdentityGeneration() {
    auto id1 = AgentIdentity::generate();
    auto id2 = AgentIdentity::generate();
    
    assert(id1.valid());
    assert(id2.valid());
    assert(id1.id != id2.id);
    std::cout << "testIdentityGeneration: PASS\n";
}

static void testIdentityFromSeed() {
    synapse::crypto::Hash256 seed{};
    seed[0] = 42;
    
    auto id1 = AgentIdentity::fromSeed(seed);
    auto id2 = AgentIdentity::fromSeed(seed);
    
    assert(id1.valid());
    assert(id2.valid());
    assert(id1.id == id2.id);
    std::cout << "testIdentityFromSeed: PASS\n";
}

static void testRoomPostMessage() {
    AgentRoom room("test-room", RoomType::TASKS);
    auto id = AgentIdentity::generate();
    
    uint64_t ts = 1700000000;
    assert(room.post(id, RoomMessageType::TASK, "Task description", ts));
    assert(room.messageCount() == 1);
    std::cout << "testRoomPostMessage: PASS\n";
}

static void testRoomRejectsInvalidIdentity() {
    AgentRoom room("test-room", RoomType::TASKS);
    
    AgentIdentity invalid;
    assert(!invalid.valid());
    assert(!room.post(invalid, RoomMessageType::TASK, "Task", 1700000000));
    assert(room.messageCount() == 0);
    std::cout << "testRoomRejectsInvalidIdentity: PASS\n";
}

static void testRoomRejectsOversizedPayload() {
    AgentRoom room("test-room", RoomType::TASKS);
    auto id = AgentIdentity::generate();
    
    std::string largePayload(10000, 'x');
    assert(!room.post(id, RoomMessageType::TASK, largePayload, 1700000000));
    assert(room.messageCount() == 0);
    std::cout << "testRoomRejectsOversizedPayload: PASS\n";
}

static void testRoomAntiReplay() {
    AgentRoom room("test-room", RoomType::TASKS);
    auto id = AgentIdentity::generate();
    
    assert(room.post(id, RoomMessageType::TASK, "First message", 1700000000));
    
    auto artifacts = room.getArtifacts(0, 10);
    assert(artifacts.size() == 1);
    
    auto msg = artifacts[0].message;
    assert(!room.addVerifiedMessage(msg));
    assert(room.messageCount() == 1);
    std::cout << "testRoomAntiReplay: PASS\n";
}

static void testRoomSequenceNumbers() {
    AgentRoom room("test-room", RoomType::TASKS);
    auto id1 = AgentIdentity::generate();
    auto id2 = AgentIdentity::generate();
    
    assert(room.nextSequence(id1.id) == 1);
    assert(room.post(id1, RoomMessageType::TASK, "Message 1", 1700000000));
    assert(room.nextSequence(id1.id) == 2);
    
    assert(room.post(id1, RoomMessageType::TASK, "Message 2", 1700000001));
    assert(room.nextSequence(id1.id) == 3);
    
    assert(room.nextSequence(id2.id) == 1);
    std::cout << "testRoomSequenceNumbers: PASS\n";
}

static void testRoomMultipleAuthors() {
    AgentRoom room("test-room", RoomType::TASKS);
    auto id1 = AgentIdentity::generate();
    auto id2 = AgentIdentity::generate();
    
    assert(room.post(id1, RoomMessageType::TASK, "From author 1", 1700000000));
    assert(room.post(id2, RoomMessageType::TASK, "From author 2", 1700000001));
    
    assert(room.messageCount() == 2);
    std::cout << "testRoomMultipleAuthors: PASS\n";
}

static void testRoomGetArtifacts() {
    AgentRoom room("test-room", RoomType::TASKS);
    auto id = AgentIdentity::generate();
    
    assert(room.post(id, RoomMessageType::TASK, "Task 1", 1700000000));
    assert(room.post(id, RoomMessageType::REVIEW, "Review 1", 1700000001));
    assert(room.post(id, RoomMessageType::ALERT, "Alert 1", 1700000002));
    
    auto all = room.getArtifacts(0, 100);
    assert(all.size() == 3);
    
    auto recent = room.getArtifacts(1700000001, 100);
    assert(recent.size() == 2);
    
    auto limited = room.getArtifacts(0, 2);
    assert(limited.size() == 2);
    std::cout << "testRoomGetArtifacts: PASS\n";
}

static void testRoomGetArtifactByHash() {
    AgentRoom room("test-room", RoomType::TASKS);
    auto id = AgentIdentity::generate();
    
    assert(room.post(id, RoomMessageType::TASK, "Task content", 1700000000));
    
    auto artifacts = room.getArtifacts(0, 10);
    assert(artifacts.size() == 1);
    
    auto hash = artifacts[0].hash;
    auto artifact = room.getArtifact(hash);
    assert(artifact.has_value());
    assert(artifact->message.payload == "Task content");
    
    synapse::crypto::Hash256 randomHash{};
    auto notFound = room.getArtifact(randomHash);
    assert(!notFound.has_value());
    std::cout << "testRoomGetArtifactByHash: PASS\n";
}

static void testHubCreateRoom() {
    CoordinationHub hub;
    
    assert(hub.createRoom("tasks-main", RoomType::TASKS));
    assert(hub.hasRoom("tasks-main"));
    assert(!hub.createRoom("tasks-main", RoomType::TASKS));
    std::cout << "testHubCreateRoom: PASS\n";
}

static void testHubMaxRoomsPerType() {
    CoordinationConfig cfg;
    cfg.maxRoomsPerType = 2;
    
    CoordinationHub hub(cfg);
    
    assert(hub.createRoom("room1", RoomType::TASKS));
    assert(hub.createRoom("room2", RoomType::TASKS));
    assert(!hub.createRoom("room3", RoomType::TASKS));
    
    assert(hub.createRoom("alerts1", RoomType::ALERTS));
    assert(hub.createRoom("alerts2", RoomType::ALERTS));
    assert(!hub.createRoom("alerts3", RoomType::ALERTS));
    std::cout << "testHubMaxRoomsPerType: PASS\n";
}

static void testHubPostToRoom() {
    CoordinationHub hub;
    assert(hub.createRoom("tasks-main", RoomType::TASKS));
    auto id = AgentIdentity::generate();
    
    assert(hub.postToRoom("tasks-main", id, RoomMessageType::TASK, "Task description", 1700000000));
    assert(!hub.postToRoom("nonexistent", id, RoomMessageType::TASK, "Task", 1700000000));
    
    assert(hub.totalMessages() == 1);
    std::cout << "testHubPostToRoom: PASS\n";
}

static void testHubListRooms() {
    CoordinationHub hub;
    
    hub.createRoom("tasks1", RoomType::TASKS);
    hub.createRoom("tasks2", RoomType::TASKS);
    hub.createRoom("reviews1", RoomType::REVIEWS);
    
    auto tasksRooms = hub.listRooms(RoomType::TASKS);
    assert(tasksRooms.size() == 2);
    
    auto reviewsRooms = hub.listRooms(RoomType::REVIEWS);
    assert(reviewsRooms.size() == 1);
    
    auto allRooms = hub.listAllRooms();
    assert(allRooms.size() == 3);
    std::cout << "testHubListRooms: PASS\n";
}

static void testObservatoryFeed() {
    CoordinationHub hub;
    assert(hub.createRoom("tasks-main", RoomType::TASKS));
    auto id1 = AgentIdentity::generate();
    auto id2 = AgentIdentity::generate();
    
    hub.postToRoom("tasks-main", id1, RoomMessageType::TASK, "Task 1", 1700000000);
    hub.postToRoom("tasks-main", id2, RoomMessageType::REVIEW, "Review 1", 1700000001);
    
    ObservatoryFeed feed(hub);
    
    auto entries = feed.getFeed(0, 100);
    assert(entries.size() == 2);
    assert(entries[0].roomId == "tasks-main");
    assert(entries[1].roomId == "tasks-main");
    
    auto recentEntries = feed.getFeed(1700000001, 100);
    assert(recentEntries.size() == 1);
    std::cout << "testObservatoryFeed: PASS\n";
}

static void testObservatoryGetArtifact() {
    CoordinationHub hub;
    assert(hub.createRoom("tasks-main", RoomType::TASKS));
    auto id = AgentIdentity::generate();
    
    hub.postToRoom("tasks-main", id, RoomMessageType::TASK, "Task content", 1700000000);
    
    auto room = hub.getRoom("tasks-main");
    assert(room != nullptr);
    auto artifacts = room->getArtifacts(0, 10);
    assert(artifacts.size() == 1);
    
    ObservatoryFeed feed(hub);
    auto artifact = feed.getArtifact(artifacts[0].hash);
    assert(artifact.has_value());
    assert(artifact->message.payload == "Task content");
    
    auto msg = feed.getMessage(artifacts[0].hash);
    assert(msg.has_value());
    assert(msg->payload == "Task content");
    std::cout << "testObservatoryGetArtifact: PASS\n";
}

static void testTypeConversions() {
    assert(roomMessageTypeToString(RoomMessageType::TASK) == "task");
    assert(roomMessageTypeToString(RoomMessageType::REVIEW) == "review");
    assert(roomMessageTypeToString(RoomMessageType::DISPUTE) == "dispute");
    assert(roomMessageTypeToString(RoomMessageType::ALERT) == "alert");
    
    assert(stringToRoomMessageType("task") == RoomMessageType::TASK);
    assert(stringToRoomMessageType("review") == RoomMessageType::REVIEW);
    assert(stringToRoomMessageType("dispute") == RoomMessageType::DISPUTE);
    assert(stringToRoomMessageType("alert") == RoomMessageType::ALERT);
    
    assert(roomTypeToString(RoomType::TASKS) == "tasks");
    assert(roomTypeToString(RoomType::REVIEWS) == "reviews");
    assert(roomTypeToString(RoomType::DISPUTES) == "disputes");
    assert(roomTypeToString(RoomType::ALERTS) == "alerts");
    
    assert(stringToRoomType("tasks") == RoomType::TASKS);
    assert(stringToRoomType("reviews") == RoomType::REVIEWS);
    assert(stringToRoomType("disputes") == RoomType::DISPUTES);
    assert(stringToRoomType("alerts") == RoomType::ALERTS);
    std::cout << "testTypeConversions: PASS\n";
}

static void testMessageSignatureVerification() {
    AgentRoom room("test-room", RoomType::TASKS);
    auto id = AgentIdentity::generate();
    
    assert(room.post(id, RoomMessageType::TASK, "Signed message", 1700000000));
    
    auto artifacts = room.getArtifacts(0, 10);
    assert(artifacts.size() == 1);
    
    auto msg = artifacts[0].message;
    auto canonical = msg.canonicalBytes();
    auto hash = synapse::crypto::sha256(canonical.data(), canonical.size());
    
    assert(synapse::crypto::verify(hash, msg.signature, msg.author));
    std::cout << "testMessageSignatureVerification: PASS\n";
}

static void testMessageTamperDetection() {
    AgentRoom room("test-room", RoomType::TASKS);
    auto id = AgentIdentity::generate();
    
    assert(room.post(id, RoomMessageType::TASK, "Original message", 1700000000));
    
    auto artifacts = room.getArtifacts(0, 10);
    assert(artifacts.size() == 1);
    
    RoomMessage tampered = artifacts[0].message;
    tampered.payload = "Tampered message";
    
    assert(!room.validateMessage(tampered));
    std::cout << "testMessageTamperDetection: PASS\n";
}

static void testHubIndexRecoveryNoop() {
    CoordinationHub hub;
    assert(hub.createRoom("tasks-main", RoomType::TASKS));
    assert(hub.createRoom("reviews-main", RoomType::REVIEWS));
    auto id = AgentIdentity::generate();

    assert(hub.postToRoom("tasks-main", id, RoomMessageType::TASK, "Task A", 1700000100));
    assert(hub.postToRoom("reviews-main", id, RoomMessageType::REVIEW, "Review A", 1700000101));

    CoordinationRecoveryStats stats;
    std::string reason;
    assert(hub.verifyAndRepairIndexes(&stats, &reason));
    assert(reason == "ok" || reason == "repaired");
    assert(stats.roomCount == 2);
    assert(stats.roomArtifacts >= 2);
    assert(stats.observatoryEntries >= 2);
    std::cout << "testHubIndexRecoveryNoop: PASS\n";
}

static void testRoomPruning() {
    RoomConfig cfg;
    cfg.maxMessages = 5;
    cfg.retentionSeconds = 100;
    
    AgentRoom room("test-room", RoomType::TASKS, cfg);
    auto id = AgentIdentity::generate();
    
    for (int i = 0; i < 10; ++i) {
        assert(room.post(id, RoomMessageType::TASK, "Message " + std::to_string(i), 
                          1700000000 + i));
    }
    
    assert(room.messageCount() <= 5);
    std::cout << "testRoomPruning: PASS\n";
}

static void testRoomMessageHashDeterminism() {
    auto id = AgentIdentity::generate();
    
    RoomMessage msg;
    msg.sequence = 1;
    msg.timestamp = 1700000000;
    msg.type = RoomMessageType::TASK;
    msg.author = id.id;
    msg.roomId = "test-room";
    msg.payload = "Test payload";
    
    auto hash1 = msg.computeHash();
    auto hash2 = msg.computeHash();
    
    assert(hash1 == hash2);
    std::cout << "testRoomMessageHashDeterminism: PASS\n";
}

int main() {
    std::cout << "Running agent_coordination tests...\n";
    
    testIdentityGeneration();
    testIdentityFromSeed();
    testRoomMessageHashDeterminism();
    testRoomPostMessage();
    testRoomRejectsInvalidIdentity();
    testRoomRejectsOversizedPayload();
    testRoomAntiReplay();
    testRoomSequenceNumbers();
    testRoomMultipleAuthors();
    testRoomGetArtifacts();
    testRoomGetArtifactByHash();
    testRoomPruning();
    testHubCreateRoom();
    testHubMaxRoomsPerType();
    testHubPostToRoom();
    testHubListRooms();
    testObservatoryFeed();
    testObservatoryGetArtifact();
    testTypeConversions();
    testMessageSignatureVerification();
    testMessageTamperDetection();
    testHubIndexRecoveryNoop();
    
    std::cout << "All agent_coordination tests passed!\n";
    return 0;
}
