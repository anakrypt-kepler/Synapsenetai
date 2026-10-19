use crate::ffi;
use serde::{Deserialize, Serialize};
use std::ffi::CStr;
use std::os::raw::c_char;
use std::path::PathBuf;
use std::sync::Mutex;
use tauri::{AppHandle, Emitter, State};

// Tauri commands the Svelte UI invokes. Most of them forward into libsynapsed.
// Config lives in ~/.synapsenet/config.toml — missing file = first-launch wizard.

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
pub async fn synapsed_rpc_call(method: String, params: String) -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(move || ffi::rpc_call(&method, &params))
        .await
        .map_err(|e| format!("rpc worker: {e}"))?
}

#[tauri::command]
pub async fn synapsed_get_status() -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(ffi::get_status)
        .await
        .map_err(|e| format!("status worker: {e}"))?
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
    let conn = if config.connection_type == "tor_bridges" {
        "tor_bridges"
    } else {
        "tor"
    };
    content.push_str(&format!("type = \"{}\"\n", conn));
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

    // The engine reads settings.json, not the wizard toml.
    let settings = serde_json::json!({
        "connection_type": conn,
        "bridge_lines": config.bridge_lines.clone().unwrap_or_default(),
        "ram_limit_mb": config.ram_limit_mb,
        "cpu_threads": config.cpu_threads,
        "disk_limit_mb": config.disk_limit_mb,
        "gpu_enabled": config.gpu_enabled,
        "gpu_layers": config.gpu_layers,
        "naan_enabled": false,
    });
    std::fs::write(dir.join("settings.json"), settings.to_string())
        .map_err(|e| format!("failed to write settings: {}", e))?;

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
pub async fn get_system_info() -> Result<SystemInfo, String> {
    // Registry and memory queries stay off the UI thread so SET can scroll while they run.
    tauri::async_runtime::spawn_blocking(|| {
        let cores = std::thread::available_parallelism()
            .map(|n| n.get())
            .unwrap_or(4);
        Ok(SystemInfo {
            cpu_cores: cores,
            ram_total_mb: sys_ram_mb(),
            gpu_devices: detect_gpu_devices(),
        })
    })
    .await
    .map_err(|e| format!("system info: {e}"))?
}

#[cfg(not(target_os = "windows"))]
fn nvidia_smi_gpus() -> Vec<GpuDevice> {
    let bins = [
        "nvidia-smi",
        "/usr/bin/nvidia-smi",
        "/usr/local/bin/nvidia-smi",
        "/usr/local/cuda/bin/nvidia-smi",
    ];
    for bin in bins {
        let Ok(output) = std::process::Command::new(bin)
            .args([
                "--query-gpu=index,name,memory.total",
                "--format=csv,noheader,nounits",
            ])
            .output()
        else {
            continue;
        };
        if !output.status.success() {
            continue;
        }
        let stdout = String::from_utf8_lossy(&output.stdout);
        let mut devices = Vec::new();
        for line in stdout.lines() {
            let parts: Vec<&str> = line.split(',').map(|s| s.trim()).collect();
            if parts.len() >= 2 {
                let vram = if parts.len() >= 3 {
                    parts[2].parse::<u64>().unwrap_or(0)
                } else {
                    0
                };
                devices.push(GpuDevice {
                    id: format!("nvidia:{}", parts[0]),
                    name: parts[1].to_string(),
                    vram_mb: vram,
                });
            }
        }
        if !devices.is_empty() {
            return devices;
        }
    }
    Vec::new()
}

