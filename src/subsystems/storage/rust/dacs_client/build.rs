use std::env;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

fn main() -> io::Result<()> {
    println!("cargo:rerun-if-env-changed=DACS_LIB_PATH");
    println!("cargo:rerun-if-env-changed=DACS_NATIVE_DIR");

    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());

    // Embedded native payload — pipeline stages .so files into native/<arch>/ before publish.
    // build.rs generates include_bytes!() entries so they're baked into the binary.
    let native_dir = env::var_os("DACS_NATIVE_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| manifest_dir.join("native").join(native_arch()));
    println!("cargo:rerun-if-changed={}", native_dir.display());

    let files = collect_files(&native_dir)?;
    for f in &files {
        println!("cargo:rerun-if-changed={}", f.display());
    }
    generate_embedded_rs(&files)
}

fn native_arch() -> String {
    match env::var("CARGO_CFG_TARGET_ARCH").as_deref() {
        Ok("x86_64") => "linux-x86_64",
        Ok("aarch64") => "linux-aarch64",
        Ok(o) => o,
        Err(_) => "linux-unknown",
    }
    .to_string()
}

fn collect_files(dir: &Path) -> io::Result<Vec<PathBuf>> {
    if !dir.is_dir() {
        return Ok(Vec::new());
    }
    let mut files: Vec<PathBuf> = fs::read_dir(dir)?
        .filter_map(|e| e.ok().map(|e| e.path()).filter(|p| p.is_file()))
        .collect();
    files.sort();
    Ok(files)
}

fn generate_embedded_rs(files: &[PathBuf]) -> io::Result<()> {
    let out = PathBuf::from(env::var("OUT_DIR").unwrap()).join("embedded_native.rs");
    let mut code = String::from(
        "pub(crate) struct EmbeddedNativeFile { pub(crate) name: &'static str, pub(crate) bytes: &'static [u8] }\n\
         pub(crate) static EMBEDDED_NATIVE_FILES: &[EmbeddedNativeFile] = &[\n"
    );
    for f in files {
        let name = f.file_name().unwrap().to_str().unwrap();
        code.push_str(&format!(
            "    EmbeddedNativeFile {{ name: {:?}, bytes: include_bytes!(r#\"{}\"#) }},\n",
            name,
            f.display()
        ));
    }
    code.push_str("];\n");
    fs::write(out, code)
}
