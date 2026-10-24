use libloading::Library;
use once_cell::sync::OnceCell;
use std::ffi::{CString, OsStr};
use std::os::raw::c_char;
use std::path::{Component, Path, PathBuf};
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

/// The C string must contain its terminator inside this many bytes.
const MAX_CSTRING_WINDOW: usize = 8 * 1024 * 1024;

struct LibraryLocation {
    path: PathBuf,
    /// Selected from `SYNAPSED_LIB_PATH` as one explicit file, not by searching a directory.
    explicit_env: bool,
}

fn library_file_names() -> &'static [&'static str] {
    if cfg!(target_os = "macos") {
        &["libsynapsed.dylib"]
    } else if cfg!(target_os = "windows") {
        &["synapsed.dll", "libsynapsed.dll"]
    } else {
        &["libsynapsed.so", "libsynapsed.so.0"]
    }
}

fn exe_dir() -> Option<PathBuf> {
    std::env::current_exe()
        .ok()
        .and_then(|exe| exe.parent().map(Path::to_path_buf))
}

/// Executable directory, then resources beside that executable.
///
/// `SYNAPSED_LIB_PATH`, `HOME`, and `USERPROFILE` are not search directories.
fn lib_search_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    if let Some(dir) = exe_dir() {
        paths.push(dir.clone());
        paths.push(dir.join("resources"));
        paths.push(dir.join("resources").join("windows-runtime"));
        paths.push(dir.join("lib"));
        #[cfg(target_os = "macos")]
        {
            if let Some(contents) = dir.parent() {
                paths.push(contents.join("Frameworks"));
            }
        }
    }
    // CMake output lives in KeplerSynapseNet/build next to this crate.
    let build_root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build");
    paths.push(build_root.join("Release"));
    paths.push(build_root.join("Debug"));
    paths.push(build_root);
    paths.push(PathBuf::from("/usr/local/lib"));
    paths.push(PathBuf::from("/usr/lib"));
    paths
}

fn is_allowed_explicit_name(name: &OsStr) -> bool {
    let Some(name) = name.to_str() else {
        return false;
    };
    if cfg!(windows) {
        name.eq_ignore_ascii_case("synapsed.dll") || name.eq_ignore_ascii_case("libsynapsed.so")
    } else {
        name == "synapsed.dll" || name == "libsynapsed.so"
    }
}

/// `SYNAPSED_LIB_PATH` may name one file, `synapsed.dll` or `libsynapsed.so`.
/// It is not a directory to search, and "." / ".." components are rejected.
fn explicit_lib_from_value(value: &str) -> Option<PathBuf> {
    let path = PathBuf::from(value);
    if !path.is_absolute() {
        return None;
    }
    if path
        .components()
        .any(|component| matches!(component, Component::ParentDir | Component::CurDir))
    {
        return None;
    }
    if !is_allowed_explicit_name(path.file_name()?) {
        return None;
    }
    if !path.is_file() {
        return None;
    }
    let canonical = std::fs::canonicalize(&path).ok()?;
    if !canonical.is_file() || !is_allowed_explicit_name(canonical.file_name()?) {
        return None;
    }
    Some(canonical)
}

fn explicit_synapsed_lib() -> Option<PathBuf> {
    explicit_lib_from_value(&std::env::var("SYNAPSED_LIB_PATH").ok()?)
}

fn first_library_file(dir: &Path, names: &[&str]) -> Option<PathBuf> {
    names
        .iter()
        .map(|name| dir.join(name))
        .find(|path| path.is_file())
}

fn find_library() -> Result<LibraryLocation, String> {
    let names = library_file_names();
    for dir in lib_search_paths() {
        if let Some(path) = first_library_file(&dir, names) {
            let path = std::fs::canonicalize(&path).unwrap_or(path);
            return Ok(LibraryLocation {
                path,
                explicit_env: false,
            });
        }
    }

    if let Some(path) = explicit_synapsed_lib() {
        return Ok(LibraryLocation {
            path,
            explicit_env: true,
        });
    }

    Err("libsynapsed not found in any search path".to_string())
}

