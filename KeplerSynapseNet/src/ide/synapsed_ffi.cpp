// C ABI wrappers. Rust/Tauri only talks to these symbols (libsynapsed).
// rpc_call mallocs the JSON string; the caller MUST synapsed_free_string it.

#include "synapsed_ffi.h"
#include "ide/synapsed_engine.h"

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

}  // namespace

SYNAPSED_API int synapsed_init(const char* config_path) {
    if (!config_path) return -1;
    return synapse::ide::SynapsedEngine::instance().init(config_path);
}

SYNAPSED_API void synapsed_shutdown(void) {
    if (!synapse::ide::synapsedEngineAlive()) return;
    synapse::ide::SynapsedEngine::instance().shutdown();
}

SYNAPSED_API const char* synapsed_rpc_call(const char* method, const char* params_json) {
    if (!method) return nullptr;
    if (!rpcMethodAllowed(method)) {
        static const char kRejected[] = "{\"error\":\"method not allowed\"}";
        char* err = static_cast<char*>(std::malloc(sizeof(kRejected)));
        if (!err) return nullptr;
        std::memcpy(err, kRejected, sizeof(kRejected));
        return err;
    }
    std::string params = params_json ? params_json : "{}";
    std::string result = synapse::ide::SynapsedEngine::instance().rpcCall(method, params);
    char* buf = static_cast<char*>(std::malloc(result.size() + 1));
    if (!buf) return nullptr;
    std::memcpy(buf, result.c_str(), result.size() + 1);
    return buf;
}

SYNAPSED_API int synapsed_subscribe(const char* event_type, synapsed_event_callback callback) {
    if (!event_type || !callback) return -1;
    return synapse::ide::SynapsedEngine::instance().subscribe(
        event_type,
        [callback](const char* et, const char* pj) { callback(et, pj); });
}

SYNAPSED_API const char* synapsed_get_status(void) {
    std::string status = synapse::ide::SynapsedEngine::instance().getStatus();
    char* buf = static_cast<char*>(std::malloc(status.size() + 1));
    if (!buf) return nullptr;
    std::memcpy(buf, status.c_str(), status.size() + 1);
    return buf;
}

SYNAPSED_API void synapsed_free_string(const char* str) {
    std::free(const_cast<char*>(str));
}
