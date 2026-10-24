#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::sync::Mutex;
use synapsenet_app::commands::{self, EngineState};
use synapsenet_app::navigation;
use tauri::{Url, WebviewWindowBuilder};

static DEV_ORIGIN: once_cell::sync::OnceCell<Option<Url>> = once_cell::sync::OnceCell::new();

fn origin_guard<R: tauri::Runtime>() -> tauri::plugin::TauriPlugin<R> {
    tauri::plugin::Builder::new("origin-guard")
        .on_navigation(|_webview, url| {
            let dev = DEV_ORIGIN.get().and_then(|origin| origin.as_ref());
            navigation::allowed(url, dev)
        })
        .build()
}

fn open_configured_windows<R: tauri::Runtime>(
    app: &mut tauri::App<R>,
) -> Result<(), Box<dyn std::error::Error>> {
    let dev_origin = if cfg!(dev) {
        app.config().build.dev_url.clone()
    } else {
        None
    };
    let _ = DEV_ORIGIN.set(dev_origin.clone());

    for window in app
        .config()
        .app
        .windows
        .iter()
        .filter(|window| !window.create)
    {
        let dev_origin = dev_origin.clone();
        WebviewWindowBuilder::from_config(app.handle(), window)?
            .on_navigation(move |url| navigation::allowed(url, dev_origin.as_ref()))
            .on_new_window(|_url, _features| tauri::webview::NewWindowResponse::Deny)
            .build()?;
    }
    Ok(())
}

fn main() {
    tauri::Builder::default()
        .plugin(origin_guard())
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
            commands::privacy_stealth_generate,
            commands::privacy_stealth_send,
            commands::privacy_ring_sign,
            commands::privacy_ring_verify,
            commands::privacy_view_scan,
            commands::privacy_status,
        ])
        .setup(open_configured_windows)
        .build(tauri::generate_context!())
        .expect("failed to build synapsenet app")
        .run(|_app, event| {
            if matches!(event, tauri::RunEvent::Exit) {
                let _ = synapsenet_app::ffi::shutdown();
            }
        });
}
