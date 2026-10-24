// C ABI wrappers. Rust/Tauri only talks to these symbols (libsynapsed).
// rpc_call mallocs the JSON string; the caller MUST synapsed_free_string it.

#include "synapsed_ffi.h"
#include "ide/synapsed_engine.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// Names the desktop app already sends. rpcCall string literals, guide ops,
// and invoke wrappers in tauri-app/src. Anything else stops here.
const char* const kAllowedRpcMethods[] = {
    "ai.complete",
    "blocks.get",
    "blocks.list",
    "crypto.status",
    "exploit.list",
    "exploit.stats",
    "exploit.sync",
    "find.engines",
    "find.read",
    "find.search",
    "guide.clear",
    "guide.confirm",
    "guide.history",
    "guide.observe",
    "guide.refuse",
    "guide.say",
    "harvest.get",
    "harvest.list",
    "knowledge.my_submissions",
    "knowledge.search",
    "knowledge.submit",
    "model.catalog",
    "model.download",
    "model.download.cancel",
    "model.download.status",
    "model.load",
    "model.status",
    "model.unload",
    "msg.get",
    "msg.list",
    "msg.send",
    "naan.config",
    "naan.control",
    "naan.status",
    "network.info",
    "node.status",
    "poe.stats",
    "poe.submit_code",
    "privacy.ring.sign",
    "privacy.ring.verify",
    "privacy.status",
    "privacy.stealth.generate",
    "privacy.stealth.send",
    "privacy.view.scan",
    "profile.set_avatar",
    "rental.list",
    "rental.share",
    "settings.get",
    "settings.update",
    "transfer.history",
    "transfer.send",
    "update.check",
    "wallet.create",
    "wallet.export",
    "wallet.import",
    "wallet.info",
    "wallet.restore",
    "wallet.seed",
};

bool rpcMethodAllowed(const char* method) {
    if (!method || method[0] == '\0') return false;
    for (const char* allowed : kAllowedRpcMethods) {
        if (std::strcmp(method, allowed) == 0) return true;
    }
    return false;
}

const char* allocCString(const char* text, size_t bytesWithNul) {
    char* buf = static_cast<char*>(std::malloc(bytesWithNul));
    if (!buf) return nullptr;
    std::memcpy(buf, text, bytesWithNul);
    return buf;
}

const char* engineUnavailable() {
    static const char kMsg[] = "{\"error\":\"not initialized\"}";
    return allocCString(kMsg, sizeof(kMsg));
}

// Keeps the process singleton alive for this FFI call. After exit begins,
// ok() is false and instance() must not be called.
struct EnginePin {
    bool armed = false;

    EnginePin() : armed(synapse::ide::synapsedEngineEnter()) {}
    ~EnginePin() {
        if (armed) synapse::ide::synapsedEngineLeave();
    }

    EnginePin(const EnginePin&) = delete;
    EnginePin& operator=(const EnginePin&) = delete;

    bool ok() const { return armed; }
};

}  // namespace

SYNAPSED_API int synapsed_init(const char* config_path) {
    if (!config_path) return -1;
    EnginePin pin;
    if (!pin.ok()) return -1;
    return synapse::ide::SynapsedEngine::instance().init(config_path);
}

SYNAPSED_API void synapsed_shutdown(void) {
    if (!synapse::ide::synapsedEngineAlive()) return;
    EnginePin pin;
    if (!pin.ok()) return;
    synapse::ide::SynapsedEngine::instance().shutdown();
}

SYNAPSED_API const char* synapsed_rpc_call(const char* method, const char* params_json) {
    if (!method) return nullptr;
    if (!rpcMethodAllowed(method)) {
        static const char kRejected[] = "{\"error\":\"method not allowed\"}";
        return allocCString(kRejected, sizeof(kRejected));
    }
    EnginePin pin;
    if (!pin.ok()) return engineUnavailable();
    std::string params = params_json ? params_json : "{}";
    std::string result = synapse::ide::SynapsedEngine::instance().rpcCall(method, params);
    char* buf = static_cast<char*>(std::malloc(result.size() + 1));
    if (!buf) return nullptr;
    std::memcpy(buf, result.c_str(), result.size() + 1);
    return buf;
}

SYNAPSED_API int synapsed_subscribe(const char* event_type, synapsed_event_callback callback) {
    if (!event_type || !callback) return -1;
    EnginePin pin;
    if (!pin.ok()) return -1;
    // Copy the type before the call returns. The Rust CString dies with
    // the caller, and the engine keeps the subscription.
    const std::string eventCopy(event_type);
    return synapse::ide::SynapsedEngine::instance().subscribe(
        eventCopy,
        [callback](const char* et, const char* pj) { callback(et, pj); });
}

SYNAPSED_API const char* synapsed_get_status(void) {
    EnginePin pin;
    if (!pin.ok()) return engineUnavailable();
    std::string status = synapse::ide::SynapsedEngine::instance().getStatus();
    char* buf = static_cast<char*>(std::malloc(status.size() + 1));
    if (!buf) return nullptr;
    std::memcpy(buf, status.c_str(), status.size() + 1);
    return buf;
}

SYNAPSED_API void synapsed_free_string(const char* str) {
    std::free(const_cast<char*>(str));
}
