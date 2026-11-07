// SPDX-License-Identifier: GPL-3.0-or-later
//! CLI regressions for fatal analysis limits on speculative continuations.

use std::fs;
use std::path::PathBuf;
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

fn synthetic_syscall_elf() -> Vec<u8> {
    let mut elf = vec![0; 52 + 32 + 12];
    elf[..16].copy_from_slice(b"\x7FELF\x01\x01\x01\0\0\0\0\0\0\0\0\0");
    put_u16(&mut elf, 16, 2); // ET_EXEC
    put_u16(&mut elf, 18, 8); // EM_MIPS
    put_u32(&mut elf, 20, 1);
    put_u32(&mut elf, 24, 0x0010_0000);
    put_u32(&mut elf, 28, 52);
    put_u16(&mut elf, 40, 52);
    put_u16(&mut elf, 42, 32);
    put_u16(&mut elf, 44, 1);
    put_u32(&mut elf, 52, 1); // PT_LOAD
    put_u32(&mut elf, 56, 84);
    put_u32(&mut elf, 60, 0x0010_0000);
    put_u32(&mut elf, 64, 0x0010_0000);
    put_u32(&mut elf, 68, 12);
    put_u32(&mut elf, 72, 12);
    put_u32(&mut elf, 76, 5); // PF_R | PF_X
    put_u32(&mut elf, 80, 4);
    elf[84..96].copy_from_slice(&[
        0x0c, 0, 0, 0, // syscall
        0, 0, 0, 0, // nop
        0, 0, 0, 0, // nop
    ]);
    elf
}

fn put_u16(bytes: &mut [u8], offset: usize, value: u16) {
    bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}

fn put_u32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

struct TempDir(PathBuf);

impl TempDir {
    fn new() -> Self {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system clock")
            .as_nanos();
        let path = std::env::temp_dir().join(format!(
            "reps2-analysis-limit-cli-{}-{nonce}",
            std::process::id()
        ));
        fs::create_dir(&path).expect("create temporary test directory");
        Self(path)
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

#[test]
fn analyze_elf_fails_closed_at_syscall_continuation_with_or_without_discovery() {
    let temp = TempDir::new();
    let input = temp.0.join("synthetic-syscall.elf");
    fs::write(&input, synthetic_syscall_elf()).expect("write synthetic ELF");

    for discover in [false, true] {
        let logs = temp.0.join(if discover { "discover-logs" } else { "logs" });
        let mut command = Command::new(env!("CARGO_BIN_EXE_reps2"));
        command
            .arg("--log-dir")
            .arg(&logs)
            .arg("analyze-elf")
            .arg(&input)
            .arg("--max-instructions")
            .arg("1");
        if discover {
            command.arg("--discover");
        }
        let output = command.output().expect("run analyze-elf");
        assert!(!output.status.success(), "discover={discover}");
        let stderr = String::from_utf8_lossy(&output.stderr);
        assert!(
            stderr.contains("analysis exceeded the 1 instruction limit; raise --max-instructions above 1 to analyze more"),
            "discover={discover}: {stderr}"
        );

        let session = fs::read_dir(&logs)
            .expect("diagnostics root")
            .next()
            .expect("one diagnostics session")
            .expect("session directory")
            .path();
        let events = fs::read_to_string(session.join("events.jsonl")).expect("events");
        assert!(events.contains("\"event\":\"pipeline.stage.failed\""));
        assert!(events.contains("\"stage_id\":\"cfg.discover\""));
        assert!(events.contains("\"subsystem\":\"analysis\""));
        assert!(events.contains("\"error_code\":\"REPS2_PIPELINE_STAGE_FAILED\""));
    }
}
