cmake_minimum_required(VERSION 3.16)
project(KeplerSynapseNet VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(USE_LLAMA_CPP "Enable llama.cpp GGML backend" ON)
option(USE_SECP256K1 "Use libsecp256k1 for signatures" ON)
option(BUILD_PRIVACY "Build with privacy layer" OFF)
option(BUILD_TESTS "Build tests" ON)

option(SYNAPSE_FETCH_LIBOQS "Fetch liboqs automatically when the system library is unavailable" ON)
option(SYNAPSE_REQUIRE_REAL_PQC "Fail configure when liboqs-backed PQC support is unavailable" OFF)
set(SYNAPSE_LIBOQS_GIT_TAG "0.12.0" CACHE STRING "liboqs git tag used when SYNAPSE_FETCH_LIBOQS is enabled")
set(SYNAPSE_LIBOQS_MINIMAL_BUILD
    "KEM_ml_kem_768;KEM_kyber_768;SIG_dilithium_3;SIG_ml_dsa_65;SIG_sphincs_sha2_128s_simple;SIG_slh_dsa_sha2_128s"
    CACHE STRING "Minimal liboqs algorithm set used for fetched builds")
set(BUILD_TUI_DEFAULT ON)
if(WIN32)
    set(BUILD_TUI_DEFAULT OFF)
endif()
option(BUILD_TUI "Build terminal UI (ncurses)" ${BUILD_TUI_DEFAULT})

if(USE_LLAMA_CPP)
    if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/CMakeLists.txt)
        add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/llama.cpp ${CMAKE_BINARY_DIR}/llama.cpp)
        if(TARGET llama)
            set(HAS_LLAMA ON)
        endif()
    else()
        include(FetchContent)
        FetchContent_Declare(
            llama_cpp
            GIT_REPOSITORY https://github.com/ggerganov/llama.cpp.git
            GIT_TAG master
        )
        FetchContent_MakeAvailable(llama_cpp)
        if(TARGET llama)
            set(HAS_LLAMA ON)
        endif()
    endif()
endif()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)
if(SODIUM_FOUND)
    message(STATUS "Found libsodium: ${SODIUM_LIBRARIES}")
    if(SODIUM_LIBRARY_DIRS)
        link_directories(${SODIUM_LIBRARY_DIRS})
    endif()
else()
    message(FATAL_ERROR "libsodium is required. Install: apt install libsodium-dev / brew install libsodium")
endif()

# Find liboqs (optional PQC library)
set(HAS_LIBOQS OFF)
set(OQS_TARGET "")
find_path(OQS_INCLUDE_DIR
    NAMES oqs/oqs.h
)
find_library(OQS_LIBRARY
    NAMES oqs
)
if(OQS_INCLUDE_DIR AND OQS_LIBRARY)
    set(HAS_LIBOQS ON)
    message(STATUS "Found liboqs: ${OQS_LIBRARY}")
    add_library(oqs_external INTERFACE)
    target_include_directories(oqs_external INTERFACE ${OQS_INCLUDE_DIR})
    target_link_libraries(oqs_external INTERFACE ${OQS_LIBRARY})
    set(OQS_TARGET oqs_external)
elseif(SYNAPSE_FETCH_LIBOQS)
    include(FetchContent)
    message(STATUS "System liboqs not found; fetching liboqs ${SYNAPSE_LIBOQS_GIT_TAG}")
    set(OQS_BUILD_ONLY_LIB ON CACHE BOOL "" FORCE)
    set(OQS_MINIMAL_BUILD "${SYNAPSE_LIBOQS_MINIMAL_BUILD}" CACHE STRING "" FORCE)
    FetchContent_Declare(
        liboqs
        GIT_REPOSITORY https://github.com/open-quantum-safe/liboqs.git
        GIT_TAG ${SYNAPSE_LIBOQS_GIT_TAG}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(liboqs)
    if(TARGET oqs)
        set(HAS_LIBOQS ON)
        set(OQS_TARGET oqs)
        message(STATUS "Using fetched liboqs target: oqs")
    else()
        message(FATAL_ERROR "Fetched liboqs but the oqs target was not created")
    endif()
elseif(SYNAPSE_REQUIRE_REAL_PQC)
    message(FATAL_ERROR "Real PQC support was requested, but liboqs was not found and fetching is disabled")
else()
    message(WARNING "liboqs not found - building with simulated PQC implementations")
endif()

set(HAS_SECP256K1 OFF)
if(USE_SECP256K1)
    if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/secp256k1/CMakeLists.txt)
        add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/secp256k1 ${CMAKE_BINARY_DIR}/secp256k1)
        if(TARGET secp256k1)
            set(HAS_SECP256K1 ON)
        endif()
    else()
        include(FetchContent)
        FetchContent_Declare(
            secp256k1
            GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1.git
            GIT_TAG master
        )
        FetchContent_MakeAvailable(secp256k1)
        if(TARGET secp256k1)
            set(HAS_SECP256K1 ON)
        endif()
    endif()
