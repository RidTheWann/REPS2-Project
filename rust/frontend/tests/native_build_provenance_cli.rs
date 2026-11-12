// SPDX-License-Identifier: GPL-3.0-or-later
//! CLI tests for parent-ISO validation and ELF compatibility diagnostics.

use std::fs;
use std::path::PathBuf;
use std::process::{Command, Output};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

// Rust tests in one binary run in parallel threads of the same process, and the
// system clock can be coarse enough (notably on Windows) that two calls observe
// the same nanosecond. A process-wide sequence keeps every temporary directory
// unique regardless of clock resolution.
static TEMP_SEQUENCE: AtomicU64 = AtomicU64::new(0);

const BLOCK: usize = 2048;
const ISO_BLOCKS: usize = 24;
const ROOT_LBA: usize = 20;
const SYSTEM_LBA: usize = 21;
const ELF_LBA: usize = 22;

struct TempDir(PathBuf);

impl TempDir {
    fn new() -> Self {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system clock")
            .as_nanos();
        let sequence = TEMP_SEQUENCE.fetch_add(1, Ordering::Relaxed);
        let path = std::env::temp_dir().join(format!(
            "reps2-parent-iso-cli-{}-{nonce}-{sequence}",
            std::process::id()
        ));
        fs::create_dir(&path).expect("create temporary directory");
        Self(path)
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

fn put_u16(bytes: &mut [u8], offset: usize, value: u16) {
    bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}

fn put_u32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn put_both_u16(bytes: &mut [u8], offset: usize, value: u16) {
    bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
    bytes[offset + 2..offset + 4].copy_from_slice(&value.to_be_bytes());
}

fn put_both_u32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    bytes[offset + 4..offset + 8].copy_from_slice(&value.to_be_bytes());
}

fn synthetic_elf() -> Vec<u8> {
    let mut bytes = vec![0u8; 112];
    bytes[..16].copy_from_slice(b"\x7FELF\x01\x01\x01\0\0\0\0\0\0\0\0\0");
    put_u16(&mut bytes, 16, 2);
    put_u16(&mut bytes, 18, 8);
    put_u32(&mut bytes, 20, 1);
    put_u32(&mut bytes, 24, 0x0010_0000);
    put_u32(&mut bytes, 28, 52);
    put_u16(&mut bytes, 40, 52);
    put_u16(&mut bytes, 42, 32);
    put_u16(&mut bytes, 44, 1);
    put_u32(&mut bytes, 52, 1);
    put_u32(&mut bytes, 56, 96);
    put_u32(&mut bytes, 60, 0x0010_0000);
    put_u32(&mut bytes, 64, 0x0010_0000);
    put_u32(&mut bytes, 68, 8);
    put_u32(&mut bytes, 72, 16);
    put_u32(&mut bytes, 76, 5);
    put_u32(&mut bytes, 80, 1);
    bytes
}

fn directory_record(extent: u32, size: u32, flags: u8, name: &[u8]) -> Vec<u8> {
    let padding = usize::from(name.len().is_multiple_of(2));
    let length = 33 + name.len() + padding;
    let mut record = vec![0u8; length];
    record[0] = length as u8;
    put_both_u32(&mut record, 2, extent);
    put_both_u32(&mut record, 10, size);
    record[25] = flags;
    put_both_u16(&mut record, 28, 1);
    record[32] = name.len() as u8;
    record[33..33 + name.len()].copy_from_slice(name);
    record
}

fn synthetic_iso(elf: &[u8], boot_path: &str) -> Vec<u8> {
    let mut image = vec![0u8; ISO_BLOCKS * BLOCK];
    let pvd_offset = 16 * BLOCK;
    let pvd = &mut image[pvd_offset..pvd_offset + BLOCK];
    pvd[0] = 1;
    pvd[1..6].copy_from_slice(b"CD001");
    pvd[6] = 1;
    put_both_u32(pvd, 80, ISO_BLOCKS as u32);
    put_both_u16(pvd, 120, 1);
    put_both_u16(pvd, 124, 1);
    put_both_u16(pvd, 128, BLOCK as u16);
    let root = directory_record(ROOT_LBA as u32, BLOCK as u32, 2, &[0]);
    pvd[156..156 + root.len()].copy_from_slice(&root);

    let terminator = &mut image[17 * BLOCK..18 * BLOCK];
    terminator[0] = 255;
    terminator[1..6].copy_from_slice(b"CD001");
    terminator[6] = 1;

    let system_cnf = format!("BOOT2 = cdrom0:\\{boot_path}\n");
    let root_offset = ROOT_LBA * BLOCK;
    let mut cursor = root_offset;
    for record in [
        directory_record(ROOT_LBA as u32, BLOCK as u32, 2, &[0]),
        directory_record(ROOT_LBA as u32, BLOCK as u32, 2, &[1]),
        directory_record(
            SYSTEM_LBA as u32,
            system_cnf.len() as u32,
            0,
            b"SYSTEM.CNF;1",
        ),
        directory_record(ELF_LBA as u32, elf.len() as u32, 0, b"BOOT.ELF;1"),
    ] {
        image[cursor..cursor + record.len()].copy_from_slice(&record);
        cursor += record.len();
    }
    let system_offset = SYSTEM_LBA * BLOCK;
    image[system_offset..system_offset + system_cnf.len()].copy_from_slice(system_cnf.as_bytes());
    let elf_offset = ELF_LBA * BLOCK;
    image[elf_offset..elf_offset + elf.len()].copy_from_slice(elf);
    image
}

fn run_recompile(temp: &TempDir, elf: &[u8], iso: Option<&[u8]>) -> (Output, PathBuf) {
    let input = temp.0.join("input.elf");
    let output = temp.0.join("output.cpp");
    fs::write(&input, elf).expect("write ELF");
    let mut command = Command::new(env!("CARGO_BIN_EXE_reps2"));
    command
        .arg("recompile-elf")
        .arg(&input)
        .arg(&output)
        .arg("--native-build");
    if let Some(iso_bytes) = iso {
        let iso_path = temp.0.join("parent.iso");
        fs::write(&iso_path, iso_bytes).expect("write ISO");
        command.arg("--parent-iso").arg(iso_path);
    }
    (command.output().expect("run recompile-elf"), output)
}

#[test]
fn parent_iso_digest_is_bound_only_after_extracted_boot_elf_matches() {
    let temp = TempDir::new();
    let elf = synthetic_elf();
    let iso = synthetic_iso(&elf, "BOOT.ELF;1");
    let expected_iso_digest =
        reps2_frontend::digest::hex_digest(&reps2_frontend::digest::sha256_bytes(&iso));
    let (output, generated_cpp) = run_recompile(&temp, &elf, Some(&iso));
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let native_dir = generated_cpp.with_file_name("output.native");
    let cmake = fs::read_to_string(native_dir.join("CMakeLists.txt")).expect("generated CMake");
    assert!(
        cmake.contains(&format!(
            "set(REPS2_PARENT_ISO_SHA256 \"{expected_iso_digest}\")"
        )),
        "the native build must contain the hash of the original ISO bytes"
    );

    let mut different_elf = elf.clone();
    different_elf[100] ^= 0x42; // Valid ELF with distinct boot-image bytes.
    let mismatch_temp = TempDir::new();
    let (output, generated_cpp) = run_recompile(&mismatch_temp, &different_elf, Some(&iso));
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("does not match extracted boot ELF"));
    assert!(
        !generated_cpp.exists(),
        "rejected provenance must not write generated C++"
    );
    assert!(!generated_cpp.with_extension("manifest.json").exists());
    assert!(!generated_cpp.with_file_name("output.native").exists());
}

