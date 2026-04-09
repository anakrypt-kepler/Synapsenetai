#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use synapsenet_app::commands::{self, EngineState};
use std::sync::Mutex;

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .manage(EngineState {
            initialized: Mutex::new(false),
        })
        .invoke_handler(tauri::generate_handler![
            commands::check_first_launch,
            commands::synapsed_init,
            commands::synapsed_shutdown,
            commands::synapsed_rpc_call,
            commands::synapsed_get_status,
            commands::synapsed_subscribe,
            commands::save_setup_config,
            commands::get_system_info,
            commands::wallet_create,
            commands::wallet_restore,
            commands::send_ngt,
            commands::get_transactions,
            commands::submit_knowledge,
            commands::search_knowledge,
            commands::naan_control,
            commands::naan_config_update,
            commands::ai_complete,
            commands::model_load,
            commands::model_unload,
            commands::update_settings,
            commands::check_updates,
            commands::poe_submit_code,
        ])
        .run(tauri::generate_context!())
        .expect("failed to run synapsenet app");
}
