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

use std::env;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};

#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;

pub const DEFAULT_INTERCEPT_PATH: &str = "/mnt/dacs_cache";

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WriteMode {
    WriteDisabled,
    WriteCacheOnly,
    WriteBlobOnly,
    WriteCacheBlobAsync,
    WriteSync,
}

impl WriteMode {
    fn as_config_value(self) -> &'static str {
        match self {
            Self::WriteDisabled => "write_disabled",
            Self::WriteCacheOnly => "write_cache_only",
            Self::WriteBlobOnly => "write_blob_only",
            Self::WriteCacheBlobAsync => "write_cache_blob_async",
            Self::WriteSync => "write_sync",
        }
    }
}

#[derive(Clone, Debug)]
pub struct ClientConfig {
    pub intercept_path: PathBuf,
    pub write_mode: Option<WriteMode>,
    pub consistent_mode: bool,
    pub config_dir: PathBuf,
}

impl Default for ClientConfig {
    fn default() -> Self {
        Self {
            intercept_path: PathBuf::from(DEFAULT_INTERCEPT_PATH),
            write_mode: None,
            consistent_mode: false,
            config_dir: PathBuf::from("/tmp"),
        }
    }
}

impl ClientConfig {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn with_write_mode(mut self, write_mode: WriteMode) -> Self {
        self.write_mode = Some(write_mode);
        self
    }

    pub fn with_consistent_mode(mut self, consistent_mode: bool) -> Self {
        self.consistent_mode = consistent_mode;
        self
    }

    pub fn with_intercept_path(mut self, intercept_path: impl Into<PathBuf>) -> Self {
        self.intercept_path = intercept_path.into();
        self
    }

    pub fn with_config_dir(mut self, config_dir: impl Into<PathBuf>) -> Self {
        self.config_dir = config_dir.into();
        self
    }

    pub fn initialize(&self) -> io::Result<PathBuf> {
        if self.config_dir.as_os_str().is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "config_dir cannot be empty",
            ));
        }

        fs::create_dir_all(&self.config_dir)?;
        if !self.config_dir.is_dir() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!(
                    "config_dir is not a directory: {}",
                    self.config_dir.display()
                ),
            ));
        }

        let config_path = self
            .config_dir
            .join(format!("storageIntercept.{}.config", std::process::id()));

        let mut file = Self::open_config_file(&config_path)?;
        self.write_config(&mut file)?;
        env::set_var("STORAGE_INTERCEPT_CONFIG_FILEPATH", &config_path);
        Ok(config_path)
    }

    #[cfg(unix)]
    fn open_config_file(config_path: &Path) -> io::Result<File> {
        let mut create_options = OpenOptions::new();
        create_options
            .write(true)
            .create_new(true)
            .mode(0o600)
            .custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK);

        let file = match create_options.open(config_path) {
            Ok(file) => file,
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => {
                let mut existing_options = OpenOptions::new();
                existing_options
                    .write(true)
                    .custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK);
                existing_options.open(config_path)?
            }
            Err(error) => return Err(error),
        };

        if !file.metadata()?.is_file() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "config path is not a regular file",
            ));
        }

        file.set_len(0)?;
        Ok(file)
    }

    #[cfg(not(unix))]
    fn open_config_file(_config_path: &Path) -> io::Result<File> {
        Err(io::Error::new(
            io::ErrorKind::Unsupported,
            "dacs-client supports Unix platforms only",
        ))
    }

    fn write_config(&self, file: &mut impl Write) -> io::Result<()> {
        writeln!(file, "storagePath {}", self.intercept_path.display())?;
        writeln!(file, "type blob")?;
        writeln!(file, "azBlobDynamicAccount true")?;
        writeln!(file, "azBlobDynamicContainer true")?;

        write_auth_config(file)?;

        writeln!(file, "enableLargePrefetch false")?;
        writeln!(file, "prefetchWindowSizeInMB 64")?;
        writeln!(file, "streamingChunkSize 33554432")?;
        writeln!(file, "maxConnsPerBlobClient 84")?;
        writeln!(file, "attributesCacheEnable true")?;

        write_mode_config(file, self.write_mode)?;
        write_cache_config(file)?;

        if self.consistent_mode
            && matches!(
                self.write_mode,
                Some(WriteMode::WriteSync | WriteMode::WriteBlobOnly)
            )
        {
            writeln!(file, "enableConsistentMode true")?;
        }

        Ok(())
    }
}