#[test]
fn parent_iso_rejects_corrupt_images_and_missing_boot_files() {
    let temp = TempDir::new();
    let elf = synthetic_elf();

    let (output, _) = run_recompile(&temp, &elf, Some(b"not an ISO"));
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("ISO"));

    let missing_boot = synthetic_iso(&elf, "MISSING.ELF;1");
    let (output, _) = run_recompile(&temp, &elf, Some(&missing_boot));
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("MISSING.ELF"));
}

#[test]
fn standalone_native_build_keeps_parent_iso_provenance_unavailable() {
    let temp = TempDir::new();
    let elf = synthetic_elf();
    let (output, generated_cpp) = run_recompile(&temp, &elf, None);
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let native_dir = generated_cpp.with_file_name("output.native");
    let cmake = fs::read_to_string(native_dir.join("CMakeLists.txt")).expect("generated CMake");
    assert!(cmake.contains("set(REPS2_PARENT_ISO_SHA256 \"\")"));
}

#[test]
fn elf_info_prints_api_compatibility_findings_and_blocked_stage() {
    let temp = TempDir::new();
    let input = temp.0.join("input.elf");
    fs::write(&input, synthetic_elf()).expect("write ELF");
    let output = Command::new(env!("CARGO_BIN_EXE_reps2"))
        .arg("elf-info")
        .arg(input)
        .output()
        .expect("run elf-info");
    assert!(output.status.success());
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Compatibility report (ELF metadata only"),
        "{stdout}"
    );
    assert!(stdout.contains("Severity: Advisory"), "{stdout}");
    assert!(
        stdout.contains("Diagnostic code: MISSING-REGINFO"),
        "{stdout}"
    );
    assert!(
        stdout.contains("Blocked stage: Some(Translation)"),
        "{stdout}"
    );
    assert!(stdout.contains("Severity: Incompatible"), "{stdout}");
    assert!(
        stdout.contains("Diagnostic code: NOT-MIPS-III-OR-COMPATIBLE"),
        "{stdout}"
    );
    assert!(
        stdout.contains("Blocked stage: Some(CompatibilityConfidence)"),
        "{stdout}"
    );
    assert!(stdout.contains("Explanation:"), "{stdout}");
    assert!(
        stdout.contains("not a boot or playability result"),
        "{stdout}"
    );
    assert!(!stdout.contains("REPS2 support: yes"), "{stdout}");
}

