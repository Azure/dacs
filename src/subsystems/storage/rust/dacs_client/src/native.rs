use std::ffi::CStr;
use std::io;
use std::ptr::NonNull;
use std::sync::OnceLock;

use libc::{c_char, c_int, c_uchar, c_ushort, c_void, mode_t, off64_t, size_t, ssize_t, time_t};
use libloading::{Library, Symbol};

use crate::loader;

type OpenFn = unsafe extern "C" fn(*const c_char, c_int, mode_t) -> c_int;
type PreadFn = unsafe extern "C" fn(c_int, *mut c_void, size_t, off64_t) -> ssize_t;
type PwriteFn = unsafe extern "C" fn(c_int, *const c_void, size_t, off64_t) -> ssize_t;
type CloseFn = unsafe extern "C" fn(c_int) -> c_int;
type StatFn = unsafe extern "C" fn(*const c_char, *mut FileStat) -> c_int;
type OpendirFn = unsafe extern "C" fn(*const c_char) -> *mut c_void;
type ReaddirFn = unsafe extern "C" fn(*mut c_void) -> *mut DirectoryEntry;
type ClosedirFn = unsafe extern "C" fn(*mut c_void) -> c_int;

static NATIVE: OnceLock<Native> = OnceLock::new();

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub(crate) struct FileStat {
    pub(crate) st_mode: u32,
    pub(crate) st_size: i64,
    pub(crate) st_blksize: i64,
    pub(crate) st_atime_sec: i64,
    pub(crate) st_mtime_sec: i64,
    pub(crate) st_ctime_sec: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct DirectoryEntry {
    pub(crate) d_ino: u64,
    pub(crate) d_off: off64_t,
    pub(crate) d_reclen: c_ushort,
    pub(crate) d_type: c_uchar,
    pub(crate) d_name: [c_char; 1024],
    pub(crate) d_size: off64_t,
    pub(crate) d_atime: time_t,
    pub(crate) d_mtime: time_t,
    pub(crate) d_version: [c_char; 40],
}

pub(crate) struct Native {
    _lib: Library,
    open: OpenFn,
    pread: PreadFn,
    pwrite: PwriteFn,
    close: CloseFn,
    stat: StatFn,
    opendir: OpendirFn,
    readdir: ReaddirFn,
    closedir: ClosedirFn,
}

unsafe impl Send for Native {}
unsafe impl Sync for Native {}

pub(crate) fn get() -> io::Result<&'static Native> {
    use std::sync::Mutex;
    static INIT_MUTEX: Mutex<()> = Mutex::new(());

    if let Some(native) = NATIVE.get() {
        return Ok(native);
    }

    let _guard = INIT_MUTEX.lock().unwrap_or_else(|e| e.into_inner());
    if let Some(native) = NATIVE.get() {
        return Ok(native);
    }

    let native = Native::load()?;
    let _ = NATIVE.set(native);
    Ok(NATIVE.get().unwrap())
}

impl Native {
    fn load() -> io::Result<Self> {
        let library_path = loader::library_path()?;
        let lib = unsafe { Library::new(&library_path) }.map_err(|err| {
            io::Error::new(
                io::ErrorKind::Other,
                format!("failed to load {}: {err}", library_path.display()),
            )
        })?;

        Ok(Self {
            open: unsafe { load_symbol(&lib, b"open\0") }?,
            pread: unsafe { load_symbol(&lib, b"pread\0") }?,
            pwrite: unsafe { load_symbol(&lib, b"pwrite\0") }?,
            close: unsafe { load_symbol(&lib, b"close\0") }?,
            stat: unsafe { load_symbol(&lib, b"dacs_stat\0") }?,
            opendir: unsafe { load_symbol(&lib, b"opendir\0") }?,
            readdir: unsafe { load_symbol(&lib, b"readdir\0") }?,
            closedir: unsafe { load_symbol(&lib, b"closedir\0") }?,
            _lib: lib,
        })
    }

    pub(crate) fn open(&self, path: &CStr, flags: c_int, mode: mode_t) -> io::Result<c_int> {
        let fd = unsafe { (self.open)(path.as_ptr(), flags, mode) };
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(fd)
    }

    pub(crate) fn pread(&self, fd: c_int, buf: &mut [u8], offset: off64_t) -> io::Result<usize> {
        let result =
            unsafe { (self.pread)(fd, buf.as_mut_ptr().cast::<c_void>(), buf.len(), offset) };
        if result < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(result as usize)
    }

    pub(crate) fn pwrite(&self, fd: c_int, buf: &[u8], offset: off64_t) -> io::Result<usize> {
        let result = unsafe { (self.pwrite)(fd, buf.as_ptr().cast::<c_void>(), buf.len(), offset) };
        if result < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(result as usize)
    }

    pub(crate) fn close(&self, fd: c_int) -> io::Result<()> {
        let result = unsafe { (self.close)(fd) };
        if result < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }

    pub(crate) fn stat(&self, path: &CStr) -> io::Result<FileStat> {
        let mut stat = FileStat::default();
        let result = unsafe { (self.stat)(path.as_ptr(), &mut stat) };
        if result != 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(stat)
    }

    pub(crate) fn opendir(&self, path: &CStr) -> io::Result<NonNull<c_void>> {
        let dir = unsafe { (self.opendir)(path.as_ptr()) };
        NonNull::new(dir).ok_or_else(io::Error::last_os_error)
    }

    pub(crate) fn readdir(&self, dir: NonNull<c_void>) -> *mut DirectoryEntry {
        unsafe { (self.readdir)(dir.as_ptr()) }
    }

    pub(crate) fn closedir(&self, dir: NonNull<c_void>) -> io::Result<()> {
        let result = unsafe { (self.closedir)(dir.as_ptr()) };
        if result != 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }
}

unsafe fn load_symbol<T: Copy>(lib: &Library, name: &'static [u8]) -> io::Result<T> {
    let symbol: Symbol<'_, T> = lib.get(name).map_err(|err| {
        io::Error::new(
            io::ErrorKind::Other,
            format!(
                "failed to resolve symbol {}: {err}",
                String::from_utf8_lossy(name)
            ),
        )
    })?;
    Ok(*symbol)
}
