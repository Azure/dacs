// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