#[test]
fn analyze_elf_prints_compatibility_report_before_analysis() {
    let temp = TempDir::new();
    let input = temp.0.join("input.elf");
    fs::write(&input, synthetic_elf()).expect("write ELF");
    let output = Command::new(env!("CARGO_BIN_EXE_reps2"))
        .arg("analyze-elf")
        .arg(input)
        .output()
        .expect("run analyze-elf");
    assert!(output.status.success());
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("Compatibility report (ELF metadata only"),
        "{stdout}"
    );
    assert!(stdout.contains("MISSING-REGINFO"), "{stdout}");
    assert!(!stdout.contains("Playable: yes"), "{stdout}");
}

#[test]
fn recompile_iso_prepares_verified_native_module_artifacts_without_overwriting() {
    let temp = TempDir::new();
    let elf = synthetic_elf();
    let iso = synthetic_iso(&elf, "BOOT.ELF;1");
    let iso_path = temp.0.join("owned-game.iso");
    let output_dir = temp.0.join("native-build");
    fs::write(&iso_path, &iso).expect("write synthetic PS2 ISO");

    let expected_iso_sha256 =
        reps2_frontend::digest::hex_digest(&reps2_frontend::digest::sha256_bytes(&iso));
    let output = Command::new(env!("CARGO_BIN_EXE_reps2"))
        .arg("recompile-iso")
        .arg(&iso_path)
        .arg(&output_dir)
        .args(["--tu-count", "1", "--probe-budget", "64"])
        .output()
        .expect("run recompile-iso");
    assert!(
        output.status.success(),
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    assert_eq!(fs::read(&iso_path).expect("source ISO remains intact"), iso);
    assert_eq!(
        fs::read(output_dir.join("boot.elf")).expect("extracted boot ELF"),
        elf
    );
    assert_eq!(
        fs::metadata(output_dir.join("boot.ram.bin"))
            .expect("initial RAM image")
            .len(),
        32 * 1024 * 1024
    );
    assert!(output_dir.join("boot.cpp").is_file());
    assert!(output_dir.join("boot.manifest.json").is_file());
    assert!(output_dir.join("boot.native/CMakeLists.txt").is_file());
    assert!(output_dir.join("boot.native/probe_main.cpp").is_file());
    assert!(output_dir.join("boot.native/rg_reps2_module.cpp").is_file());

    let cmake = fs::read_to_string(output_dir.join("boot.native/CMakeLists.txt"))
        .expect("native CMake project");
    assert!(cmake.contains("add_library(reps2_native_module SHARED"));
    assert!(cmake.contains("set(REPS2_PARENT_ISO_SHA256"));
    assert!(cmake.contains(&expected_iso_sha256));

    let manifest = fs::read_to_string(output_dir.join("boot.manifest.json"))
        .expect("deterministic build manifest");
    assert!(manifest.contains("\"playable\": false"));
    assert!(manifest.contains("\"input\": {"));
    assert!(manifest.contains("\"sha256\":"));

    // Reusing the same output directory is refused; the verified build is
    // byte-identical after the rejected attempt.
    let manifest_before = fs::read(output_dir.join("boot.manifest.json")).expect("manifest bytes");
    let second = Command::new(env!("CARGO_BIN_EXE_reps2"))
        .arg("recompile-iso")
        .arg(&iso_path)
        .arg(&output_dir)
        .output()
        .expect("run duplicate recompile-iso");
    assert!(!second.status.success());
    assert!(String::from_utf8_lossy(&second.stderr).contains("output directory already exists"));
    assert_eq!(
        fs::read(output_dir.join("boot.manifest.json")).expect("manifest remains intact"),
        manifest_before
    );
}

#[test]
fn recompile_iso_removes_incomplete_output_when_boot_extraction_fails() {
    let temp = TempDir::new();
    let invalid_iso = temp.0.join("invalid.iso");
    let output_dir = temp.0.join("should-not-publish");
    fs::write(&invalid_iso, vec![0u8; 17 * BLOCK + 1]).expect("write invalid image");

    let output = Command::new(env!("CARGO_BIN_EXE_reps2"))
        .arg("recompile-iso")
        .arg(&invalid_iso)
        .arg(&output_dir)
        .output()
        .expect("run recompile-iso with invalid image");
    assert!(!output.status.success());
    assert!(
        !output_dir.exists(),
        "failed preparation must clean its output directory"
    );
    assert!(
        String::from_utf8_lossy(&output.stderr).contains("2048-byte-sector ISO"),
        "stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
}
