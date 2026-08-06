use std::io;

use libc::{c_int, off64_t};

use crate::native;

#[derive(Debug)]
pub struct FileHandle {
    fd: c_int,
}

impl FileHandle {
    pub(crate) fn new(fd: c_int) -> Self {
        Self { fd }
    }

    pub fn read(&self, buffer: &mut [u8], offset: off64_t) -> io::Result<usize> {
        native::get()?.pread(self.fd, buffer, offset)
    }

    pub fn write(&self, buffer: &[u8], offset: off64_t) -> io::Result<usize> {
        native::get()?.pwrite(self.fd, buffer, offset)
    }

    pub fn close(&mut self) -> io::Result<()> {
        if self.fd < 0 {
            return Ok(());
        }

        let fd = self.fd;
        self.fd = -1;
        native::get()?.close(fd)
    }
}

impl Drop for FileHandle {
    fn drop(&mut self) {
        let _ = self.close();
    }
}
