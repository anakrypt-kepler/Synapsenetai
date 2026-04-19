#include "synapsed_ffi.h"
#include "ide/synapsed_engine.h"

#include <cstring>

SYNAPSED_API int synapsed_init(const char* config_path) {
    if (!config_path) return -1;
    return synapse::ide::SynapsedEngine::instance().init(config_path);
}

SYNAPSED_API void synapsed_shutdown(void) {
    synapse::ide::SynapsedEngine::instance().shutdown();
}

SYNAPSED_API const char* synapsed_rpc_call(const char* method, const char* params_json) {
    if (!method) return nullptr;
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