else()
    message(FATAL_ERROR "USE_SECP256K1=OFF is not supported: placeholder crypto fallback has been removed")
endif()

if(NOT HAS_SECP256K1)
    message(FATAL_ERROR "libsecp256k1 is required for real key derivation/signature verification")
endif()

# Find and require curses/ncurses (TUI dependency).
# NOTE: On some Windows environments (e.g. MSYS2) CMake's FindCurses can be flaky,
# so we fall back to pkg-config and a manual search when needed.
if(BUILD_TUI)
    if(WIN32)
        set(CURSES_NEED_NCURSES TRUE)
        set(CURSES_NEED_WIDE TRUE)
    endif()
    find_package(Curses QUIET)
    if(NOT CURSES_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(NCURSES QUIET ncursesw ncurses)
            if(NCURSES_FOUND)
                set(CURSES_FOUND TRUE)
                set(CURSES_LIBRARIES ${NCURSES_LIBRARIES})
                set(CURSES_INCLUDE_DIR ${NCURSES_INCLUDE_DIRS})
            endif()
        endif()
    endif()
    if(NOT CURSES_FOUND)
        find_path(CURSES_INCLUDE_DIR
            NAMES
                ncurses.h
                curses.h
            PATH_SUFFIXES
                ncurses
        )
        find_library(CURSES_LIBRARIES
            NAMES
                ncursesw
                ncurses
                curses
                pdcurses
        )
        if(CURSES_INCLUDE_DIR AND CURSES_LIBRARIES)
            set(CURSES_FOUND TRUE)
        endif()
    endif()
    if(NOT CURSES_FOUND)
        message(FATAL_ERROR "curses/ncurses library not found. Install with: 'brew install ncurses' (macOS), 'apt-get install libncurses-dev' (Linux), or 'pacman -S ncurses' (MSYS2)")
    endif()
endif()

# Find SQLite3 (optional, but enables persistent storage).
set(HAS_SQLITE3 OFF)
set(SQLITE3_LIBRARY "")
set(SQLITE3_INCLUDE_DIR "")

find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    set(SQLITE3_LIBRARY ${SQLite3_LIBRARIES})
    set(SQLITE3_INCLUDE_DIR ${SQLite3_INCLUDE_DIRS})
else()
    find_library(SQLITE3_LIBRARY sqlite3)
    find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
    if(NOT SQLITE3_LIBRARY AND APPLE)
        find_library(SQLITE3_LIBRARY sqlite3
            PATHS /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib)
    endif()
endif()

if(SQLITE3_LIBRARY AND SQLITE3_INCLUDE_DIR)
    set(HAS_SQLITE3 ON)
else()
    message(WARNING "SQLite3 headers/library not found. Building with an in-memory DB fallback (no persistence).")
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)
if(HAS_SQLITE3)
    include_directories(${SQLITE3_INCLUDE_DIR})
endif()
if(BUILD_TUI AND CURSES_FOUND)
    include_directories(${CURSES_INCLUDE_DIR})
endif()
if(USE_LLAMA_CPP)
    include_directories(${CMAKE_SOURCE_DIR}/third_party/llama.cpp/include)
endif()

# Source files
set(CORE_SOURCES
    src/core/agent_coordination.cpp
    src/core/agent_draft_queue.cpp
    src/core/agent_runtime.cpp
    src/core/agent_scheduler.cpp
    src/core/agent_score.cpp
    src/core/agent_storage.cpp
    src/core/agent_submission_pipeline.cpp
    src/core/consensus.cpp
    src/core/event_system.cpp
    src/core/implant_compatibility.cpp
    src/core/implant_safety_pipeline.cpp
    src/core/knowledge.cpp
    src/core/ledger.cpp
    src/core/poe_v1.cpp
    src/core/poe_v1_engine.cpp
    src/core/poe_v1_objects.cpp
    src/core/state_manager.cpp
    src/core/tor_bridge_provider.cpp
    src/core/tor_bridge_utils.cpp
    src/core/tor_peer_identity.cpp
    src/core/tor_process_guard.cpp
    src/core/tor_route_policy.cpp
    src/core/transfer.cpp
    src/core/update_bundle.cpp
    src/core/update_installer.cpp
    src/core/wallet.cpp
)