fn components_of(path: &Path) -> Vec<String> {
    path.components()
        .filter(|component| !matches!(component, Component::CurDir))
        .map(|component| component.as_os_str().to_string_lossy().to_lowercase())
        .collect()
}

fn comparable_components(path: &Path) -> Vec<String> {
    if let Ok(canonical) = std::fs::canonicalize(path) {
        return components_of(&canonical);
    }

    let mut leftover = Vec::new();
    let mut cursor = path.to_path_buf();
    let max_steps = path.components().count() + 1;
    let mut steps = 0usize;
    while steps < max_steps && !cursor.as_os_str().is_empty() && !cursor.exists() {
        steps += 1;
        let Some(name) = cursor.file_name().map(OsStr::to_os_string) else {
            break;
        };
        leftover.push(name);
        if !cursor.pop() {
            break;
        }
    }

    let mut components = if let Ok(canonical) = std::fs::canonicalize(&cursor) {
        components_of(&canonical)
    } else {
        components_of(&cursor)
    };
    for name in leftover.iter().rev() {
        components.push(name.to_string_lossy().to_lowercase());
    }
    components
}

fn paths_equal(left: &Path, right: &Path) -> bool {
    let left = comparable_components(left);
    let right = comparable_components(right);
    !left.is_empty() && left == right
}

fn is_same_or_under(path: &Path, root: &Path) -> bool {
    let path = comparable_components(path);
    let root = comparable_components(root);
    !root.is_empty() && path.len() >= root.len() && path[..root.len()] == root[..]
}

/// True when `path` is `root` or a file inside it, after `..` and links resolve.
pub(crate) fn path_within_root(path: &Path, root: &Path) -> bool {
    is_same_or_under(path, root)
}

fn profile_roots() -> Vec<PathBuf> {
    let mut roots = Vec::new();
    if let Some(home) = dirs::home_dir() {
        if home.is_dir() {
            roots.push(home);
        }
    }
    for key in ["USERPROFILE", "HOME"] {
        if let Ok(value) = std::env::var(key) {
            let path = PathBuf::from(value);
            if path.is_dir() {
                roots.push(path);
            }
        }
    }
    roots
}

fn is_under_user_profile(dir: &Path) -> bool {
    profile_roots()
        .iter()
        .any(|root| is_same_or_under(dir, root))
}

fn is_app_local_library_dir(dir: &Path) -> bool {
    let Some(exe_dir) = exe_dir() else {
        return false;
    };
    [
        exe_dir.clone(),
        exe_dir.join("resources"),
        exe_dir.join("resources").join("windows-runtime"),
        exe_dir.join("lib"),
    ]
    .iter()
    .any(|candidate| paths_equal(candidate, dir))
}

/// Directories beside the executable may be added. A library chosen through
/// `SYNAPSED_LIB_PATH`, and any other user-profile directory, may not.
fn dll_directory_permitted(dir: &Path, explicit_env: bool) -> bool {
    if explicit_env {
        return false;
    }
    if is_app_local_library_dir(dir) {
        return true;
    }
    !is_under_user_profile(dir)
}

#[cfg(windows)]
fn add_dll_directory(dir: &Path) {
    use std::os::windows::ffi::OsStrExt;
    let wide: Vec<u16> = dir
        .as_os_str()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    extern "system" {
        fn SetDllDirectoryW(path: *const u16) -> i32;
    }
    unsafe {
        SetDllDirectoryW(wide.as_ptr());
    }
}

#[cfg(windows)]
fn configure_dll_directory(lib_path: &Path, explicit_env: bool) {
    let Ok(canonical) = std::fs::canonicalize(lib_path) else {
        return;
    };
    let Some(parent) = canonical.parent() else {
        return;
    };
    if dll_directory_permitted(parent, explicit_env) {
        add_dll_directory(parent);
    }
}

