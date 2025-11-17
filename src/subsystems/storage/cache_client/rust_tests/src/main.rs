#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use rand::Rng;
use std::ffi::{c_char, c_void, CString};

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

fn generate_random_string(length: usize) -> String {
    let mut rng = rand::thread_rng();
    let characters: Vec<char> = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        .chars()
        .collect();
    let string: String = (0..length)
        .map(|_| characters[rng.gen_range(0..characters.len())])
        .collect();
    string
}

fn test_upload() -> (String, usize) {
    let cache_key = generate_random_string(20);
    let key_copy = cache_key.clone();
    let rand_str = generate_random_string(32);
    let data = rand_str.as_bytes();
    let data_size = data.len() as u64;
    let expiry: u32 = 0;
    let group_id: *const c_char = std::ptr::null();
    let metadata: *const Metadata = std::ptr::null();
    let encryption_key: *const c_char = std::ptr::null();

    println!("Starting upload");

    unsafe {
        let status = Upload(
            CString::new(cache_key).unwrap().into_raw(),
            data.as_ptr() as *const c_void,
            data_size,
            expiry,
            group_id,
            metadata,
            encryption_key,
        );

        assert!(status == 0, "Upload failed");
    }

    println!("Finished uploading");
    return (key_copy, data_size as usize);
}

fn test_download(cache_key: String, data_size: usize) {
    // allocate buffer for download
    let mut buf: Vec<u8> = vec![0; data_size];
    let mut buf_len = buf.len() as u64;
    let mut metadata = Metadata {
        arrKeyValuePairs: std::ptr::null_mut() as *mut KeyValuePair,
        nKeyValuePairs: 0,
    };
    let encryption_key: *const c_char = std::ptr::null();

    println!("Starting download");

    let status = unsafe {
        Download(
            CString::new(cache_key).unwrap().into_raw(),
            buf.as_mut_ptr() as *mut c_void,
            &mut buf_len,
            &mut metadata,
            encryption_key,
        )
    };

    println!("Finished download");

    let buf_as_c_string = unsafe { std::ffi::CStr::from_ptr(buf.as_ptr() as *const c_char) };
    println!("Downloaded: {}", buf_as_c_string.to_str().unwrap());

    assert!(status == 0, "Download failed");
}

fn test_initialize() {
    let servers = vec!["127.0.0.1"];
    let num_servers = servers.len() as u32;
    let port: u32 = std::env::var("CACHE_SERVER_PORT").unwrap().parse().unwrap();
    println!("Using port {port}");
    let enable_ib = false;
    let enable_calculate_crc = false;
    let enable_validate_crc = false;
    let enable_consistent_hashing = false;
    let checksum_algorithm = std::ptr::null();

    let accountName = "admin";
    let accountKey = "1234";
    let auth_credentials = AuthCredentials {
        m_strAccountName: CString::new(accountName.to_string()).unwrap().into_raw(),
        m_strAccountKey: CString::new(accountKey.to_string()).unwrap().into_raw(),
    };

    let c_strings: Vec<CString> = servers.iter().map(|s| CString::new(*s).unwrap()).collect();
    let c_string_ptrs: Vec<*const c_char> = c_strings.iter().map(|s| s.as_ptr()).collect();
    let c_string_array: *const *const c_char = c_string_ptrs.as_ptr();

    unsafe {
        let status = Initialize(
            c_string_array,
            num_servers,
            port,
            enable_ib,
            enable_calculate_crc,
            enable_validate_crc,
            checksum_algorithm,
            &auth_credentials as *const AuthCredentials,
            enable_consistent_hashing,
        );

        assert!(status == 0, "Initialize failed")
    }
}

fn test_delete_group() {
    let group_id: *const c_char = CString::new("test_group".to_string()).unwrap().into_raw();
    unsafe {
        let status = DeleteGroup(group_id);
        assert!(status == 0, "DeleteGroup failed");
    }
}

fn main() {
    test_initialize();
    println!("Initialize test passed");

    let (cache_key, data_size) = test_upload();
    println!("Upload test passed");

    test_download(cache_key, data_size);
    println!("Download test passed");

    test_delete_group();
    println!("Delete group test passed");

    println!("All tests passed");
}
