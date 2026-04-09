cmake_minimum_required(VERSION 3.16)

add_executable(test_crypto test_crypto.cpp)
target_link_libraries(test_crypto synapsed_core Threads::Threads)
add_test(NAME CryptoTests COMMAND test_crypto)

add_executable(test_crypto_secp test_crypto_secp.cpp)
target_link_libraries(test_crypto_secp synapsed_core)
add_test(NAME CryptoSecpTests COMMAND test_crypto_secp)

add_executable(test_network test_network.cpp)
target_link_libraries(test_network synapsed_core Threads::Threads)
add_test(NAME NetworkTests COMMAND test_network)

add_executable(test_ledger test_ledger.cpp)
target_link_libraries(test_ledger synapsed_core Threads::Threads)
add_test(NAME LedgerTests COMMAND test_ledger)

add_executable(test_poe test_poe.cpp)
target_link_libraries(test_poe synapsed_core)
add_test(NAME PoETests COMMAND test_poe)

add_executable(test_poe_e2e test_poe_e2e.cpp)
target_link_libraries(test_poe_e2e synapsed_core)
add_test(NAME PoeE2ETests COMMAND test_poe_e2e)

add_executable(test_poe_runtime test_poe_runtime.cpp)
target_link_libraries(test_poe_runtime synapsed_core)
add_test(NAME PoeRuntimeTests COMMAND test_poe_runtime)

add_executable(test_knowledge_antispam test_knowledge_antispam.cpp)
target_link_libraries(test_knowledge_antispam synapsed_core)
add_test(NAME KnowledgeAntispamTests COMMAND test_knowledge_antispam)

add_executable(test_network_soak test_network_soak.cpp)
target_link_libraries(test_network_soak synapsed_core Threads::Threads)
add_test(NAME NetworkSoakTests COMMAND test_network_soak)

add_executable(test_privacy test_privacy.cpp)
target_link_libraries(test_privacy synapsed_core Threads::Threads)
add_test(NAME PrivacyTests COMMAND test_privacy)

add_executable(test_quantum test_quantum.cpp)
target_link_libraries(test_quantum synapsed_core Threads::Threads)
add_test(NAME QuantumTests COMMAND test_quantum)

add_executable(test_quantum_runtime test_quantum_runtime.cpp)
target_link_libraries(test_quantum_runtime synapsed_core)
add_test(NAME QuantumRuntimeTests COMMAND test_quantum_runtime)

add_executable(test_model test_model.cpp)
target_link_libraries(test_model synapsed_core Threads::Threads)
add_test(NAME ModelTests COMMAND test_model)

add_executable(test_wallet test_wallet.cpp)
target_link_libraries(test_wallet synapsed_core Threads::Threads)
add_test(NAME WalletTests COMMAND test_wallet)

add_executable(test_wallet_crypto_runtime test_wallet_crypto_runtime.cpp)
target_link_libraries(test_wallet_crypto_runtime synapsed_core Threads::Threads)
add_test(NAME WalletCryptoRuntimeTests COMMAND test_wallet_crypto_runtime)

add_executable(test_poe_bootstrap_policy test_poe_bootstrap_policy.cpp)
target_link_libraries(test_poe_bootstrap_policy synapsed_core)
add_test(NAME PoeBootstrapPolicyTests COMMAND test_poe_bootstrap_policy)

add_executable(test_transfer test_transfer.cpp)
target_link_libraries(test_transfer synapsed_core)
add_test(NAME TransferTests COMMAND test_transfer)

if(NOT WIN32)
    add_executable(test_daemon_shutdown test_daemon_shutdown.cpp)
    add_test(NAME DaemonShutdownTests COMMAND test_daemon_shutdown $<TARGET_FILE:synapsed>)
endif()

add_executable(test_update_manifest test_update_manifest.cpp)
target_link_libraries(test_update_manifest synapsed_core)
add_test(NAME UpdateManifestTests COMMAND test_update_manifest)

add_executable(test_update_installer test_update_installer.cpp)
target_link_libraries(test_update_installer synapsed_core)
add_test(NAME UpdateInstallerTests COMMAND test_update_installer)

add_executable(test_agent_runtime test_agent_runtime.cpp)
target_link_libraries(test_agent_runtime synapsed_core)
add_test(NAME AgentRuntimeTests COMMAND test_agent_runtime)

