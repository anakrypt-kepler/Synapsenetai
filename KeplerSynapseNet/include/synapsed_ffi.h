#ifndef SYNAPSED_FFI_H
#define SYNAPSED_FFI_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #ifdef SYNAPSED_BUILDING_LIB
    #define SYNAPSED_API __declspec(dllexport)
  #else
    #define SYNAPSED_API __declspec(dllimport)
  #endif
#else
  #define SYNAPSED_API __attribute__((visibility("default")))
#endif

typedef void (*synapsed_event_callback)(const char* event_type, const char* payload_json);

SYNAPSED_API int synapsed_init(const char* config_path);
SYNAPSED_API void synapsed_shutdown(void);
SYNAPSED_API const char* synapsed_rpc_call(const char* method, const char* params_json);
SYNAPSED_API int synapsed_subscribe(const char* event_type, synapsed_event_callback callback);
SYNAPSED_API const char* synapsed_get_status(void);
SYNAPSED_API void synapsed_free_string(const char* str);

#ifdef __cplusplus
}
#endif

#endif