fn write_auth_config(file: &mut impl Write) -> io::Result<()> {
    let sas_token = if env::var("USE_SAS_TOKEN").unwrap_or_default() == "true" {
        env::var("STORAGE_INTCPT_SAS_TOKEN")
            .ok()
            .map(|token| token.trim().to_string())
            .filter(|token| !token.is_empty())
    } else {
        None
    };
    let access_token = env::var("SI_azBlobAccessToken")
        .ok()
        .filter(|token| !token.is_empty());

    if let Some(token) = sas_token {
        writeln!(file, "azBlobSASToken {token}")?;
    } else if let Some(token) = access_token {
        writeln!(file, "azBlobAccessToken {token}")?;
    } else {
        let client_id = env::var("AZURE_CLIENT_ID")
            .ok()
            .filter(|value| !value.is_empty());
        let msi_endpoint = env::var("MSI_ENDPOINT")
            .ok()
            .filter(|value| !value.is_empty());
        let msi_secret = env::var("MSI_SECRET")
            .ok()
            .filter(|value| !value.is_empty());

        if let (Some(client_id), Some(msi_endpoint), Some(msi_secret)) =
            (client_id, msi_endpoint, msi_secret)
        {
            writeln!(file, "azBlobIdentityClientId {client_id}")?;
            writeln!(file, "azBlobMSIEndpoint {msi_endpoint}")?;
            writeln!(file, "azBlobMSISecret {msi_secret}")?;
        } else {
            writeln!(file, "azBlobUseAzureIdentitySDK true")?;
        }
    }

    Ok(())
}

fn write_mode_config(file: &mut impl Write, write_mode: Option<WriteMode>) -> io::Result<()> {
    match write_mode.map(WriteMode::as_config_value) {
        Some("write_disabled") | None => {
            writeln!(file, "enableWrite false")?;
            writeln!(file, "enableWriteRemote false")?;
            writeln!(file, "enableWriteRemoteAsync false")?;
        }
        Some("write_cache_only") => {
            writeln!(file, "enableWrite true")?;
            writeln!(file, "enableWriteRemote false")?;
            writeln!(file, "enableWriteRemoteAsync false")?;
        }
        Some("write_blob_only") => {
            writeln!(file, "enableWrite true")?;
            writeln!(file, "enableWriteRemote true")?;
            writeln!(file, "enableWriteRemoteAsync false")?;
        }
        Some("write_cache_blob_async") => {
            writeln!(file, "enableWrite true")?;
            writeln!(file, "enableWriteRemote true")?;
            writeln!(file, "enableWriteRemoteAsync true")?;
        }
        Some(_) => {
            writeln!(file, "enableWrite true")?;
            writeln!(file, "enableWriteRemote true")?;
            writeln!(file, "enableWriteRemoteAsync false")?;
        }
    }

    Ok(())
}

fn write_cache_config(file: &mut impl Write) -> io::Result<()> {
    let discovery_url = env::var("CACHE_DISCOVERY_URL")
        .ok()
        .filter(|value| !value.is_empty());
    let server_list = env::var("CACHE_SERVER_LIST")
        .ok()
        .filter(|value| !value.is_empty());
    let server_port = env::var("CACHE_SERVER_PORT")
        .ok()
        .filter(|value| !value.is_empty());

    let cache_enabled =
        if let (Some(discovery_url), Some(server_port)) = (&discovery_url, &server_port) {
            writeln!(file, "cacheServerPort {server_port}")?;
            writeln!(file, "cacheServerDiscoveryEnabled true")?;
            writeln!(file, "cacheServerDiscoveryEndpoint {discovery_url}")?;
            writeln!(file, "cacheServerDiscoveryIntervalInSeconds 300")?;
            true
        } else if let (Some(server_list), Some(server_port)) = (&server_list, &server_port) {
            writeln!(file, "cacheServerPort {server_port}")?;
            writeln!(file, "cacheServerList {server_list}")?;
            true
        } else {
            false
        };

    if cache_enabled {
        writeln!(file, "cacheEnable true")?;
        writeln!(file, "cacheEnableRemote true")?;
        writeln!(file, "cacheRetryMaxAttempts 5")?;
        writeln!(file, "cacheRetryBaseDelayMs 100")?;
        writeln!(file, "cacheRetryMaxDelayMs 1000")?;
        writeln!(file, "cacheMaxUploadBufferSizePerProcessGB 400")?;
        writeln!(file, "cacheNumUploadThreadsPerProcess 80")?;
        writeln!(file, "attributesCacheUseRemoteServer true")?;
    }

    Ok(())
}
