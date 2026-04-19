use crate::ffi;
use serde::{Deserialize, Serialize};
use std::ffi::CStr;
use std::os::raw::c_char;
use std::path::PathBuf;
use std::sync::Mutex;
use tauri::{AppHandle, Emitter, State};

pub struct EngineState {
    pub initialized: Mutex<bool>,
}

fn config_dir() -> PathBuf {
    dirs::home_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join(".synapsenet")
}

fn config_path() -> PathBuf {
    config_dir().join("config.toml")
}

#[tauri::command]
pub fn check_first_launch() -> Result<bool, String> {
    Ok(!config_path().exists())
}

#[tauri::command]
pub fn synapsed_init(state: State<'_, EngineState>) -> Result<(), String> {
    let mut initialized = state
        .initialized
        .lock()
        .map_err(|e| format!("lock error: {}", e))?;

    if *initialized {
        return Ok(());
    }

    ffi::load()?;

    let cfg = config_path();
    let cfg_str = cfg.to_string_lossy().to_string();
    ffi::init(&cfg_str)?;

    *initialized = true;
    Ok(())
}

#[tauri::command]
pub fn synapsed_shutdown(state: State<'_, EngineState>) -> Result<(), String> {
    let mut initialized = state
        .initialized
        .lock()
        .map_err(|e| format!("lock error: {}", e))?;

    if !*initialized {
        return Ok(());
    }

    ffi::shutdown()?;
    *initialized = false;
    Ok(())
}

#[tauri::command]
pub fn synapsed_rpc_call(method: String, params: String) -> Result<String, String> {
    ffi::rpc_call(&method, &params)
}

#[tauri::command]
pub fn synapsed_get_status() -> Result<String, String> {
    ffi::get_status()
}

static EVENT_APP_HANDLE: once_cell::sync::OnceCell<AppHandle> = once_cell::sync::OnceCell::new();

unsafe extern "C" fn event_trampoline(event_type: *const c_char, payload: *const c_char) {
    if let Some(handle) = EVENT_APP_HANDLE.get() {
        let etype = if event_type.is_null() {
            String::new()
        } else {
            CStr::from_ptr(event_type)
                .to_string_lossy()
                .into_owned()
        };
        let data = if payload.is_null() {
            String::new()
        } else {
            CStr::from_ptr(payload).to_string_lossy().into_owned()
        };

        #[derive(Clone, Serialize)]
        struct SynapsedEvent {
            event_type: String,
            payload: String,
        }

        let _ = handle.emit(
            &format!("synapsed:{}", etype),
            SynapsedEvent {
                event_type: etype,
                payload: data,
            },
        );
    }
}

#[tauri::command]
pub fn synapsed_subscribe(app: AppHandle, event_type: String) -> Result<(), String> {
    let _ = EVENT_APP_HANDLE.set(app);
    ffi::subscribe(&event_type, event_trampoline)
}

#[derive(Serialize, Deserialize)]
pub struct SetupConfig {
    pub wallet_mode: String,
    pub seed_phrase: Option<String>,
    pub password: Option<String>,
    pub connection_type: String,
    pub bridge_lines: Option<String>,
    pub ai_model: String,
    pub model_path: Option<String>,
    pub cpu_threads: u32,
    pub ram_limit_mb: u64,
    pub disk_limit_mb: u64,
    pub gpu_enabled: bool,
    pub gpu_device: Option<String>,
    pub gpu_layers: u32,
    pub launch_at_startup: bool,
    pub mine_background: bool,
}

#[tauri::command]
pub fn save_setup_config(config: SetupConfig) -> Result<(), String> {
    let dir = config_dir();
    std::fs::create_dir_all(&dir).map_err(|e| format!("failed to create config dir: {}", e))?;

    let mut content = String::new();
    content.push_str("[wallet]\n");
    content.push_str(&format!("mode = \"{}\"\n", config.wallet_mode));
    if let Some(ref pwd) = config.password {
        if !pwd.is_empty() {
            content.push_str("password_protected = true\n");
        }
    }
    content.push_str("\n[connection]\n");
    content.push_str(&format!("type = \"{}\"\n", config.connection_type));
    if let Some(ref bridges) = config.bridge_lines {
        if !bridges.is_empty() {
            content.push_str(&format!("bridge_lines = \"\"\"\n{}\n\"\"\"\n", bridges));
        }
    }
    content.push_str("\n[ai]\n");
    content.push_str(&format!("mode = \"{}\"\n", config.ai_model));
    if let Some(ref path) = config.model_path {
        if !path.is_empty() {
            content.push_str(&format!("model_path = \"{}\"\n", path));
        }
    }
    content.push_str("\n[resources]\n");
    content.push_str(&format!("cpu_threads = {}\n", config.cpu_threads));
    content.push_str(&format!("ram_limit_mb = {}\n", config.ram_limit_mb));
    content.push_str(&format!("disk_limit_mb = {}\n", config.disk_limit_mb));
    content.push_str(&format!("gpu_enabled = {}\n", config.gpu_enabled));
    if let Some(ref device) = config.gpu_device {
        if !device.is_empty() {
            content.push_str(&format!("gpu_device = \"{}\"\n", device));
        }
    }
    content.push_str(&format!("gpu_layers = {}\n", config.gpu_layers));
    content.push_str("\n[startup]\n");
    content.push_str(&format!("launch_at_login = {}\n", config.launch_at_startup));
    content.push_str(&format!("mine_background = {}\n", config.mine_background));

    std::fs::write(config_path(), content)
        .map_err(|e| format!("failed to write config: {}", e))?;

    Ok(())
}

