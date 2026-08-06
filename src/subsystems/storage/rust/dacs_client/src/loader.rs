use std::env;
use std::fs;
use std::io;
use std::path::PathBuf;

#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;

mod embedded_native {
    include!(concat!(env!("OUT_DIR"), "/embedded_native.rs"));
}

const LIB_NAME: &str = "libStorageDirect.so";

/// Resolve the path to libStorageDirect.so.
///
/// 1. DACS_LIB_PATH env var — explicit path to a specific .so
/// 2. Embedded native payload — .so bytes baked into the binary, extracted to cache dir
pub(crate) fn library_path() -> io::Result<PathBuf> {
    if let Some(path) = resolve_from_env()? {
        return Ok(path);
    }

    if let Some(path) = extract_embedded()? {
        return Ok(path);
    }

    Err(io::Error::new(
        io::ErrorKind::NotFound,
        format!("{LIB_NAME} not found. Set DACS_LIB_PATH or use a crate with an embedded .so.",),
    ))
}

/// If DACS_LIB_PATH is set, return its value (error if the file is missing).
fn resolve_from_env() -> io::Result<Option<PathBuf>> {
    if let Some(raw_path) = env::var_os("DACS_LIB_PATH") {
        let path = PathBuf::from(raw_path);
        if path.is_file() {
            return Ok(Some(path));
        }
        return Err(io::Error::new(
            io::ErrorKind::NotFound,
            format!("DACS_LIB_PATH points to a missing file: {}", path.display()),
        ));
    }

    Ok(None)
}

/// Extract embedded .so bytes to a cache directory on disk.
/// Required because dlopen needs a file path — cannot load from memory.
/// Skips extraction if the file already exists and matches.
fn extract_embedded() -> io::Result<Option<PathBuf>> {
    let files = embedded_native::EMBEDDED_NATIVE_FILES;
    if files.is_empty() || !files.iter().any(|f| f.name == LIB_NAME) {
        return Ok(None);
    }

    let dir = cache_dir()?
        .join(env!("CARGO_PKG_VERSION"))
        .join(native_arch_dir());
    fs::create_dir_all(&dir)?;

    for file in files {
        let dest = dir.join(file.name);
        if !fs::read(&dest).map(|b| b == file.bytes).unwrap_or(false) {
            fs::write(&dest, file.bytes)?;
            #[cfg(unix)]
            fs::set_permissions(&dest, fs::Permissions::from_mode(0o755))?;
        }
    }

    Ok(Some(dir.join(LIB_NAME)))
}

/// Determine where to store extracted .so files on disk.
///
/// 1. XDG_CACHE_HOME/dacs-client — Linux standard
/// 2. HOME/.cache/dacs-client — common default
fn cache_dir() -> io::Result<PathBuf> {
    if let Some(p) = env::var_os("XDG_CACHE_HOME") {
        return Ok(PathBuf::from(p).join("dacs-client"));
    }
    if let Some(h) = env::var_os("HOME") {
        return Ok(PathBuf::from(h).join(".cache").join("dacs-client"));
    }
    Err(io::Error::new(
        io::ErrorKind::NotFound,
        "Cannot determine cache directory: HOME is not set",
    ))
}

/// Platform subdirectory name (e.g. "linux-x86_64"), matching the pipeline staging layout.
fn native_arch_dir() -> &'static str {
    match env::consts::ARCH {
        "x86_64" => "linux-x86_64",
        "aarch64" => "linux-aarch64",
        a => a,
    }
}