add_executable(test_agent_scheduler test_agent_scheduler.cpp)
target_link_libraries(test_agent_scheduler synapsed_core)
add_test(NAME AgentSchedulerTests COMMAND test_agent_scheduler)

add_executable(test_agent_coordination test_agent_coordination.cpp)
target_link_libraries(test_agent_coordination synapsed_core)
add_test(NAME AgentCoordinationTests COMMAND test_agent_coordination)

add_executable(test_agent_draft_queue test_agent_draft_queue.cpp)
target_link_libraries(test_agent_draft_queue synapsed_core)
add_test(NAME AgentDraftQueueTests COMMAND test_agent_draft_queue)

add_executable(test_agent_submission_pipeline test_agent_submission_pipeline.cpp)
target_link_libraries(test_agent_submission_pipeline synapsed_core)
add_test(NAME AgentSubmissionPipelineTests COMMAND test_agent_submission_pipeline)

add_executable(test_agent_score test_agent_score.cpp)
target_link_libraries(test_agent_score synapsed_core)
add_test(NAME AgentScoreTests COMMAND test_agent_score)

add_executable(test_state_manager test_state_manager.cpp)
target_link_libraries(test_state_manager synapsed_core)
add_test(NAME StateManagerTests COMMAND test_state_manager)

add_executable(test_agent_storage test_agent_storage.cpp)
target_link_libraries(test_agent_storage synapsed_core)
add_test(NAME AgentStorageTests COMMAND test_agent_storage)

add_executable(test_determinism_replay test_determinism_replay.cpp)
target_link_libraries(test_determinism_replay synapsed_core)
add_test(NAME DeterminismReplayTests COMMAND test_determinism_replay)

add_executable(test_naan_integration test_naan_integration.cpp)
target_link_libraries(test_naan_integration synapsed_core)
add_test(NAME NaanIntegrationTests COMMAND test_naan_integration)

add_executable(test_naan_recovery test_naan_recovery.cpp)
target_link_libraries(test_naan_recovery synapsed_core)
add_test(NAME NaanRecoveryTests COMMAND test_naan_recovery)

add_executable(test_naan_adversarial test_naan_adversarial.cpp)
target_link_libraries(test_naan_adversarial synapsed_core)
add_test(NAME NaanAdversarialTests COMMAND test_naan_adversarial)

add_executable(test_web_route_policy test_web_route_policy.cpp)
target_link_libraries(test_web_route_policy synapsed_core)
add_test(NAME WebRoutePolicyTests COMMAND test_web_route_policy)

add_executable(test_web_connector_health test_web_connector_health.cpp)
target_link_libraries(test_web_connector_health synapsed_core)
add_test(NAME WebConnectorHealthTests COMMAND test_web_connector_health)

add_executable(test_web_extraction_risk test_web_extraction_risk.cpp)
target_link_libraries(test_web_extraction_risk synapsed_core)
add_test(NAME WebExtractionRiskTests COMMAND test_web_extraction_risk)

add_executable(test_web_connector_audit test_web_connector_audit.cpp)
target_link_libraries(test_web_connector_audit synapsed_core)
add_test(NAME WebConnectorAuditTests COMMAND test_web_connector_audit)

add_executable(test_rpc_server test_rpc_server.cpp)
target_link_libraries(test_rpc_server synapsed_core Threads::Threads)
add_test(NAME RpcServerTests COMMAND test_rpc_server)

add_executable(test_web_connector_abuse test_web_connector_abuse.cpp)
target_link_libraries(test_web_connector_abuse synapsed_core)
add_test(NAME WebConnectorAbuseTests COMMAND test_web_connector_abuse)

add_executable(test_implant_compatibility test_implant_compatibility.cpp)
target_link_libraries(test_implant_compatibility synapsed_core)
add_test(NAME ImplantCompatibilityTests COMMAND test_implant_compatibility)

add_executable(test_implant_safety_pipeline test_implant_safety_pipeline.cpp)
target_link_libraries(test_implant_safety_pipeline synapsed_core)
add_test(NAME ImplantSafetyPipelineTests COMMAND test_implant_safety_pipeline)

add_executable(test_consensus test_consensus.cpp)
target_link_libraries(test_consensus synapsed_core)
add_test(NAME ConsensusTests COMMAND test_consensus)

add_executable(test_peer_discovery test_peer_discovery.cpp)
target_link_libraries(test_peer_discovery synapsed_core)
add_test(NAME PeerDiscoveryTests COMMAND test_peer_discovery)

