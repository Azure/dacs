// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

extern crate bindgen;

use std::env;
use std::path::PathBuf;

use bindgen::CargoCallbacks;

fn main() {
    let libdir_path = PathBuf::from(env::var("CACHE_CLIENT_LIB_DIR").unwrap())
        .canonicalize()
        .expect("cannot canonicalize path");
    let headers_path = PathBuf::from(env::var("CACHE_CLIENT_HEADER_FILE").unwrap())
        .canonicalize()
        .expect("cannot canonicalize path");
    let headers_path_str = headers_path.to_str().expect("Path is not a valid string");
    let libdir_path_str = libdir_path.to_str().expect("Path is not a valid string");

    // Tell cargo to look for shared libraries in the specified directory
    println!("cargo:rustc-link-search={}", libdir_path_str);

    // Tell cargo to tell rustc to link our `hello` library. Cargo will
    // automatically know it must look for a `libhello.a` file.
    println!("cargo:rustc-link-lib=CacheClientSharedLib");

    // Tell cargo to invalidate the built crate whenever the header changes.
    println!("cargo:rerun-if-changed={}", headers_path_str);

    // The bindgen::Builder is the main entry point
    // to bindgen, and lets you build up options for
    // the resulting bindings.
    let bindings = bindgen::Builder::default()
        // The input header we would like to generate
        // bindings for.
        .header(headers_path_str)
        // Tell cargo to invalidate the built crate whenever any of the
        // included header files changed.
        .parse_callbacks(Box::new(CargoCallbacks))
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("Unable to generate bindings");

    // Write the bindings to the $OUT_DIR/bindings.rs file.
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap()).join("bindings.rs");
    println!("Out path is {out_path:?}");
    bindings
        .write_to_file(out_path)
        .expect("Couldn't write bindings!");
}
