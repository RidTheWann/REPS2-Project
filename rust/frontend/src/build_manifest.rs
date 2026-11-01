// SPDX-License-Identifier: GPL-3.0-or-later
//! Deterministic machine-readable build manifest for `recompile-elf`.
//!
//! The manifest is the second half of the documented frontend/native boundary:
//! generated C++ is compiled by the host toolchain, while this JSON record names
//! the exact inputs, analysis limits, discovered program shape, and translation
//! layout that produced it. Rendering is deterministic: a repeated invocation
//! with the same arguments and inputs yields byte-identical output. No
//! timestamps, machine names, or random values are recorded.

use crate::analysis::AnalysisLimits;
use crate::json::json_string;
use crate::{BUILD_TARGET, VERSION};

/// Schema version of the build manifest rendered by this module.
pub const MANIFEST_VERSION: u32 = 1;

/// Bounded analysis limits that produced the program.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ManifestLimits {
    pub max_loaded_memory: usize,
    pub max_instructions: usize,
    pub max_blocks: usize,
    pub max_aliased_roots: u8,
    pub max_function_roots: usize,
}

impl From<AnalysisLimits> for ManifestLimits {
    fn from(limits: AnalysisLimits) -> Self {
        Self {
            max_loaded_memory: limits.max_loaded_memory,
            max_instructions: limits.max_instructions,
            max_blocks: limits.max_blocks,
            max_aliased_roots: limits.max_aliased_roots,
            max_function_roots: limits.max_function_roots,
        }
    }
}

/// Static function-discovery outcome, present only when discovery ran.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DiscoveryStats {
    /// Distinct candidate addresses found by the static scan.
    pub candidate_count: usize,
    /// Candidates supplied by `JAL` targets.
    pub call_target_count: usize,
    /// Candidates supplied by ABI stack prologues.
    pub prologue_candidate_count: usize,
    /// Aligned words examined by the scan.
    pub scanned_words: u64,
    /// Whether the candidate limit stopped the scan early.
    pub truncated: bool,
    /// Candidates accepted after CFG validation.
    pub accepted_root_count: usize,
    /// Candidates rejected after CFG validation.
    pub skipped_root_count: usize,
    /// Heuristic blocks skipped during graph construction.
    pub skipped_block_count: usize,
}

/// Analysis and translation counts for the emitted program.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProgramStats {
    pub entry_point_count: usize,
    pub basic_block_count: usize,
    pub guest_instruction_count: usize,
    pub unsupported_stop_count: usize,
    pub static_edge_count: usize,
    pub unresolved_indirect_target_count: usize,
    pub external_target_count: usize,
}

/// Everything recorded in `<stem>.manifest.json`.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BuildManifest {
    pub input_path: String,
    pub input_size_bytes: u64,
    pub input_sha256: String,
    pub entry_point: u32,
    pub machine: u16,
    pub file_type: u16,
    pub loadable_segment_count: usize,
    pub limits: ManifestLimits,
    pub discovery: Option<DiscoveryStats>,
    pub program: ProgramStats,
    pub translation_unit_count: usize,
    pub primary_output: String,
    pub part_outputs: Vec<String>,
    pub ram_output: String,
    pub ram_image_bytes: usize,
}

