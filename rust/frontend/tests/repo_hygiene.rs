// SPDX-License-Identifier: GPL-3.0-or-later
//! Guards that keep repository metadata consistent with the code.

use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;

fn repository_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
}

fn read(relative: &str) -> String {
    let path = repository_root().join(relative);
    fs::read_to_string(&path).unwrap_or_else(|error| panic!("{}: {error}", path.display()))
}

#[test]
fn readme_status_line_reports_the_crate_version() {
    let expected = format!("(v{})", reps2_frontend::VERSION);
    assert!(
        read("README.md").contains(&expected),
        "README.md status line must contain {expected}; update it when the crate version changes"
    );
}

#[test]
fn readme_documents_the_declared_minimum_rust_version() {
    let manifest = read("rust/frontend/Cargo.toml");
    let msrv = manifest
        .lines()
        .find_map(|line| line.strip_prefix("rust-version = "))
        .map(|value| value.trim().trim_matches('"').to_owned())
        .expect("rust/frontend/Cargo.toml declares rust-version");
    assert!(
        read("README.md").contains(&format!("Rust {msrv} or newer")),
        "README.md requirements must say \"Rust {msrv} or newer\""
    );
    assert!(
        read(".github/workflows/ci.yml").contains(&format!("dtolnay/rust-toolchain@{msrv}")),
        "CI must build with the declared MSRV {msrv}"
    );
}

#[test]
fn adr_numbers_are_unique_and_match_titles() {
    let directory = repository_root().join("docs").join("decisions");
    let mut seen: BTreeMap<String, String> = BTreeMap::new();
    for entry in fs::read_dir(&directory).expect("docs/decisions exists") {
        let entry = entry.expect("directory entry");
        let name = entry.file_name().to_string_lossy().into_owned();
        let Some(number) = name
            .get(..4)
            .filter(|n| n.chars().all(|c| c.is_ascii_digit()))
        else {
            continue; // README.md and other non-record files
        };
        let text = fs::read_to_string(entry.path()).expect("ADR is readable");
        let title = text.lines().next().unwrap_or_default();
        assert!(
            title.starts_with(&format!("# ADR {number}:")),
            "{name}: first line must start with \"# ADR {number}:\" but is {title:?}"
        );
        if let Some(previous) = seen.insert(number.to_owned(), name.clone()) {
            panic!("ADR number {number} is used by both {previous} and {name}");
        }
    }
    assert!(seen.len() >= 36, "expected the existing ADRs to be found");
}
