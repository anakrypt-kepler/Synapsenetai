use tauri::Url;

/// The webview may stay on the app origin. A caller-supplied external origin is refused.
///
/// `dev_origin` is the configured dev server, and only while the desktop is built for `tauri dev`.
pub fn allowed(url: &Url, dev_origin: Option<&Url>) -> bool {
    if !url.username().is_empty() || url.password().is_some() {
        return false;
    }
    if app_origin(url) {
        return true;
    }
    dev_origin.is_some_and(|origin| same_origin(url, origin))
}

fn app_origin(url: &Url) -> bool {
    match (url.scheme(), url.host_str(), url.port()) {
        ("tauri", Some("localhost"), None) => true,
        ("http" | "https", Some("tauri.localhost"), None) => true,
        _ => false,
    }
}

fn same_origin(url: &Url, origin: &Url) -> bool {
    url.username().is_empty()
        && url.password().is_none()
        && origin.username().is_empty()
        && origin.password().is_none()
        && url.scheme() == origin.scheme()
        && url.host() == origin.host()
        && url.port_or_known_default() == origin.port_or_known_default()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn url(raw: &str) -> Url {
        Url::parse(raw).expect("test url")
    }

    #[test]
    fn app_origins_stay_open() {
        assert!(allowed(&url("http://tauri.localhost/"), None));
        assert!(allowed(&url("https://tauri.localhost/index.html"), None));
        assert!(allowed(&url("tauri://localhost/"), None));
        assert!(allowed(
            &url("http://tauri.localhost/wallet?tab=1#desk"),
            None
        ));
    }

    #[test]
    fn external_origins_are_refused() {
        let dev = url("http://localhost:5173");
        assert!(!allowed(&url("https://example.com/"), None));
        assert!(!allowed(&url("http://ipc.localhost/"), None));
        assert!(!allowed(&url("https://ipc.localhost/"), None));
        assert!(!allowed(&url("file:///C:/Users/owner/.synapsenet"), None));
        assert!(!allowed(&url("http://tauri.localhost.example/"), None));
        assert!(!allowed(&url("http://user@tauri.localhost/"), None));
        assert!(!allowed(&url("http://tauri.localhost:5173/"), None));
        assert!(!allowed(&url("http://127.0.0.1:5173/"), Some(&dev)));
        assert!(!allowed(&url("http://localhost:5173/"), None));
        assert!(allowed(&url("http://localhost:5173/wallet"), Some(&dev)));
    }
}