#[derive(Serialize)]
pub struct GpuDevice {
    pub id: String,
    pub name: String,
    pub vram_mb: u64,
}

#[derive(Serialize)]
pub struct SystemInfo {
    pub cpu_cores: usize,
    pub ram_total_mb: u64,
    pub gpu_devices: Vec<GpuDevice>,
}

#[tauri::command]
pub fn get_system_info() -> Result<SystemInfo, String> {
    let cores = std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(4);

    let ram = sys_ram_mb();
    let gpu_devices = detect_gpu_devices();

    Ok(SystemInfo {
        cpu_cores: cores,
        ram_total_mb: ram,
        gpu_devices,
    })
}

fn detect_gpu_devices() -> Vec<GpuDevice> {
    let mut devices = Vec::new();

    #[cfg(target_os = "linux")]
    {
        if let Ok(entries) = std::fs::read_dir("/sys/class/drm") {
            for entry in entries.flatten() {
                let name = entry.file_name().to_string_lossy().to_string();
                if !name.starts_with("card") || name.contains('-') {
                    continue;
                }
                let device_path = entry.path().join("device");
                let vendor_path = device_path.join("vendor");
                let gpu_name = if vendor_path.exists() {
                    let vendor = std::fs::read_to_string(&vendor_path)
                        .unwrap_or_default()
                        .trim()
                        .to_string();
                    match vendor.as_str() {
                        "0x10de" => format!("NVIDIA GPU ({})", name),
                        "0x1002" => format!("AMD GPU ({})", name),
                        "0x8086" => format!("Intel GPU ({})", name),
                        _ => format!("GPU ({})", name),
                    }
                } else {
                    format!("GPU ({})", name)
                };

                let vram = detect_gpu_vram(&device_path);

                devices.push(GpuDevice {
                    id: name,
                    name: gpu_name,
                    vram_mb: vram,
                });
            }
        }

        if devices.is_empty() {
            if let Ok(output) = std::process::Command::new("nvidia-smi")
                .args(["--query-gpu=index,name,memory.total", "--format=csv,noheader,nounits"])
                .output()
            {
                if output.status.success() {
                    let stdout = String::from_utf8_lossy(&output.stdout);
                    for line in stdout.lines() {
                        let parts: Vec<&str> = line.split(',').map(|s| s.trim()).collect();
                        if parts.len() >= 3 {
                            let vram = parts[2].parse::<u64>().unwrap_or(0);
                            devices.push(GpuDevice {
                                id: format!("nvidia:{}", parts[0]),
                                name: parts[1].to_string(),
                                vram_mb: vram,
                            });
                        }
                    }
                }
            }
        }
    }

    #[cfg(target_os = "macos")]
    {
        if let Ok(output) = std::process::Command::new("system_profiler")
            .args(["SPDisplaysDataType", "-json"])
            .output()
        {
            if output.status.success() {
                let stdout = String::from_utf8_lossy(&output.stdout);
                if let Ok(parsed) = serde_json::from_str::<serde_json::Value>(&stdout) {
                    if let Some(displays) = parsed["SPDisplaysDataType"].as_array() {
                        for (i, gpu) in displays.iter().enumerate() {
                            let name = gpu["sppci_model"]
                                .as_str()
                                .unwrap_or("Unknown GPU")
                                .to_string();
                            let vram_str = gpu["spdisplays_vram"]
                                .as_str()
                                .unwrap_or("0");
                            let vram = vram_str
                                .split_whitespace()
                                .next()
                                .and_then(|v| v.parse::<u64>().ok())
                                .unwrap_or(0)
                                * 1024;
                            devices.push(GpuDevice {
                                id: format!("gpu:{}", i),
                                name,
                                vram_mb: vram,
                            });
                        }
                    }
                }
            }
        }
    }

    #[cfg(target_os = "windows")]
    {
        if let Ok(output) = std::process::Command::new("wmic")
            .args(["path", "win32_VideoController", "get", "Name,AdapterRAM", "/format:csv"])
            .output()
        {
            if output.status.success() {
                let stdout = String::from_utf8_lossy(&output.stdout);
                for (i, line) in stdout.lines().skip(2).enumerate() {
                    let parts: Vec<&str> = line.split(',').collect();
                    if parts.len() >= 3 {
                        let vram_bytes = parts[1].trim().parse::<u64>().unwrap_or(0);
                        devices.push(GpuDevice {
                            id: format!("gpu:{}", i),
                            name: parts[2].trim().to_string(),
                            vram_mb: vram_bytes / (1024 * 1024),
                        });
                    }
                }
            }
        }
    }

    devices
}