pub fn load() -> Result<(), String> {
    let located = find_library()?;

    #[cfg(windows)]
    configure_dll_directory(&located.path, located.explicit_env);

    let lib_path = located.path;
    let result = unsafe {
        let library = Library::new(&lib_path).map_err(|e| {
            format!(
                "failed to load libsynapsed from {}: {:?}",
                lib_path.display(),
                e
            )
        })?;

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

/// Reads at most `max_window` bytes and requires a null inside that window.
///
/// # Safety
/// `ptr` must be readable through the first null, or for `max_window` bytes
/// when no null appears sooner. Bytes past the window are not read.
unsafe fn copy_cstr_within(ptr: *const c_char, max_window: usize) -> Option<String> {
    let mut len = 0usize;
    while len < max_window {
        if *ptr.add(len) == 0 {
            let bytes = std::slice::from_raw_parts(ptr.cast::<u8>(), len);
            return Some(String::from_utf8_lossy(bytes).into_owned());
        }
        len += 1;
    }
    None
}

/// Copies one callback C string using the same window as rpc and status.
///
/// A null pointer is an empty string. Any other pointer is copied only when a
/// null byte sits inside that window.
///
/// # Safety
/// When `ptr` is not null, it must be readable through the first null byte, or
/// for the whole window when no null appears sooner. Bytes past the window are
/// not read.
pub(crate) unsafe fn copy_cstr_bounded(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        return Some(String::new());
    }
    unsafe { copy_cstr_within(ptr, MAX_CSTRING_WINDOW) }
}

fn ptr_to_string(lib: &SynapsedLib, ptr: *const c_char) -> Result<String, String> {
    if ptr.is_null() {
        return Ok(String::new());
    }

    // free_string still runs for every non-null pointer, including a string
    // that has no terminator inside the window.
    struct FreeString<'a> {
        lib: &'a SynapsedLib,
        ptr: *const c_char,
    }

    impl Drop for FreeString<'_> {
        fn drop(&mut self) {
            unsafe { (self.lib.free_string)(self.ptr) };
        }
    }

    let _free = FreeString { lib, ptr };
    if let Some(text) = unsafe { copy_cstr_within(ptr, MAX_CSTRING_WINDOW) } {
        Ok(text)
    } else {
        Err("synapsed response was not null-terminated within 8 MiB".to_string())
    }
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
        ptr_to_string(lib, ptr)
    })
}

pub fn get_status() -> Result<String, String> {
    with_lib(|lib| {
        let ptr = unsafe { (lib.get_status)() };
        ptr_to_string(lib, ptr)
    })
}