set(TUI_SOURCES_FULL
    src/tui/primary_ui_spec.cpp
    src/tui/screens.cpp
    src/tui/tui_command_bridge.cpp
    src/tui/tui.cpp
    src/tui/tui_dashboard.cpp
    src/tui/tui_help.cpp
    src/tui/tui_knowledge.cpp
    src/tui/tui_mining.cpp
    src/tui/tui_model.cpp
    src/tui/tui_network.cpp
    src/tui/tui_runtime.cpp
    src/tui/tui_settings.cpp
    src/tui/tui_snapshot_builder.cpp
    src/tui/tui_startup.cpp
    src/tui/tui_wallet.cpp
)
set(TUI_SOURCES)
if(BUILD_TUI)
    list(APPEND TUI_SOURCES ${TUI_SOURCES_FULL})
else()
    list(APPEND TUI_SOURCES src/tui/primary_ui_spec.cpp)
endif()

set(MODEL_SOURCES
    src/model/model_access.cpp
    src/model/model_download.cpp
    src/model/model_inference.cpp
    src/model/model_loader.cpp
    src/model/model_marketplace.cpp
    src/model/model_training.cpp
)

set(NETWORK_SOURCES
    src/network/discovery.cpp
    src/network/handshake.cpp
    src/network/network.cpp
    src/network/peer_manager.cpp
    src/network/protocol.cpp
    src/network/scale_policy.cpp
    src/network/sync.cpp
)

set(CRYPTO_SOURCES
    src/crypto/address.cpp
    src/crypto/crypto.cpp
    src/crypto/keys.cpp
)

set(DATABASE_SOURCES
    src/database/database.cpp
    src/database/wallet_db.cpp
)

set(CLI_SOURCES
    src/cli/cli_parser.cpp
    src/cli/cli_rpc_client.cpp
)

set(CONFIG_SOURCES
    src/config/config_loader.cpp
)

set(NODE_SOURCES
    src/node/maintenance_runtime.cpp
    src/node/node_init.cpp
    src/node/poe_runtime.cpp
    src/node/node_status_runtime.cpp
    src/node/node_runtime.cpp
    src/node/synapse_net.cpp
    src/node/tor_control_runtime.cpp
    src/node/tor_status_runtime.cpp
)

set(RPC_SOURCES
    src/rpc/rpc_ai_runtime.cpp
    src/rpc/rpc_commands.cpp
    src/rpc/rpc_model_views.cpp
    src/rpc/rpc_naan_runtime.cpp
    src/rpc/rpc_naan_views.cpp
    src/rpc/rpc_node_views.cpp
    src/rpc/rpc_poe_views.cpp
    src/rpc/rpc_update_runtime.cpp
    src/rpc/rpc_update_views.cpp
)

set(PYTHON_SOURCES
    src/python/python_bridge.cpp
    src/python/sandbox.cpp
)

set(UTILS_SOURCES
    src/utils/config.cpp
    src/utils/format.cpp
    src/utils/logger.cpp
    src/utils/memory.cpp
    src/utils/serialize.cpp
    src/utils/single_instance.cpp
    src/utils/sleep_inhibitor.cpp
    src/utils/threading.cpp
)

set(INFRA_SOURCES
    src/infrastructure/error_handling.cpp
    src/infrastructure/messages.cpp
)

set(WEB_SOURCES
    src/web/ai_wrapper.cpp
    src/web/connector_audit.cpp
    src/web/curl_fetch.cpp
    src/web/darknet_engines.cpp
    src/web/deep_search_wrapper.cpp
    src/web/forum_crawler.cpp
    src/web/html_extractor.cpp
    src/web/onion_search_router.cpp
    src/web/query_detector.cpp
    src/web/rpc_server.cpp
    src/web/search_config.cpp
    src/web/tor_engine.cpp
    src/web/tor_fetch.cpp
    src/web/web_search.cpp
)

set(QUANTUM_SOURCES
    src/quantum/crypto_selector.cpp
    src/quantum/deterministic_otp.cpp
    src/quantum/dilithium.cpp
    src/quantum/hwrng.cpp
    src/quantum/hybrid_kem.cpp
    src/quantum/hybrid_sig.cpp
    src/quantum/key_derivation.cpp
    src/quantum/knowledge_crypto.cpp
    src/quantum/kyber.cpp
    src/quantum/model_inference_crypto.cpp
    src/quantum/network_security.cpp
    src/quantum/otp.cpp
    src/quantum/otp_manager.cpp
    src/quantum/pqc_backend_status.cpp
    src/quantum/pqc_manager.cpp
    src/quantum/qkd_bb84.cpp
    src/quantum/qkd_integration.cpp
    src/quantum/qkd_relay.cpp
    src/quantum/quantum_manager.cpp
    src/quantum/sphincs.cpp
    src/quantum/timing_defense.cpp
    src/quantum/wallet_security.cpp
)

set(PRIVACY_SOURCES
    src/privacy/amnesia.cpp
    src/privacy/dandelion.cpp
    src/privacy/decoy_traffic.cpp
    src/privacy/hidden_volume.cpp
    src/privacy/mix_inference.cpp
    src/privacy/onion_service.cpp
    src/privacy/privacy.cpp
    src/privacy/privacy_manager.cpp
    src/privacy/session_crypto.cpp
    src/privacy/socks5_proxy.cpp
    src/privacy/stealth_address.cpp
    src/privacy/traffic_analysis.cpp
)

