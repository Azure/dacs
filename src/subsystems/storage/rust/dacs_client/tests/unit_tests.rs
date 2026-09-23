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

use dacs_client::{normalize_azure_path, ClientConfig, WriteMode, DEFAULT_INTERCEPT_PATH};
use std::io;

// =============================================================================
// 1. Path normalization
// =============================================================================

#[test]
fn normalize_az_paths() {
    assert_eq!(
        normalize_azure_path("az://myaccount/mycontainer/myblob.txt").unwrap(),
        "az://myaccount.blob.core.windows.net/mycontainer/myblob.txt"
    );
    let long = "az://myaccount.blob.core.windows.net/mycontainer/myblob.txt";
    assert_eq!(normalize_azure_path(long).unwrap(), long);
    assert_eq!(
        normalize_azure_path("az://myaccount/mycontainer").unwrap(),
        "az://myaccount.blob.core.windows.net/mycontainer"
    );
    assert_eq!(
        normalize_azure_path("az://myaccount").unwrap(),
        "az://myaccount.blob.core.windows.net/"
    );
    assert_eq!(
        normalize_azure_path("az://acct/container/a/b/c/file.bin").unwrap(),
        "az://acct.blob.core.windows.net/container/a/b/c/file.bin"
    );
    assert_eq!(
        normalize_azure_path("az://account/container/path with spaces/file.txt").unwrap(),
        "az://account.blob.core.windows.net/container/path with spaces/file.txt"
    );
    assert!(
        normalize_azure_path("az://account/container/données/fichier.txt")
            .unwrap()
            .contains("données")
    );
}

#[test]
fn normalize_https_path() {
    assert_eq!(
        normalize_azure_path("https://myaccount.blob.core.windows.net/mycontainer/myblob.txt")
            .unwrap(),
        "az://myaccount.blob.core.windows.net/mycontainer/myblob.txt"
    );
}

#[test]
fn normalize_invalid_paths() {
    assert_eq!(
        normalize_azure_path("https://example.com/container/blob")
            .unwrap_err()
            .kind(),
        io::ErrorKind::InvalidInput
    );
    assert_eq!(
        normalize_azure_path("s3://bucket/key").unwrap_err().kind(),
        io::ErrorKind::InvalidInput
    );
    assert!(normalize_azure_path("").is_err());
    assert!(normalize_azure_path("not-a-url").is_err());
    assert!(normalize_azure_path("http://account.blob.core.windows.net/c/b").is_err());
    assert!(normalize_azure_path("https://").is_err());
}

// =============================================================================
// 2. ClientConfig + WriteMode
// =============================================================================

#[test]
fn config_defaults_and_builder() {
    let config = ClientConfig::default();
    assert_eq!(
        config.intercept_path.to_str().unwrap(),
        DEFAULT_INTERCEPT_PATH
    );
    assert!(config.write_mode.is_none());
    assert!(!config.consistent_mode);

    let config = ClientConfig::new()
        .with_write_mode(WriteMode::WriteSync)
        .with_consistent_mode(true)
        .with_intercept_path("/custom/path")
        .with_config_dir("/custom/config");
    assert_eq!(config.write_mode, Some(WriteMode::WriteSync));
    assert!(config.consistent_mode);
}