pub fn subscribe(event_type: &str, callback: EventCallback) -> Result<(), String> {
    with_lib(|lib| {
        let c_event = CString::new(event_type).map_err(|e| format!("invalid event type: {}", e))?;
        let rc = unsafe { (lib.subscribe)(c_event.as_ptr(), callback) };
        if rc == 0 {
            Ok(())
        } else {
            Err(format!("synapsed_subscribe returned error code {}", rc))
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    struct TempDir(PathBuf);

    impl TempDir {
        fn new() -> Self {
            let path = std::env::temp_dir().join(format!(
                "synapsed-ffi-{}-{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_nanos()
            ));
            std::fs::create_dir_all(&path).unwrap();
            Self(path)
        }
    }

    impl Drop for TempDir {
        fn drop(&mut self) {
            std::fs::remove_dir_all(&self.0).ok();
        }
    }

    #[test]
    fn search_prefers_executable_then_resources() {
        let paths = lib_search_paths();
        let exe_dir = exe_dir().expect("test executable has a directory");
        assert_eq!(paths[0], exe_dir);
        assert_eq!(paths[1], exe_dir.join("resources"));
        assert_eq!(paths[2], exe_dir.join("resources").join("windows-runtime"));
        assert!(paths.iter().all(|path| {
            let comps = comparable_components(path);
            !(comps.len() >= 2
                && comps[comps.len() - 2] == ".synapsenet"
                && comps[comps.len() - 1] == "lib")
        }));
    }

    #[test]
    fn explicit_path_accepts_only_a_named_file() {
        let dir = TempDir::new();
        let good = dir.0.join("synapsed.dll");
        std::fs::write(&good, b"synapsed").unwrap();
        let resolved = explicit_lib_from_value(good.to_str().unwrap()).unwrap();
        assert!(resolved.is_file());
        assert!(is_allowed_explicit_name(resolved.file_name().unwrap()));

        let nested_dir = dir.0.join("nested");
        std::fs::create_dir_all(&nested_dir).unwrap();
        let nested = nested_dir.join("libsynapsed.so");
        std::fs::write(&nested, b"synapsed").unwrap();
        assert!(explicit_lib_from_value(nested.to_str().unwrap()).is_some());

        let wrong = dir.0.join("libsynapsed.dll");
        std::fs::write(&wrong, b"synapsed").unwrap();
        assert!(explicit_lib_from_value(wrong.to_str().unwrap()).is_none());
        assert!(explicit_lib_from_value(dir.0.to_str().unwrap()).is_none());
        assert!(explicit_lib_from_value("synapsed.dll").is_none());
        assert!(explicit_lib_from_value("libsynapsed.so").is_none());

        let walked = dir
            .0
            .join("..")
            .join(dir.0.file_name().unwrap())
            .join("synapsed.dll");
        assert!(walked
            .components()
            .any(|component| matches!(component, Component::ParentDir)));
        assert!(explicit_lib_from_value(walked.to_str().unwrap()).is_none());

        let mut dotted_raw = dir.0.as_os_str().to_os_string();
        dotted_raw.push(r"\.\synapsed.dll");
        let dotted = PathBuf::from(dotted_raw);
        if dotted
            .components()
            .any(|component| matches!(component, Component::CurDir))
        {
            assert!(explicit_lib_from_value(dotted.to_str().unwrap()).is_none());
        }

        std::fs::remove_file(&good).unwrap();
        std::fs::create_dir(&good).unwrap();
        assert!(explicit_lib_from_value(good.to_str().unwrap()).is_none());
    }

    #[test]
    fn dll_directory_skips_profile_and_env_selection() {
        let exe_dir = exe_dir().expect("test executable has a directory");
        assert!(dll_directory_permitted(&exe_dir, false));
        assert!(dll_directory_permitted(&exe_dir.join("resources"), false));
        assert!(dll_directory_permitted(
            &exe_dir.join("resources").join("windows-runtime"),
            false
        ));
        assert!(!dll_directory_permitted(&exe_dir, true));
        assert!(!dll_directory_permitted(&exe_dir.join("resources"), true));

        let profile = std::env::var("USERPROFILE")
            .or_else(|_| std::env::var("HOME"))
            .map(PathBuf::from)
            .expect("user profile is available");
        assert!(profile.is_dir());
        assert!(
            is_under_user_profile(&profile),
            "user profile directory was not recognized"
        );
        if !is_app_local_library_dir(&profile) {
            assert!(!dll_directory_permitted(&profile, false));
        }

        let temp = std::env::temp_dir();
        if is_under_user_profile(&temp) && !is_app_local_library_dir(&temp) {
            assert!(!dll_directory_permitted(&temp, false));
        }
    }

    #[test]
    fn copy_cstr_requires_null_within_window() {
        assert_eq!(MAX_CSTRING_WINDOW, 8 * 1024 * 1024);

        let terminated = b"hello\0";
        let text = unsafe { copy_cstr_within(terminated.as_ptr().cast(), 8) };
        assert_eq!(text.as_deref(), Some("hello"));

        let empty = b"\0";
        let text = unsafe { copy_cstr_within(empty.as_ptr().cast(), MAX_CSTRING_WINDOW) };
        assert_eq!(text.as_deref(), Some(""));

        let mut bounded = [b'a', b'b', b'c', 0];
        let text = unsafe { copy_cstr_within(bounded.as_mut_ptr().cast(), bounded.len()) };
        assert_eq!(text.as_deref(), Some("abc"));

        let mut missing = [b'a'; 4];
        let text = unsafe { copy_cstr_within(missing.as_mut_ptr().cast(), missing.len()) };
        assert!(text.is_none());
    }

    #[test]
    fn bounded_copy_keeps_terminated_text_and_empty_null() {
        let text = unsafe { copy_cstr_bounded(std::ptr::null()) };
        assert_eq!(text.as_deref(), Some(""));

        let terminated = b"node.event\0";
        let text = unsafe { copy_cstr_bounded(terminated.as_ptr().cast()) };
        assert_eq!(text.as_deref(), Some("node.event"));
    }
}