fn detect_gpu_devices() -> Vec<GpuDevice> {
    // wmic and nvidia-smi are console programs. A GUI parent flashes a black
    // window for each of them. Read the display-class registry instead.
    #[cfg(target_os = "windows")]
    {
        return windows_gpu_devices();
    }
    #[cfg(not(target_os = "windows"))]
    {
    let mut devices = nvidia_smi_gpus();
    let has_nvidia = !devices.is_empty();

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
                let vendor = std::fs::read_to_string(&vendor_path)
                    .unwrap_or_default()
                    .trim()
                    .to_string();
                if has_nvidia && vendor == "0x10de" {
                    continue;
                }
                let gpu_name = match vendor.as_str() {
                    "0x10de" => format!("NVIDIA GPU ({})", name),
                    "0x1002" => format!("AMD GPU ({})", name),
                    "0x8086" => format!("Intel GPU ({})", name),
                    "" => format!("GPU ({})", name),
                    _ => format!("GPU ({})", name),
                };

                let vram = detect_gpu_vram(&device_path);

                devices.push(GpuDevice {
                    id: name,
                    name: gpu_name,
                    vram_mb: vram,
                });
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

    devices
    }
}

#[cfg(target_os = "windows")]
fn windows_gpu_devices() -> Vec<GpuDevice> {
    #[link(name = "advapi32")]
    extern "system" {
        fn RegOpenKeyExW(key: isize, sub: *const u16, opts: u32, sam: u32, out: *mut isize) -> i32;
        fn RegCloseKey(key: isize) -> i32;
        fn RegEnumKeyExW(
            key: isize,
            index: u32,
            name: *mut u16,
            name_len: *mut u32,
            reserved: *mut u32,
            class: *mut u16,
            class_len: *mut u32,
            last_write: *mut u8,
        ) -> i32;
        fn RegGetValueW(
            key: isize,
            sub: *const u16,
            value: *const u16,
            flags: u32,
            typ: *mut u32,
            data: *mut u8,
            data_len: *mut u32,
        ) -> i32;
    }

    // Sign-extend. 0x80000002 as usize is not HKEY_LOCAL_MACHINE on 64-bit.
    const HKEY_LOCAL_MACHINE: isize = 0x8000_0002u32 as i32 as isize;
    const KEY_READ: u32 = 0x20019;
    const ERROR_NO_MORE_ITEMS: i32 = 259;
    const RRF_RT_REG_SZ: u32 = 0x2;
    const RRF_RT_REG_QWORD: u32 = 0x40;

    fn wide(s: &str) -> Vec<u16> {
        s.encode_utf16().chain(std::iter::once(0)).collect()
    }

    let class_path = wide(
        "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
    );
    let mut class_key: isize = 0;
    unsafe {
        if RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            class_path.as_ptr(),
            0,
            KEY_READ,
            &mut class_key,
        ) != 0
        {
            return Vec::new();
        }
    }

    let mut devices = Vec::new();
    let mut index = 0u32;
    loop {
        let mut name = [0u16; 64];
        let mut name_len = name.len() as u32;
        let rc = unsafe {
            RegEnumKeyExW(
                class_key,
                index,
                name.as_mut_ptr(),
                &mut name_len,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        };
        if rc == ERROR_NO_MORE_ITEMS || rc != 0 {
            break;
        }
        index += 1;
        let subname = String::from_utf16_lossy(&name[..name_len as usize]);
        if !subname.chars().all(|c| c.is_ascii_digit()) {
            continue;
        }

        let desc_value = wide("DriverDesc");
        let mut desc_buf = [0u16; 256];
        let mut desc_bytes = (desc_buf.len() * 2) as u32;
        let desc_ok = unsafe {
            RegGetValueW(
                class_key,
                name.as_ptr(),
                desc_value.as_ptr(),
                RRF_RT_REG_SZ,
                std::ptr::null_mut(),
                desc_buf.as_mut_ptr() as *mut u8,
                &mut desc_bytes,
            ) == 0
        };
        if !desc_ok || desc_bytes < 2 {
            continue;
        }
        let desc_chars = (desc_bytes as usize / 2).saturating_sub(1);
        let desc = String::from_utf16_lossy(&desc_buf[..desc_chars]).trim().to_string();
        if desc.is_empty() {
            continue;
        }

        let mem_value = wide("HardwareInformation.qwMemorySize");
        let mut mem: u64 = 0;
        let mut mem_bytes = std::mem::size_of::<u64>() as u32;
        unsafe {
            let _ = RegGetValueW(
                class_key,
                name.as_ptr(),
                mem_value.as_ptr(),
                RRF_RT_REG_QWORD,
                std::ptr::null_mut(),
                &mut mem as *mut u64 as *mut u8,
                &mut mem_bytes,
            );
        }
        devices.push(GpuDevice {
            id: format!("gpu:{subname}"),
            name: desc,
            vram_mb: mem / (1024 * 1024),
        });
    }
    unsafe {
        RegCloseKey(class_key);
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
    #[cfg(target_os = "windows")]
    {
        windows_ram_mb()
    }
    #[cfg(not(any(target_os = "linux", target_os = "windows")))]
    {
        8192
    }
}

#[cfg(target_os = "windows")]
fn windows_ram_mb() -> u64 {
    #[repr(C)]
    struct MemoryStatusEx {
        length: u32,
        memory_load: u32,
        total_phys: u64,
        avail_phys: u64,
        total_page: u64,
        avail_page: u64,
        total_virt: u64,
        avail_virt: u64,
        avail_ext: u64,
    }
    #[link(name = "kernel32")]
    extern "system" {
        fn GlobalMemoryStatusEx(status: *mut MemoryStatusEx) -> i32;
    }
    let mut info = MemoryStatusEx {
        length: std::mem::size_of::<MemoryStatusEx>() as u32,
        memory_load: 0,
        total_phys: 0,
        avail_phys: 0,
        total_page: 0,
        avail_page: 0,
        total_virt: 0,
        avail_virt: 0,
        avail_ext: 0,
    };
    unsafe {
        if GlobalMemoryStatusEx(&mut info) != 0 && info.total_phys > 0 {
            return info.total_phys / (1024 * 1024);
        }
    }
    8192
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
pub fn naan_control(action: String, agent_id: Option<String>) -> Result<String, String> {
    let mut params = serde_json::json!({ "action": action });
    if let Some(id) = agent_id {
        if !id.is_empty() {
            params["agent_id"] = serde_json::Value::String(id);
        }
    }
    ffi::rpc_call("naan.control", &params.to_string())
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

#[tauri::command]
pub fn privacy_stealth_generate() -> Result<String, String> {
    ffi::rpc_call("privacy.stealth.generate", "{}")
}

#[tauri::command]
pub fn privacy_stealth_send(recipient: String, amount: String, memo: Option<String>) -> Result<String, String> {
    let params = serde_json::json!({
        "recipient": recipient,
        "amount": amount,
        "memo": memo.unwrap_or_default()
    }).to_string();
    ffi::rpc_call("privacy.stealth.send", &params)
}

#[tauri::command]
pub fn privacy_ring_sign(message: String, ring_size: u32) -> Result<String, String> {
    let params = serde_json::json!({
        "message": message,
        "ring_size": ring_size
    }).to_string();
    ffi::rpc_call("privacy.ring.sign", &params)
}

#[tauri::command]
pub fn privacy_ring_verify(message: String, signature: String, ring: Vec<String>) -> Result<String, String> {
    let params = serde_json::json!({
        "message": message,
        "signature": signature,
        "ring": ring
    }).to_string();
    ffi::rpc_call("privacy.ring.verify", &params)
}

#[tauri::command]
pub fn privacy_view_scan(view_priv: String, spend_pub: String) -> Result<String, String> {
    let params = serde_json::json!({
        "view_priv": view_priv,
        "spend_pub": spend_pub
    }).to_string();
    ffi::rpc_call("privacy.view.scan", &params)
}

#[tauri::command]
pub fn privacy_status() -> Result<String, String> {
    ffi::rpc_call("privacy.status", "{}")
}
