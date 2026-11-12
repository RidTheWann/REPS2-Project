// SPDX-License-Identifier: GPL-3.0-or-later
//! End-to-end check that `reps2 dev capabilities` emits exactly one stable,
//! valid JSON document and that its published envelope kinds match the schema.

use std::fs;
use std::path::PathBuf;
use std::process::Command;

fn repository_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
}

fn run_capabilities() -> (String, String) {
    let output = Command::new(env!("CARGO_BIN_EXE_reps2"))
        .arg("dev")
        .arg("capabilities")
        .output()
        .expect("run reps2 dev capabilities");
    assert!(
        output.status.success(),
        "dev capabilities must exit 0; stderr: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    (
        String::from_utf8(output.stdout).expect("utf8 stdout"),
        String::from_utf8_lossy(&output.stderr).into_owned(),
    )
}

/// Extracts a flat array of JSON strings for `key` without a JSON dependency.
fn extract_string_array(document: &str, key: &str) -> Vec<String> {
    let marker = format!("\"{key}\":[");
    let start = document
        .find(&marker)
        .unwrap_or_else(|| panic!("missing {key} in capabilities output"))
        + marker.len();
    let end = document[start..]
        .find(']')
        .map(|offset| start + offset)
        .expect("array is terminated");
    document[start..end]
        .split(',')
        .map(|item| item.trim().trim_matches('"').to_owned())
        .filter(|item| !item.is_empty())
        .collect()
}

#[test]
fn capabilities_command_emits_one_stable_valid_report() {
    let (stdout, stderr) = run_capabilities();
    assert!(stderr.is_empty(), "capabilities wrote to stderr: {stderr}");
    assert_eq!(
        stdout.lines().count(),
        1,
        "capabilities must print a single JSON document"
    );

    let trimmed = stdout.trim();
    assert!(
        trimmed.starts_with('{') && trimmed.ends_with('}'),
        "capabilities output is not a JSON object"
    );
    assert!(trimmed.contains("\"kind\":\"reps2.developer.capabilities\""));
    assert!(trimmed.contains("\"schema_version\":1"));
    assert!(trimmed.contains("\"report_schema_version\":1"));
    assert!(trimmed.contains("\"event_schema_version\":1"));
    assert!(trimmed.contains("\"session_manifest_schema_version\":1"));

    // Determinism: no timestamps or run identifiers are present.
    let (second, _) = run_capabilities();
    assert_eq!(stdout, second, "capabilities output is not deterministic");

    // Every published envelope kind is part of the JSON schema contract.
    let schema =
        fs::read_to_string(repository_root().join("docs/schemas/developer-report-v1.schema.json"))
            .expect("read developer report schema");
    let kinds = extract_string_array(trimmed, "envelope_kinds");
    assert!(!kinds.is_empty(), "no envelope kinds published");
    for kind in &kinds {
        assert!(
            schema.contains(kind.as_str()),
            "schema is missing envelope kind {kind}"
        );
    }

    // The stable diagnostic-code catalog is surfaced and namespaced.
    for code in [
        "REPS2_PIPELINE_STAGE_FAILED",
        "REPS2_COMMAND_FAILED",
        "REPS2_DEV_INVALID_ARGUMENTS",
        "REPS2_DEV_SESSION_LIST_FAILED",
        "REPS2_DEV_SESSION_INSPECTION_FAILED",
    ] {
        assert!(trimmed.contains(code), "missing diagnostic code {code}");
    }
}