add_executable(test_network_message test_network_message.cpp)
target_link_libraries(test_network_message synapsed_core)
add_test(NAME NetworkMessageTests COMMAND test_network_message)

add_executable(test_network_scale_policy test_network_scale_policy.cpp)
target_link_libraries(test_network_scale_policy synapsed_core)
add_test(NAME NetworkScalePolicyTests COMMAND test_network_scale_policy)

add_executable(test_ledger_recovery test_ledger_recovery.cpp)
target_link_libraries(test_ledger_recovery synapsed_core)
add_test(NAME LedgerRecoveryTests COMMAND test_ledger_recovery)

add_executable(test_remote_model test_remote_model.cpp)
target_link_libraries(test_remote_model synapsed_core)
add_test(NAME RemoteModelTests COMMAND test_remote_model)

add_executable(test_marketplace test_marketplace.cpp)
target_link_libraries(test_marketplace synapsed_core)
add_test(NAME MarketplaceTests COMMAND test_marketplace)

add_executable(test_primary_ui_navigation test_primary_ui_navigation.cpp)
target_link_libraries(test_primary_ui_navigation synapsed_core)
add_test(NAME PrimaryUINavigationTests COMMAND test_primary_ui_navigation)

add_executable(test_primary_ui_chat_status test_primary_ui_chat_status.cpp)
target_link_libraries(test_primary_ui_chat_status synapsed_core)
add_test(NAME PrimaryUIChatStatusTests COMMAND test_primary_ui_chat_status)

add_executable(test_primary_ui_snapshot test_primary_ui_snapshot.cpp)
target_link_libraries(test_primary_ui_snapshot synapsed_core)
add_test(NAME PrimaryUISnapshotTests COMMAND test_primary_ui_snapshot)

add_executable(test_tui_peers test_tui_peers.cpp)
target_link_libraries(test_tui_peers synapsed_core)
add_test(NAME TUIPeersTests COMMAND test_tui_peers)

add_executable(test_tor_route_policy test_tor_route_policy.cpp)
target_link_libraries(test_tor_route_policy synapsed_core)
add_test(NAME TorRoutePolicyTests COMMAND test_tor_route_policy)

add_executable(test_tor_process_guard test_tor_process_guard.cpp)
target_link_libraries(test_tor_process_guard synapsed_core)
add_test(NAME TorProcessGuardTests COMMAND test_tor_process_guard)

add_executable(test_tor_bridge_utils test_tor_bridge_utils.cpp)
target_link_libraries(test_tor_bridge_utils synapsed_core)
add_test(NAME TorBridgeUtilsTests COMMAND test_tor_bridge_utils)

add_executable(test_tor_bridge_provider test_tor_bridge_provider.cpp)
target_link_libraries(test_tor_bridge_provider synapsed_core)
add_test(NAME TorBridgeProviderTests COMMAND test_tor_bridge_provider)

add_executable(test_tor_external_path test_tor_external_path.cpp)
target_link_libraries(test_tor_external_path synapsed_core)
add_test(NAME TorExternalPathTests COMMAND test_tor_external_path)

add_executable(test_tor_managed_startup_path test_tor_managed_startup_path.cpp)
target_link_libraries(test_tor_managed_startup_path synapsed_core)
add_test(NAME TorManagedStartupPathTests COMMAND test_tor_managed_startup_path)

add_executable(test_tor_onion_service_policy test_tor_onion_service_policy.cpp)
target_link_libraries(test_tor_onion_service_policy synapsed_core)
add_test(NAME TorOnionServicePolicyTests COMMAND test_tor_onion_service_policy)

add_executable(test_tor_peer_identity test_tor_peer_identity.cpp)
target_link_libraries(test_tor_peer_identity synapsed_core)
add_test(NAME TorPeerIdentityTests COMMAND test_tor_peer_identity)

# Task 5: ModelLoader Unit Tests
if(GTEST_FOUND)
    add_executable(test_model_loader test_model_loader.cpp)
    target_include_directories(test_model_loader PRIVATE ${GTEST_INCLUDE_DIRS})
    target_link_libraries(test_model_loader 
        Threads::Threads 
        synapsed_core
        ${GTEST_LIBRARIES}
    )
    add_test(NAME ModelLoaderTests COMMAND test_model_loader)
else()
    message(WARNING "GoogleTest not found - skipping test_model_loader tests")
endif()