impl BuildManifest {
    /// Renders the manifest as deterministic JSON.
    ///
    /// `playable` is always `false`: analysis success and generated C++ are not
    /// evidence that a title can boot.
    #[must_use]
    pub fn to_json(&self) -> String {
        let mut output = String::new();
        output.push_str("{\n");
        output.push_str(&format!("  \"manifest_version\": {MANIFEST_VERSION},\n"));
        output.push_str("  \"tool\": {\n");
        output.push_str("    \"name\": \"reps2-frontend\",\n");
        output.push_str(&format!("    \"version\": {},\n", json_string(VERSION)));
        output.push_str("    \"host_target\": ");
        output.push_str(&json_string(BUILD_TARGET));
        output.push_str("\n  },\n");

        output.push_str("  \"input\": {\n");
        output.push_str(&format!(
            "    \"path\": {},\n",
            json_string(&self.input_path)
        ));
        output.push_str(&format!("    \"size_bytes\": {},\n", self.input_size_bytes));
        output.push_str(&format!(
            "    \"sha256\": {}\n",
            json_string(&self.input_sha256)
        ));
        output.push_str("  },\n");

        output.push_str("  \"guest\": {\n");
        output.push_str(&format!(
            "    \"entry_point\": \"0x{:08X}\",\n",
            self.entry_point
        ));
        output.push_str(&format!("    \"machine\": {},\n", self.machine));
        output.push_str(&format!("    \"file_type\": {},\n", self.file_type));
        output.push_str(&format!(
            "    \"loadable_segment_count\": {}\n",
            self.loadable_segment_count
        ));
        output.push_str("  },\n");

        output.push_str("  \"analysis\": {\n");
        output.push_str("    \"limits\": {\n");
        output.push_str(&format!(
            "      \"max_loaded_memory\": {},\n",
            self.limits.max_loaded_memory
        ));
        output.push_str(&format!(
            "      \"max_instructions\": {},\n",
            self.limits.max_instructions
        ));
        output.push_str(&format!(
            "      \"max_blocks\": {},\n",
            self.limits.max_blocks
        ));
        output.push_str(&format!(
            "      \"max_aliased_roots\": {},\n",
            self.limits.max_aliased_roots
        ));
        output.push_str(&format!(
            "      \"max_function_roots\": {}\n",
            self.limits.max_function_roots
        ));
        output.push_str("    },\n");
        output.push_str("    \"discovery\": ");
        match &self.discovery {
            None => output.push_str("null\n"),
            Some(discovery) => {
                output.push_str("{\n");
                output.push_str(&format!(
                    "      \"candidate_count\": {},\n",
                    discovery.candidate_count
                ));
                output.push_str(&format!(
                    "      \"call_target_count\": {},\n",
                    discovery.call_target_count
                ));
                output.push_str(&format!(
                    "      \"prologue_candidate_count\": {},\n",
                    discovery.prologue_candidate_count
                ));
                output.push_str(&format!(
                    "      \"scanned_words\": {},\n",
                    discovery.scanned_words
                ));
                output.push_str(&format!("      \"truncated\": {},\n", discovery.truncated));
                output.push_str(&format!(
                    "      \"accepted_root_count\": {},\n",
                    discovery.accepted_root_count
                ));
                output.push_str(&format!(
                    "      \"skipped_root_count\": {},\n",
                    discovery.skipped_root_count
                ));
                output.push_str(&format!(
                    "      \"skipped_block_count\": {}\n",
                    discovery.skipped_block_count
                ));
                output.push_str("    }\n");
            }
        }
        output.push_str("  },\n");

        output.push_str("  \"program\": {\n");
        output.push_str(&format!(
            "    \"entry_point_count\": {},\n",
            self.program.entry_point_count
        ));
        output.push_str(&format!(
            "    \"basic_block_count\": {},\n",
            self.program.basic_block_count
        ));
        output.push_str(&format!(
            "    \"guest_instruction_count\": {},\n",
            self.program.guest_instruction_count
        ));
        output.push_str(&format!(
            "    \"unsupported_stop_count\": {},\n",
            self.program.unsupported_stop_count
        ));
        output.push_str(&format!(
            "    \"static_edge_count\": {},\n",
            self.program.static_edge_count
        ));
        output.push_str(&format!(
            "    \"unresolved_indirect_target_count\": {},\n",
            self.program.unresolved_indirect_target_count
        ));
        output.push_str(&format!(
            "    \"external_target_count\": {}\n",
            self.program.external_target_count
        ));
        output.push_str("  },\n");

        output.push_str("  \"translation\": {\n");
        output.push_str(&format!(
            "    \"translation_unit_count\": {},\n",
            self.translation_unit_count
        ));
        output.push_str(&format!(
            "    \"primary_output\": {},\n",
            json_string(&self.primary_output)
        ));
        output.push_str("    \"part_outputs\": [");
        if self.part_outputs.is_empty() {
            output.push_str("]\n");
        } else {
            for (index, part) in self.part_outputs.iter().enumerate() {
                if index != 0 {
                    output.push(',');
                }
                output.push_str("\n      ");
                output.push_str(&json_string(part));
            }
            output.push_str("\n    ]\n");
        }
        output.push_str("  },\n");

        output.push_str("  \"artifacts\": {\n");
        output.push_str(&format!(
            "    \"initial_ee_ram\": {},\n",
            json_string(&self.ram_output)
        ));
        output.push_str(&format!(
            "    \"ram_image_bytes\": {}\n",
            self.ram_image_bytes
        ));
        output.push_str("  },\n");

        output.push_str("  \"playable\": false\n");
        output.push_str("}\n");
        output
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> BuildManifest {
        BuildManifest {
            input_path: "C:\\games\\SLUS_212.34".to_owned(),
            input_size_bytes: 4_096,
            input_sha256: "00".repeat(32),
            entry_point: 0x0010_0008,
            machine: 8,
            file_type: 2,
            loadable_segment_count: 2,
            limits: AnalysisLimits::default().into(),
            discovery: None,
            program: ProgramStats {
                entry_point_count: 1,
                basic_block_count: 9,
                guest_instruction_count: 113,
                unsupported_stop_count: 0,
                static_edge_count: 10,
                unresolved_indirect_target_count: 0,
                external_target_count: 0,
            },
            translation_unit_count: 1,
            primary_output: "game.cpp".to_owned(),
            part_outputs: Vec::new(),
            ram_output: "game.ram.bin".to_owned(),
            ram_image_bytes: 32 * 1024 * 1024,
        }
    }

    #[test]
    fn renders_deterministically() {
        let manifest = sample();
        assert_eq!(manifest.to_json(), manifest.to_json());
    }

    #[test]
    fn records_versioned_identity_and_non_playable_status() {
        let json = sample().to_json();
        assert!(json.contains("\"manifest_version\": 1"));
        assert!(json.contains(&format!("\"version\": \"{VERSION}\"")));
        assert!(json.contains(&format!("\"host_target\": \"{BUILD_TARGET}\"")));
        assert!(json.contains("\"playable\": false"));
        assert!(json.contains("\"input\": {"));
        assert!(json.contains("\"sha256\": \"0000"));
        assert!(json.contains("\"discovery\": null"));
    }

    #[test]
    fn renders_discovery_statistics_when_present() {
        let mut manifest = sample();
        manifest.discovery = Some(DiscoveryStats {
            candidate_count: 5,
            call_target_count: 3,
            prologue_candidate_count: 2,
            scanned_words: 1_024,
            truncated: false,
            accepted_root_count: 4,
            skipped_root_count: 1,
            skipped_block_count: 2,
        });
        let json = manifest.to_json();
        assert!(json.contains("\"candidate_count\": 5"));
        assert!(json.contains("\"accepted_root_count\": 4"));
        assert!(json.contains("\"truncated\": false"));
    }

    #[test]
    fn escapes_paths_in_json_strings() {
        let mut manifest = sample();
        manifest.input_path = "a\"b\\c\n\u{1f}".to_owned();
        let json = manifest.to_json();
        assert!(json.contains("\"path\": \"a\\\"b\\\\c\\n\\u001F\""));
    }

    #[test]
    fn renders_part_outputs_in_order() {
        let mut manifest = sample();
        manifest.translation_unit_count = 3;
        manifest.part_outputs = vec![
            "game.part0001.cpp".to_owned(),
            "game.part0002.cpp".to_owned(),
        ];
        let json = manifest.to_json();
        assert!(json.contains(
            "    \"part_outputs\": [\n      \"game.part0001.cpp\",\n      \"game.part0002.cpp\"\n    ]"
        ));
        assert!(!json.contains("timestamp"));
    }
}
