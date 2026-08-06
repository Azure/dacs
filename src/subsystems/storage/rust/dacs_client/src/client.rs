use std::ffi::{CStr, CString};
use std::io;
use std::path::PathBuf;

use libc::{O_CREAT, O_EXCL, O_RDONLY, O_TRUNC, O_WRONLY};

use crate::config::ClientConfig;
use crate::handle::FileHandle;
use crate::native::{self, DirectoryEntry as NativeDirectoryEntry, FileStat as NativeFileStat};

#[derive(Clone, Debug)]
pub struct Client {
    intercept_base_path: PathBuf,
    config_path: PathBuf,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Stat {
    pub mode: u32,
    pub size: i64,
    pub block_size: i64,
    pub atime: i64,
    pub mtime: i64,
    pub ctime: i64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DirEntry {
    pub ino: u64,
    pub off: i64,
    pub reclen: u16,
    pub file_type: u8,
    pub name: String,
    pub size: i64,
    pub atime: i64,
    pub mtime: i64,
}

impl Client {
    pub fn new() -> io::Result<Self> {
        Self::with_config(ClientConfig::default())
    }

    pub fn with_config(config: ClientConfig) -> io::Result<Self> {
        let config_path = config.initialize()?;
        native::get()?;

        Ok(Self {
            intercept_base_path: config.intercept_path,
            config_path,
        })
    }

    pub fn config_path(&self) -> &std::path::Path {
        &self.config_path
    }

    pub fn file_size(&self, blob_path: &str) -> io::Result<i64> {
        Ok(self.file_stat(blob_path)?.size)
    }

    pub fn file_stat(&self, blob_path: &str) -> io::Result<Stat> {
        let open_path = self.intercept_file_path(blob_path)?;
        let native_stat = native::get()?.stat(&open_path)?;
        Ok(native_stat.into())
    }

    pub fn open(&self, blob_path: &str, flags: i32) -> io::Result<FileHandle> {
        let open_path = self.intercept_file_path(blob_path)?;
        let fd = native::get()?.open(&open_path, flags, 0o644)?;
        Ok(FileHandle::new(fd))
    }

    pub fn read_blob(&self, blob_path: &str, buffer: &mut [u8], offset: i64) -> io::Result<usize> {
        let mut handle = self.open(blob_path, O_RDONLY)?;
        let mut total = 0;
        while total < buffer.len() {
            let n = handle.read(&mut buffer[total..], offset + total as i64)?;
            if n == 0 {
                break;
            }
            total += n;
        }
        handle.close()?;
        Ok(total)
    }

    pub fn write_blob(&self, blob_path: &str, buffer: &[u8], overwrite: bool) -> io::Result<usize> {
        let flags = if overwrite {
            O_WRONLY | O_CREAT | O_TRUNC
        } else {
            O_WRONLY | O_CREAT | O_EXCL
        };
        let mut handle = self.open(blob_path, flags)?;
        let n = handle.write(buffer, 0)?;
        if n != buffer.len() {
            return Err(io::Error::new(
                io::ErrorKind::WriteZero,
                format!(
                    "incomplete write: expected {} bytes but wrote {}",
                    buffer.len(),
                    n
                ),
            ));
        }
        handle.close()?;
        Ok(n)
    }

    // TODO: consider returning a lazy iterator (DirIter) instead of Vec for large directories
    pub fn list_dir(&self, blob_dir_path: &str) -> io::Result<Vec<DirEntry>> {
        let open_path = self.intercept_file_path(blob_dir_path)?;
        let native = native::get()?;
        let dir = native.opendir(&open_path)?;
        let mut entries = Vec::new();

        loop {
            let entry = native.readdir(dir);
            if entry.is_null() {
                break;
            }
            entries.push(unsafe { DirEntry::from_native(&*entry) });
        }

        native.closedir(dir)?;
        Ok(entries)
    }

    fn intercept_file_path(&self, blob_path: &str) -> io::Result<CString> {
        let normalized_path = normalize_azure_path(blob_path)?;
        let remote_path = normalized_path.strip_prefix("az://").ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("normalized path does not have az:// prefix: {normalized_path}"),
            )
        })?;
        let intercept_path = self.intercept_base_path.join(remote_path);
        CString::new(intercept_path.to_string_lossy().as_bytes()).map_err(|err| {
            io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("path contains an interior NUL byte: {err}"),
            )
        })
    }
}

pub fn normalize_azure_path(path: &str) -> io::Result<String> {
    if let Some(without_scheme) = path.strip_prefix("https://") {
        let (host, rest) = split_once_or_empty(without_scheme);
        // TODO: handle non-prod blob hostnames (e.g. .blob.core.usgovcloudapi.net for Fairfax)
        if !host.ends_with(".blob.core.windows.net") {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("invalid https Azure path: {path}; expected https://account.blob.core.windows.net/container/blob"),
            ));
        }
        return Ok(format!("az://{host}/{rest}"));
    }

    if let Some(without_scheme) = path.strip_prefix("az://") {
        let (account_or_host, rest) = split_once_or_empty(without_scheme);
        if account_or_host.ends_with(".blob.core.windows.net") {
            return Ok(path.to_string());
        }
        return Ok(format!(
            "az://{account_or_host}.blob.core.windows.net/{rest}"
        ));
    }

    Err(io::Error::new(
        io::ErrorKind::InvalidInput,
        format!("invalid URL '{path}'; expected 'https' or 'az' scheme"),
    ))
}

fn split_once_or_empty(input: &str) -> (&str, &str) {
    input.split_once('/').unwrap_or((input, ""))
}

impl From<NativeFileStat> for Stat {
    fn from(value: NativeFileStat) -> Self {
        Self {
            mode: value.st_mode,
            size: value.st_size,
            block_size: value.st_blksize,
            atime: value.st_atime_sec,
            mtime: value.st_mtime_sec,
            ctime: value.st_ctime_sec,
        }
    }
}

impl DirEntry {
    unsafe fn from_native(value: &NativeDirectoryEntry) -> Self {
        let name = CStr::from_ptr(value.d_name.as_ptr())
            .to_string_lossy()
            .into_owned();
        Self {
            ino: value.d_ino,
            off: value.d_off,
            reclen: value.d_reclen,
            file_type: value.d_type,
            name,
            size: value.d_size,
            atime: value.d_atime,
            mtime: value.d_mtime,
        }
    }
}