add_executable(test_protocol_fuzz test_protocol_fuzz.cpp)
target_link_libraries(test_protocol_fuzz synapsed_core)
add_test(NAME ProtocolFuzzTests COMMAND test_protocol_fuzz)

add_executable(test_tor_no_clearnet_leak test_tor_no_clearnet_leak.cpp)
target_link_libraries(test_tor_no_clearnet_leak synapsed_core Threads::Threads)
add_test(NAME TorNoClearnetLeakTests COMMAND test_tor_no_clearnet_leak)

add_executable(test_bip39_vectors test_bip39_vectors.cpp)
target_link_libraries(test_bip39_vectors synapsed_core)
add_test(NAME Bip39VectorsTests COMMAND test_bip39_vectors)

add_executable(test_socks5_timeout test_socks5_timeout.cpp)
target_link_libraries(test_socks5_timeout synapsed_core Threads::Threads)
add_test(NAME Socks5TimeoutTests COMMAND test_socks5_timeout)

add_executable(test_no_secret_logging test_no_secret_logging.cpp)
target_link_libraries(test_no_secret_logging synapsed_core)
add_test(NAME NoSecretLoggingTests COMMAND test_no_secret_logging)

add_executable(test_config test_config.cpp)
target_link_libraries(test_config synapsed_core)
add_test(NAME ConfigTests COMMAND test_config)

add_executable(test_pqc_ci_validation test_pqc_ci_validation.cpp)
target_link_libraries(test_pqc_ci_validation synapsed_core)
add_test(NAME PqcCiValidationTests COMMAND test_pqc_ci_validation)

add_executable(test_ide_agent test_ide_agent.cpp)
target_link_libraries(test_ide_agent synapsed_core)
add_test(NAME IdeAgentTests COMMAND test_ide_agent)

add_executable(test_ide_session test_ide_session.cpp)
target_link_libraries(test_ide_session synapsed_core)
add_test(NAME IdeSessionTests COMMAND test_ide_session)

add_executable(test_ide_lsp test_ide_lsp.cpp)
target_link_libraries(test_ide_lsp synapsed_core)
add_test(NAME IdeLspTests COMMAND test_ide_lsp)

add_executable(test_ffi_lifecycle test_ffi_lifecycle.cpp)
target_link_libraries(test_ffi_lifecycle synapsed_lib)
add_test(NAME FfiLifecycleTests COMMAND test_ffi_lifecycle)

set(SYNAPSENET_TEST_TARGETS
    test_crypto
    test_crypto_secp
    test_network
    test_ledger
    test_poe
    test_privacy
    test_quantum
    test_quantum_runtime
    test_model
    test_wallet
    test_transfer
    test_update_manifest
    test_update_installer
    test_agent_runtime
    test_agent_scheduler
    test_agent_coordination
    test_agent_draft_queue
    test_agent_submission_pipeline
    test_agent_score
    test_agent_storage
    test_determinism_replay
    test_naan_integration
    test_naan_recovery
    test_naan_adversarial
    test_web_route_policy
    test_web_connector_health
    test_web_extraction_risk
    test_web_connector_audit
    test_web_connector_abuse
    test_implant_compatibility
    test_implant_safety_pipeline
    test_consensus
    test_peer_discovery
    test_network_message
    test_network_scale_policy
    test_ledger_recovery
    test_remote_model
    test_marketplace
    test_primary_ui_navigation
    test_primary_ui_chat_status
    test_primary_ui_snapshot
    test_state_manager
    test_tor_route_policy
    test_tor_process_guard
    test_tor_bridge_utils
    test_tor_bridge_provider
    test_tor_external_path
    test_tor_managed_startup_path
    test_tor_onion_service_policy
    test_tor_peer_identity
    test_protocol_fuzz
    test_tor_no_clearnet_leak
    test_bip39_vectors
    test_socks5_timeout
    test_no_secret_logging
    test_config
    test_pqc_ci_validation
    test_poe_e2e
    test_knowledge_antispam
    test_network_soak
    test_model_loader
    test_ide_agent
    test_ide_session
    test_ide_lsp
    test_ffi_lifecycle
)

foreach(TEST_TARGET IN LISTS SYNAPSENET_TEST_TARGETS)
    if(TARGET ${TEST_TARGET})
        if(MSVC)
            target_compile_options(${TEST_TARGET} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${TEST_TARGET} PRIVATE -UNDEBUG)
        endif()
    endif()
endforeach()
