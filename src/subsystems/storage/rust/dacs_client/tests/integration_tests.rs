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

use dacs_client::{ClientConfig, WriteMode};

fn blob_base_path() -> String {
    std::env::var("DACS_TEST_BLOB_PATH")
        .expect("DACS_TEST_BLOB_PATH must identify the integration-test data directory")
}

fn nonexistent_blob_path() -> String {
    format!("{}/does_not_exist_12345", blob_base_path())
}

fn test_file_path() -> String {
    format!("{}/rust-fixture/sample/sample.bin", blob_base_path())
}

// =============================================================================
// 1. Client init
// =============================================================================

#[test]
fn client_init_and_config() {
    let client = dacs_client::Client::new().unwrap();
    assert!(client.config_path().exists());
}

// =============================================================================
// 2. Single-threaded API tests
// =============================================================================

#[test]
fn client_list_dir_and_stat() {
    let client = dacs_client::Client::new().unwrap();
    let entries = client.list_dir(&blob_base_path()).unwrap();
    assert!(!entries.is_empty());
    let stat = client.file_stat(&blob_base_path()).unwrap();
    assert!(stat.mode & 0o40000 != 0, "expected directory mode");
}

#[test]
fn client_file_size_and_stat_agree() {
    let client = dacs_client::Client::new().unwrap();
    let path = test_file_path();
    let size = client.file_size(&path).unwrap();
    assert!(size > 0);
    assert_eq!(size, client.file_stat(&path).unwrap().size);
}

#[test]
fn client_read_blob() {
    let client = dacs_client::Client::new().unwrap();
    let path = test_file_path();
    let size = client.file_stat(&path).unwrap().size.clamp(0, 4096) as usize;
    let mut buf = vec![0u8; size];
    assert_eq!(client.read_blob(&path, &mut buf, 0).unwrap(), size);
}

#[test]
fn client_open_read_close() {
    let client = dacs_client::Client::new().unwrap();
    let path = test_file_path();
    let mut handle = client.open(&path, libc::O_RDONLY).unwrap();
    let mut buf = vec![0u8; 512];
    let n = handle.read(&mut buf, 0).unwrap();
    assert!(n > 0);
    handle.close().unwrap();
}

// =============================================================================
// 3. Multi-threaded API tests
// =============================================================================

#[test]
fn client_concurrent_reads() {
    use std::sync::Arc;
    use std::thread;

    let client = Arc::new(dacs_client::Client::new().unwrap());
    let path = Arc::new(test_file_path());

    let results: Vec<usize> = (0..4)
        .map(|_| {
            let client = Arc::clone(&client);
            let path = Arc::clone(&path);
            thread::spawn(move || {
                let mut buf = vec![0u8; 1024];
                client.read_blob(&path, &mut buf, 0).unwrap()
            })
        })
        .collect::<Vec<_>>()
        .into_iter()
        .map(|h| h.join().unwrap())
        .collect();

    assert!(
        results.iter().all(|&n| n == results[0]),
        "inconsistent reads: {:?}",
        results
    );
}

// =============================================================================
// 4. Error cases
// =============================================================================

#[test]
fn client_stat_nonexistent_blob() {
    let client = dacs_client::Client::new().unwrap();
    let result = client.file_stat(&nonexistent_blob_path());
    assert!(result.is_err(), "expected error for non-existent blob");
}

#[test]
fn client_file_size_nonexistent_blob() {
    let client = dacs_client::Client::new().unwrap();
    let result = client.file_size(&nonexistent_blob_path());
    assert!(result.is_err(), "expected error for non-existent blob");
}

#[test]
fn client_open_nonexistent_blob() {
    let client = dacs_client::Client::new().unwrap();
    let result = client.open(&nonexistent_blob_path(), libc::O_RDONLY);
    let err = result.unwrap_err();
    assert_eq!(
        err.raw_os_error(),
        Some(libc::ENOENT),
        "expected ENOENT, got: {}",
        err
    );
}

#[test]
fn client_read_nonexistent_blob() {
    let client = dacs_client::Client::new().unwrap();
    let mut buf = vec![0u8; 64];
    let result = client.read_blob(&nonexistent_blob_path(), &mut buf, 0);
    let err = result.unwrap_err();
    assert_eq!(
        err.raw_os_error(),
        Some(libc::ENOENT),
        "expected ENOENT, got: {}",
        err
    );
}

#[test]
fn client_read_beyond_file_size() {
    let client = dacs_client::Client::new().unwrap();
    let path = test_file_path();
    let size = client.file_stat(&path).unwrap().size as i64;
    let mut buf = vec![0u8; 64];
    let n = client.read_blob(&path, &mut buf, size + 1000).unwrap();
    assert_eq!(n, 0);
}

// =============================================================================
// 5. Write (ignored )
//TODO: add cleanup and enable these tests in CI
// =============================================================================

#[test]
#[ignore = "creates blobs without cleanup — run manually"]
fn client_write_then_read_back() {
    let client = dacs_client::Client::with_config(
        ClientConfig::new().with_write_mode(WriteMode::WriteBlobOnly),
    )
    .unwrap();
    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_millis();
    let path = format!(
        "{}/rust_test_roundtrip_{}_{}",
        blob_base_path(),
        std::process::id(),
        ts
    );
    let data = b"roundtrip test data from rust client";

    let written = client.write_blob(&path, data, false).unwrap();
    assert_eq!(written, data.len());

    // Read back and verify
    let mut buf = vec![0u8; data.len()];
    let read = client.read_blob(&path, &mut buf, 0).unwrap();
    assert_eq!(read, data.len());
}

#[test]
//run consistent mode test alone as config is loaded once and cannot be changed in the same process.
#[ignore = "creates blobs without cleanup — run manually"]
fn client_write_overwrite_with_consistent_mode() {
    let client = dacs_client::Client::with_config(
        ClientConfig::new()
            .with_write_mode(WriteMode::WriteSync)
            .with_consistent_mode(true),
    )
    .unwrap();
    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_millis();
    let path = format!(
        "{}/rust_test_overwrite_{}_{}",
        blob_base_path(),
        std::process::id(),
        ts
    );

    // First write with overwrite=true (new file, should succeed)
    client.write_blob(&path, b"first version", true).unwrap();

    // Overwrite same file (should succeed in consistent mode)
    client.write_blob(&path, b"second version", true).unwrap();

    // Read back — should get the latest
    let mut buf = vec![0u8; 14];
    let read = client.read_blob(&path, &mut buf, 0).unwrap();
    assert_eq!(read, 14);
    assert_eq!(&buf, b"second version");
}

#[test]
#[ignore = "creates blobs without cleanup — run manually"]
fn client_write_overwrite_fails_in_efficient_mode() {
    let client = dacs_client::Client::with_config(
        ClientConfig::new().with_write_mode(WriteMode::WriteBlobOnly),
    )
    .unwrap();
    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_millis();
    let path = format!(
        "{}/rust_test_efficient_overwrite_{}_{}",
        blob_base_path(),
        std::process::id(),
        ts
    );

    // First write (new file)
    client.write_blob(&path, b"original data", false).unwrap();

    // Attempt overwrite in efficient mode — should fail
    let result = client.write_blob(&path, b"overwritten data", true);
    assert!(
        result.is_err(),
        "expected error when overwriting in efficient mode"
    );
}
