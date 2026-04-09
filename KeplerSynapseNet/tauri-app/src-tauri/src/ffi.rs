use libloading::Library;
use once_cell::sync::OnceCell;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::sync::Mutex;

type SynapsedInit = unsafe extern "C" fn(*const c_char) -> i32;
type SynapsedShutdown = unsafe extern "C" fn();
type SynapsedRpcCall = unsafe extern "C" fn(*const c_char, *const c_char) -> *const c_char;
type SynapsedGetStatus = unsafe extern "C" fn() -> *const c_char;
type SynapsedFreeString = unsafe extern "C" fn(*const c_char);
type EventCallback = unsafe extern "C" fn(*const c_char, *const c_char);
type SynapsedSubscribe = unsafe extern "C" fn(*const c_char, EventCallback) -> i32;

struct SynapsedLib {
    _library: Library,
    init: SynapsedInit,
    shutdown: SynapsedShutdown,
    rpc_call: SynapsedRpcCall,
    get_status: SynapsedGetStatus,
    free_string: SynapsedFreeString,
    subscribe: SynapsedSubscribe,
}

unsafe impl Send for SynapsedLib {}
unsafe impl Sync for SynapsedLib {}

static INSTANCE: OnceCell<Mutex<SynapsedLib>> = OnceCell::new();

fn lib_search_paths() -> Vec<std::path::PathBuf> {
    let mut paths = Vec::new();
    if let Ok(p) = std::env::var("SYNAPSED_LIB_PATH") {
        paths.push(std::path::PathBuf::from(p));
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            paths.push(dir.to_path_buf());
            paths.push(dir.join("lib"));
        }
    }
    paths.push(std::path::PathBuf::from("/usr/local/lib"));
    paths.push(std::path::PathBuf::from("/usr/lib"));
    paths
}

fn find_library() -> Result<std::path::PathBuf, String> {
    let names = if cfg!(target_os = "macos") {
        vec!["libsynapsed.dylib"]
    } else if cfg!(target_os = "windows") {
        vec!["synapsed.dll", "libsynapsed.dll"]
    } else {
        vec!["libsynapsed.so", "libsynapsed.so.0"]
    };

    for dir in lib_search_paths() {
        for name in &names {
            let path = dir.join(name);
            if path.exists() {
                return Ok(path);
            }
        }
    }

    Err("libsynapsed not found in any search path".to_string())
}

pub fn load() -> Result<(), String> {
    let lib_path = find_library()?;

    let result = unsafe {
        let library = Library::new(&lib_path)
            .map_err(|e| format!("failed to load libsynapsed from {}: {}", lib_path.display(), e))?;

        let fn_init: SynapsedInit = *library
            .get::<SynapsedInit>(b"synapsed_init\0")
            .map_err(|e| format!("symbol synapsed_init: {}", e))?;
        let fn_shutdown: SynapsedShutdown = *library
            .get::<SynapsedShutdown>(b"synapsed_shutdown\0")
            .map_err(|e| format!("symbol synapsed_shutdown: {}", e))?;
        let fn_rpc_call: SynapsedRpcCall = *library
            .get::<SynapsedRpcCall>(b"synapsed_rpc_call\0")
            .map_err(|e| format!("symbol synapsed_rpc_call: {}", e))?;
        let fn_get_status: SynapsedGetStatus = *library
            .get::<SynapsedGetStatus>(b"synapsed_get_status\0")
            .map_err(|e| format!("symbol synapsed_get_status: {}", e))?;
        let fn_free_string: SynapsedFreeString = *library
            .get::<SynapsedFreeString>(b"synapsed_free_string\0")
            .map_err(|e| format!("symbol synapsed_free_string: {}", e))?;
        let fn_subscribe: SynapsedSubscribe = *library
            .get::<SynapsedSubscribe>(b"synapsed_subscribe\0")
            .map_err(|e| format!("symbol synapsed_subscribe: {}", e))?;

        SynapsedLib {
            _library: library,
            init: fn_init,
            shutdown: fn_shutdown,
            rpc_call: fn_rpc_call,
            get_status: fn_get_status,
            free_string: fn_free_string,
            subscribe: fn_subscribe,
        }
    };

    INSTANCE
        .set(Mutex::new(result))
        .map_err(|_| "libsynapsed already loaded".to_string())
}

fn with_lib<F, R>(f: F) -> Result<R, String>
where
    F: FnOnce(&SynapsedLib) -> Result<R, String>,
{
    let guard = INSTANCE
        .get()
        .ok_or_else(|| "libsynapsed not loaded".to_string())?
        .lock()
        .map_err(|e| format!("lock poisoned: {}", e))?;
    f(&guard)
}

fn ptr_to_string(lib: &SynapsedLib, ptr: *const c_char) -> String {
    if ptr.is_null() {
        return String::new();
    }
    let s = unsafe { CStr::from_ptr(ptr) }
        .to_string_lossy()
        .into_owned();
    unsafe { (lib.free_string)(ptr) };
    s
}

pub fn init(config_path: &str) -> Result<(), String> {
    with_lib(|lib| {
        let c_path =
            CString::new(config_path).map_err(|e| format!("invalid config path: {}", e))?;
        let rc = unsafe { (lib.init)(c_path.as_ptr()) };
        if rc == 0 {
            Ok(())
        } else {
            Err(format!("synapsed_init returned error code {}", rc))
        }
    })
}

pub fn shutdown() -> Result<(), String> {
    with_lib(|lib| {
        unsafe { (lib.shutdown)() };
        Ok(())
    })
}

pub fn rpc_call(method: &str, params: &str) -> Result<String, String> {
    with_lib(|lib| {
        let c_method = CString::new(method).map_err(|e| format!("invalid method: {}", e))?;
        let c_params = CString::new(params).map_err(|e| format!("invalid params: {}", e))?;
        let ptr = unsafe { (lib.rpc_call)(c_method.as_ptr(), c_params.as_ptr()) };
        Ok(ptr_to_string(lib, ptr))
    })
}

pub fn get_status() -> Result<String, String> {
    with_lib(|lib| {
        let ptr = unsafe { (lib.get_status)() };
        Ok(ptr_to_string(lib, ptr))
    })
}

pub fn subscribe(event_type: &str, callback: EventCallback) -> Result<(), String> {
    with_lib(|lib| {
        let c_event =
            CString::new(event_type).map_err(|e| format!("invalid event type: {}", e))?;
        let rc = unsafe { (lib.subscribe)(c_event.as_ptr(), callback) };
        if rc == 0 {
            Ok(())
        } else {
            Err(format!("synapsed_subscribe returned error code {}", rc))
        }
    })
}