set(IDE_SOURCES
    src/ide/agent_coordinator.cpp
    src/ide/agent_prompt.cpp
    src/ide/tools/bash_tool.cpp
    src/ide/tools/edit_tool.cpp
    src/ide/tools/grep_tool.cpp
    src/ide/tools/glob_tool.cpp
    src/ide/tools/fetch_tool.cpp
    src/ide/tools/write_tool.cpp
    src/ide/tools/download_tool.cpp
    src/ide/tools/web_search_tool.cpp
    src/ide/session.cpp
    src/ide/session_db.cpp
    src/ide/config.cpp
    src/ide/patch.cpp
    src/ide/skills.cpp
    src/ide/lsp_client.cpp
    src/ide/mcp_server.cpp
    src/ide/oauth.cpp
)

set(BASE_SOURCES
    ${CORE_SOURCES}
    ${TUI_SOURCES}
    ${MODEL_SOURCES}
    ${NETWORK_SOURCES}
    ${CRYPTO_SOURCES}
    ${DATABASE_SOURCES}
    ${CLI_SOURCES}
    ${CONFIG_SOURCES}
    ${NODE_SOURCES}
    ${RPC_SOURCES}
    ${PYTHON_SOURCES}
    ${UTILS_SOURCES}
    ${INFRA_SOURCES}
    ${WEB_SOURCES}
    ${QUANTUM_SOURCES}
    ${PRIVACY_SOURCES}
    ${IDE_SOURCES}
)

if(BUILD_PRIVACY)
    add_definitions(-DPRIVACY_ENABLED)
endif()

add_library(synapsed_core STATIC ${BASE_SOURCES})

target_link_libraries(synapsed_core PUBLIC Threads::Threads)
target_link_libraries(synapsed_core PUBLIC OpenSSL::Crypto)
target_link_libraries(synapsed_core PUBLIC ${SODIUM_LIBRARIES})
target_include_directories(synapsed_core PUBLIC ${SODIUM_INCLUDE_DIRS})
if(BUILD_TUI)
    target_link_libraries(synapsed_core PUBLIC ${CURSES_LIBRARIES})
endif()
target_compile_definitions(synapsed_core PUBLIC SYNAPSE_BUILD_TUI=$<BOOL:${BUILD_TUI}>)
target_compile_definitions(synapsed_core PUBLIC SYNAPSE_HAVE_SQLITE3=$<BOOL:${HAS_SQLITE3}>)

if(HAS_SECP256K1)
    target_compile_definitions(synapsed_core PUBLIC SYNAPSE_USE_SECP256K1=1)
    target_link_libraries(synapsed_core PUBLIC secp256k1)
endif()

if(HAS_LIBOQS)
    target_compile_definitions(synapsed_core PUBLIC USE_LIBOQS=1)
    target_link_libraries(synapsed_core PUBLIC ${OQS_TARGET})
    if(liboqs_BINARY_DIR)
        target_include_directories(synapsed_core PUBLIC ${liboqs_BINARY_DIR}/include)
    endif()
    if(liboqs_SOURCE_DIR)
        target_include_directories(synapsed_core PUBLIC ${liboqs_SOURCE_DIR}/src)
    endif()
endif()

if(HAS_SQLITE3)
    target_link_libraries(synapsed_core PUBLIC ${SQLITE3_LIBRARY})
else()
    message(STATUS "SQLite3 support disabled (in-memory DB fallback)")
endif()

if(USE_LLAMA_CPP AND HAS_LLAMA)
    target_compile_definitions(synapsed_core PUBLIC USE_LLAMA_CPP=1)
    target_link_libraries(synapsed_core PUBLIC llama)
endif()

add_executable(synapsed src/main.cpp)
target_link_libraries(synapsed PRIVATE synapsed_core)

add_library(synapsed_lib SHARED src/ide/synapsed_engine.cpp src/ide/synapsed_ffi.cpp)
target_link_libraries(synapsed_lib PRIVATE synapsed_core)
target_compile_definitions(synapsed_lib PRIVATE SYNAPSED_BUILDING_LIB)
set_target_properties(synapsed_lib PROPERTIES
    OUTPUT_NAME "synapsed"
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
if(APPLE)
    set_target_properties(synapsed_lib PROPERTIES SUFFIX ".dylib")
elseif(WIN32)
    set_target_properties(synapsed_lib PROPERTIES SUFFIX ".dll")
endif()
install(TARGETS synapsed_lib DESTINATION lib)
install(FILES include/synapsed_ffi.h DESTINATION include)



# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install
install(TARGETS synapsed DESTINATION bin)