#[cfg(target_os = "linux")]
fn detect_gpu_vram(device_path: &std::path::Path) -> u64 {
    let mem_path = device_path.join("mem_info_vram_total");
    if mem_path.exists() {
        if let Ok(val) = std::fs::read_to_string(&mem_path) {
            return val.trim().parse::<u64>().unwrap_or(0) / (1024 * 1024);
        }
    }
    let resource_path = device_path.join("resource");
    if resource_path.exists() {
        if let Ok(content) = std::fs::read_to_string(&resource_path) {
            if let Some(line) = content.lines().next() {
                let parts: Vec<&str> = line.split_whitespace().collect();
                if parts.len() >= 2 {
                    let start = u64::from_str_radix(parts[0].trim_start_matches("0x"), 16).unwrap_or(0);
                    let end = u64::from_str_radix(parts[1].trim_start_matches("0x"), 16).unwrap_or(0);
                    if end > start {
                        return (end - start) / (1024 * 1024);
                    }
                }
            }
        }
    }
    0
}

#[cfg(not(target_os = "linux"))]
fn detect_gpu_vram(_device_path: &std::path::Path) -> u64 {
    0
}

fn sys_ram_mb() -> u64 {
    #[cfg(target_os = "linux")]
    {
        if let Ok(info) = std::fs::read_to_string("/proc/meminfo") {
            for line in info.lines() {
                if line.starts_with("MemTotal:") {
                    let parts: Vec<&str> = line.split_whitespace().collect();
                    if parts.len() >= 2 {
                        if let Ok(kb) = parts[1].parse::<u64>() {
                            return kb / 1024;
                        }
                    }
                }
            }
        }
        8192
    }
    #[cfg(not(target_os = "linux"))]
    {
        8192
    }
}

#[tauri::command]
pub fn wallet_create() -> Result<String, String> {
    ffi::rpc_call("wallet.create", "{}")
}

#[tauri::command]
pub fn wallet_restore(seed_phrase: String) -> Result<String, String> {
    let params = serde_json::json!({ "seed": seed_phrase }).to_string();
    ffi::rpc_call("wallet.restore", &params)
}

#[tauri::command]
pub fn send_ngt(recipient: String, amount: String, memo: Option<String>) -> Result<String, String> {
    let params = serde_json::json!({
        "recipient": recipient,
        "amount": amount,
        "memo": memo.unwrap_or_default()
    })
    .to_string();
    ffi::rpc_call("transfer.send", &params)
}

#[tauri::command]
pub fn get_transactions(filter: Option<String>) -> Result<String, String> {
    let params = serde_json::json!({
        "filter": filter.unwrap_or_else(|| "all".to_string())
    })
    .to_string();
    ffi::rpc_call("transfer.history", &params)
}

#[tauri::command]
pub fn submit_knowledge(title: String, content: String, citations: String) -> Result<String, String> {
    let params = serde_json::json!({
        "title": title,
        "content": content,
        "citations": citations
    })
    .to_string();
    ffi::rpc_call("knowledge.submit", &params)
}

#[tauri::command]
pub fn search_knowledge(query: String) -> Result<String, String> {
    let params = serde_json::json!({ "query": query }).to_string();
    ffi::rpc_call("knowledge.search", &params)
}

#[tauri::command]
pub fn naan_control(action: String) -> Result<String, String> {
    let params = serde_json::json!({ "action": action }).to_string();
    ffi::rpc_call("naan.control", &params)
}

#[tauri::command]
pub fn naan_config_update(config: String) -> Result<String, String> {
    ffi::rpc_call("naan.config", &config)
}

#[tauri::command]
pub fn ai_complete(prompt: String) -> Result<String, String> {
    let params = serde_json::json!({ "prompt": prompt }).to_string();
    ffi::rpc_call("ai.complete", &params)
}

#[tauri::command]
pub fn model_load(path: String) -> Result<String, String> {
    let params = serde_json::json!({ "path": path }).to_string();
    ffi::rpc_call("model.load", &params)
}

#[tauri::command]
pub fn model_unload() -> Result<String, String> {
    ffi::rpc_call("model.unload", "{}")
}

#[tauri::command]
pub fn update_settings(settings: String) -> Result<String, String> {
    ffi::rpc_call("settings.update", &settings)
}

#[tauri::command]
pub fn check_updates() -> Result<String, String> {
    ffi::rpc_call("update.check", "{}")
}

#[tauri::command]
pub fn poe_submit_code(patch: String) -> Result<String, String> {
    let params = serde_json::json!({ "patch": patch }).to_string();
    ffi::rpc_call("poe.submit_code", &params)
}