#[test]
fn config_initialize_writes_correct_file() {
    use std::os::unix::fs::PermissionsExt;

    let dir = std::env::temp_dir().join(format!("dacs_test_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = ClientConfig::new()
        .with_config_dir(&dir)
        .initialize()
        .unwrap();
    assert!(path.exists());
    let c = std::fs::read_to_string(&path).unwrap();
    assert!(c.contains("storagePath /mnt/dacs_cache"));
    assert!(c.contains("type blob"));
    assert!(c.contains("azBlobDynamicAccount true"));
    assert!(c.contains("enableWrite false"));
    assert_eq!(
        std::fs::metadata(&path).unwrap().permissions().mode() & 0o777,
        0o600
    );
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn config_initialize_replaces_existing_regular_file() {
    use std::os::unix::fs::PermissionsExt;

    let dir = std::env::temp_dir().join(format!("dacs_replace_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let config = ClientConfig::new().with_config_dir(&dir);

    let path = config.initialize().unwrap();
    std::fs::write(&path, "stale config").unwrap();
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o644)).unwrap();

    assert_eq!(config.initialize().unwrap(), path);
    let contents = std::fs::read_to_string(&path).unwrap();
    assert!(contents.contains("storagePath /mnt/dacs_cache"));
    assert!(!contents.contains("stale config"));
    assert_eq!(
        std::fs::metadata(&path).unwrap().permissions().mode() & 0o777,
        0o644
    );

    std::fs::remove_dir_all(&dir).ok();
}

#[cfg(unix)]
#[test]
fn config_initialize_rejects_symlink() {
    let dir = std::env::temp_dir().join(format!("dacs_symlink_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();

    let config_path = dir.join(format!("storageIntercept.{}.config", std::process::id()));
    let target_path = dir.join("target");
    std::fs::write(&target_path, "target must remain unchanged").unwrap();
    std::os::unix::fs::symlink(&target_path, &config_path).unwrap();

    let result = ClientConfig::new().with_config_dir(&dir).initialize();
    assert!(result.is_err());
    assert_eq!(
        std::fs::read_to_string(&target_path).unwrap(),
        "target must remain unchanged"
    );

    std::fs::remove_dir_all(&dir).ok();
}

#[cfg(unix)]
#[test]
fn config_initialize_rejects_fifo() {
    use std::ffi::CString;
    use std::os::unix::ffi::OsStrExt;

    let dir = std::env::temp_dir().join(format!("dacs_fifo_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();

    let config_path = dir.join(format!("storageIntercept.{}.config", std::process::id()));
    let config_path_c = CString::new(config_path.as_os_str().as_bytes()).unwrap();
    assert_eq!(unsafe { libc::mkfifo(config_path_c.as_ptr(), 0o600) }, 0);

    let result = ClientConfig::new().with_config_dir(&dir).initialize();
    assert!(result.is_err());

    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn config_all_write_modes_and_consistent() {
    let cases = vec![
        (
            WriteMode::WriteDisabled,
            "enableWrite false",
            "enableWriteRemote false",
            "enableWriteRemoteAsync false",
        ),
        (
            WriteMode::WriteCacheOnly,
            "enableWrite true",
            "enableWriteRemote false",
            "enableWriteRemoteAsync false",
        ),
        (
            WriteMode::WriteBlobOnly,
            "enableWrite true",
            "enableWriteRemote true",
            "enableWriteRemoteAsync false",
        ),
        (
            WriteMode::WriteCacheBlobAsync,
            "enableWrite true",
            "enableWriteRemote true",
            "enableWriteRemoteAsync true",
        ),
        (
            WriteMode::WriteSync,
            "enableWrite true",
            "enableWriteRemote true",
            "enableWriteRemoteAsync false",
        ),
    ];
    for (mode, w, r, a) in cases {
        let dir = std::env::temp_dir().join(format!("dacs_wm_{:?}_{}", mode, std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let c = std::fs::read_to_string(
            ClientConfig::new()
                .with_write_mode(mode)
                .with_config_dir(&dir)
                .initialize()
                .unwrap(),
        )
        .unwrap();
        assert!(c.contains(w), "{:?}: missing '{}'", mode, w);
        assert!(c.contains(r), "{:?}: missing '{}'", mode, r);
        assert!(c.contains(a), "{:?}: missing '{}'", mode, a);
        std::fs::remove_dir_all(&dir).ok();
    }

    // Consistent mode: enabled with WriteSync, ignored with WriteCacheOnly
    let dir = std::env::temp_dir().join(format!("dacs_cm_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let c = std::fs::read_to_string(
        ClientConfig::new()
            .with_write_mode(WriteMode::WriteSync)
            .with_consistent_mode(true)
            .with_config_dir(&dir)
            .initialize()
            .unwrap(),
    )
    .unwrap();
    assert!(c.contains("enableConsistentMode true"));
    std::fs::remove_dir_all(&dir).ok();

    let dir = std::env::temp_dir().join(format!("dacs_cm2_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let c = std::fs::read_to_string(
        ClientConfig::new()
            .with_write_mode(WriteMode::WriteCacheOnly)
            .with_consistent_mode(true)
            .with_config_dir(&dir)
            .initialize()
            .unwrap(),
    )
    .unwrap();
    assert!(!c.contains("enableConsistentMode"));
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn config_error_cases() {
    // Empty config dir
    assert!(ClientConfig::new()
        .with_config_dir("")
        .initialize()
        .is_err());
}

// =============================================================================
// 3. Type safety — all public types are Send + Sync
// =============================================================================

#[test]
fn write_mode_equality() {
    assert_eq!(WriteMode::WriteDisabled, WriteMode::WriteDisabled);
    assert_ne!(WriteMode::WriteSync, WriteMode::WriteBlobOnly);
}

#[test]
fn all_public_types_are_send_and_sync() {
    fn assert_send_sync<T: Send + Sync>() {}
    assert_send_sync::<dacs_client::Client>();
    assert_send_sync::<dacs_client::ClientConfig>();
    assert_send_sync::<dacs_client::Stat>();
    assert_send_sync::<dacs_client::DirEntry>();
}
