// SPDX-License-Identifier: GPL-3.0-or-later
//! Synthetic, independently constructed ELF fixture; no game data is used.

use std::fs;
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

fn one_byte_executable_elf() -> [u8; 85] {
    // ELF32 little-endian MIPS ET_EXEC, one PT_LOAD, no section table.
    // The sole file-backed executable byte is at EOF; the other three
    // bytes of p_memsz are BSS, not a complete instruction.
    let mut bytes = [0; 85];
    bytes[..7].copy_from_slice(b"\x7FELF\x01\x01\x01");
    for (offset, value) in [(16, 2u16), (18, 8), (40, 52), (42, 32), (44, 1), (46, 40)] {
        bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
    }
    for (offset, value) in [
        (20, 1u32),
        (24, 0x0010_0000),
        (28, 52),
        (52, 1),
        (56, 84),
        (60, 0x0010_0000),
        (64, 0x0010_0000),
        (68, 1),
        (72, 4),
        (76, 5),
        (80, 4),
    ] {
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    bytes
}

#[test]
fn profile_elf_one_byte_executable_reports_zero_words() {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("system clock")
        .as_nanos();
    let directory =
        std::env::temp_dir().join(format!("reps2-coverage-cli-{}-{nonce}", std::process::id()));
    fs::create_dir(&directory).expect("unique fixture directory");
    let path = directory.join("one-byte.elf");
    let bytes = one_byte_executable_elf();
    fs::write(&path, bytes).expect("write synthetic ELF");
    let expected = "REPS2 decoder coverage profile
  Entry point: 0x00100000
  File-backed executable bytes: 1
  Scanned complete words: 0
  Supported words: 0 (0.00%)
  Trap words (syscall/break): 0 (0.00%)
  Unsupported words: 0 (0.00%)
  Scope: every aligned word in executable PT_LOAD file bytes; embedded data is included and no word is claimed reachable.
  Supported opcodes:
  Unsupported families:
  Unsupported encodings (most frequent first):
  Linear entry scan: entry point is not inside file-backed executable bytes
";

    for capture in [false, true] {
        let log_dir = directory.join("diagnostics");
        let mut command = Command::new(env!("CARGO_BIN_EXE_reps2"));
        if capture {
            command.arg("--log-dir").arg(&log_dir);
        }
        let output = command
            .arg("profile-elf")
            .arg(&path)
            .output()
            .expect("run profile-elf");
        // Preserve the fixture and subprocess output for diagnosing failures.
        fs::write(
            directory.join(format!("stdout-{capture}.txt")),
            &output.stdout,
        )
        .expect("save stdout");
        fs::write(
            directory.join(format!("stderr-{capture}.txt")),
            &output.stderr,
        )
        .expect("save stderr");
        assert!(
            output.status.success(),
            "profile-elf failed: {:?}\n{}\nartifacts: {}",
            output.status,
            String::from_utf8_lossy(&output.stderr),
            directory.display()
        );
        let stdout = String::from_utf8(output.stdout).expect("UTF-8 stdout");
        let profile_output = if capture {
            let (notice, profile) = stdout.split_once('\n').expect("diagnostic notice");
            assert!(notice.starts_with("Diagnostics session: "));
            profile
        } else {
            &stdout
        };
        assert_eq!(
            profile_output.lines().collect::<Vec<_>>(),
            expected.lines().collect::<Vec<_>>()
        );
        assert_eq!(fs::read(&path).expect("unchanged input"), bytes);

        if capture {
            let sessions: Vec<_> = fs::read_dir(&log_dir)
                .expect("diagnostic root")
                .map(|entry| entry.expect("session entry").path())
                .collect();
            assert_eq!(sessions.len(), 1);
            let manifest =
                fs::read_to_string(sessions[0].join("session.json")).expect("session manifest");
            assert!(manifest.contains("\"outcome\": \"succeeded\""));
            let events =
                fs::read_to_string(sessions[0].join("events.jsonl")).expect("diagnostic events");
            for stage in [
                "input.read",
                "elf.parse",
                "elf.target_validation",
                "coverage.profile",
            ] {
                assert_eq!(
                    events
                        .lines()
                        .filter(|line| {
                            line.contains("\"event\":\"pipeline.stage.succeeded\"")
                                && line.contains(&format!("\"stage_id\":\"{stage}\""))
                        })
                        .count(),
                    1,
                    "successful {stage} event missing: {events}"
                );
            }
            assert_eq!(events.matches("\"event\":\"command.succeeded\"").count(), 1);
            assert!(!events.contains("\"severity\":\"error\""));
            assert!(!events.contains("\"event\":\"pipeline.stage.failed\""));
        }
    }
}
