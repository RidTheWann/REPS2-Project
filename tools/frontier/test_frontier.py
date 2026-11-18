"""Tests for the local frontier probe tool. Synthetic values only."""

from __future__ import annotations

import copy
import json
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import frontier  # noqa: E402

SHA = "a" * 64
COMMIT = "0" * 40
FRONTEND_VERSION = "REPS2 0.7.0 (Rust frontend, experimental)"
ENGINE_VERSION = "RG REPS2 ENGINE v0.2.0 (ABI 2)"


def record() -> dict:
    return {
        "schema_version": 2,
        "kind": "reps2.frontier.v2",
        "title": {"serial": "TEST_000.01", "iso_sha256": SHA, "boot_elf_sha256": SHA},
        "source": {
            "reps2_commit": COMMIT,
            "reps2_version": "0.7.0",
            "frontend_reported_version": FRONTEND_VERSION,
            "frontend_sha256": SHA,
            "engine_sha256": SHA,
            "runtime_abi": 19,
            "toolchain_headers_sha256": SHA,
            "manifest_sha256": SHA,
            "manifest_normalized_sha256": SHA,
            "native_module_sha256": SHA,
            "ram_image_sha256": SHA,
        },
        "host": {
            "os": "windows",
            "architecture": "x64",
            "compiler": "MSVC",
            "compiler_version": "19.51.36260.0",
            "cxx_standard": 20,
        },
        "configuration": {
            "discovery": True,
            "tu_count": 8,
            "hle_enabled": True,
            "instruction_budget": 5000000,
            "max_batches": 16,
            "extra_roots": ["0x0000000000246CD0"],
            "relocated_regions": ["0x80076000:0x002829F8:0x740"],
        },
        "result": {
            "outcome": "guest_stopped",
            "guest_status": "semaphore_wait_blocked",
            "guest_status_code": 16,
            "stop_reason": "semaphore_wait_blocked",
            "final_pc": "0x000000000025A724",
            "instructions_executed": 4586235,
            "batches": 1,
            "syscalls_seen": 1221,
            "syscalls_handled": 1203,
            "callbacks_completed": 78,
            "rpc_call_number": 255,
            "rpc_call_replied": 0,
        },
        "evidence": {
            "report_sha256": SHA,
            "manifest_census": {
                "basic_blocks": 60984,
                "guest_instructions": 317701,
                "unsupported_stops": 622,
                "guest_exceptions": 258,
                "unresolved_indirect_targets": 625,
                "translation_units": 9,
                "accepted_roots": 7170,
            },
            "reproducible": False,
        },
    }


class ValidateTests(unittest.TestCase):
    def test_valid_record_has_no_errors(self) -> None:
        self.assertEqual(frontier.validate(record()), [])

    def test_missing_section_is_reported(self) -> None:
        broken = record()
        del broken["result"]
        self.assertIn("record.result is missing", frontier.validate(broken))

    def test_unknown_outcome_is_rejected(self) -> None:
        broken = record()
        broken["result"]["outcome"] = "booted"
        self.assertTrue(any("result.outcome" in error for error in frontier.validate(broken)))

    def test_budget_outcome_must_not_claim_completion(self) -> None:
        broken = record()
        broken["result"]["outcome"] = "guest_stopped"
        broken["result"]["stop_reason"] = "guest_completed"
        self.assertTrue(any("guest_completed" in error for error in frontier.validate(broken)))

    def test_short_digest_is_rejected(self) -> None:
        broken = record()
        broken["title"]["iso_sha256"] = "abc"
        self.assertIn("title.iso_sha256 is not a lowercase SHA-256", frontier.validate(broken))

    def test_absolute_path_is_rejected(self) -> None:
        leaky = record()
        leaky["title"]["serial"] = "D:\\List Games\\Some Game.iso"
        self.assertTrue(any("absolute path" in error for error in frontier.validate(leaky)))

    def test_uppercase_address_is_rejected(self) -> None:
        broken = record()
        broken["result"]["final_pc"] = "0x0000000025a724"
        self.assertIn("result.final_pc is not 0x + 16 hex digits", frontier.validate(broken))

    def test_zero_batches_is_rejected(self) -> None:
        broken = record()
        broken["result"]["batches"] = 0
        self.assertTrue(any("batches" in error for error in frontier.validate(broken)))

    def test_schema_file_matches_validator_fields(self) -> None:
        schema = json.loads((Path(__file__).resolve().parent / "frontier.schema.json").read_text("utf-8"))
        self.assertEqual(schema["properties"]["schema_version"]["const"], frontier.SCHEMA_VERSION)
        self.assertEqual(schema["properties"]["kind"]["const"], frontier.KIND)
        self.assertEqual(set(schema["required"]), set(record()))
        for section in ("title", "source", "host", "configuration", "result", "evidence"):
            self.assertEqual(set(schema["properties"][section]["required"]), set(record()[section]),
                             section)

    def test_missing_provenance_field_is_rejected(self) -> None:
        broken = record()
        del broken["source"]["engine_sha256"]
        self.assertTrue(any("source.engine_sha256" in error for error in frontier.validate(broken)))

    def test_previous_schema_version_is_rejected(self) -> None:
        broken = record()
        broken["schema_version"] = 1
        broken["kind"] = "reps2.frontier.v1"
        errors = frontier.validate(broken)
        self.assertTrue(any("schema_version" in error for error in errors))
        self.assertTrue(any("kind" in error for error in errors))


class CompareTests(unittest.TestCase):
    def test_identical_records_have_no_difference(self) -> None:
        self.assertEqual(frontier.semantic_diff(record(), copy.deepcopy(record())), [])

    def test_result_change_is_a_difference(self) -> None:
        other = copy.deepcopy(record())
        other["result"]["final_pc"] = "0x000000000025A728"
        self.assertTrue(frontier.semantic_diff(record(), other))

    def test_report_digest_alone_is_not_a_difference(self) -> None:
        other = copy.deepcopy(record())
        other["evidence"]["report_sha256"] = "b" * 64
        self.assertEqual(frontier.semantic_diff(record(), other), [])

    def test_raw_manifest_digest_alone_is_not_a_difference(self) -> None:
        other = copy.deepcopy(record())
        other["source"]["manifest_sha256"] = "b" * 64
        self.assertEqual(frontier.semantic_diff(record(), other), [])

    def test_reproducible_flag_alone_is_not_a_difference(self) -> None:
        other = copy.deepcopy(record())
        other["evidence"]["reproducible"] = True
        self.assertEqual(frontier.semantic_diff(record(), other), [])

    def test_host_change_is_a_difference(self) -> None:
        other = copy.deepcopy(record())
        other["host"]["compiler_version"] = "19.40.00000.0"
        self.assertTrue(any("host.compiler_version" in line
                            for line in frontier.semantic_diff(record(), other)))

    def test_census_change_is_a_difference(self) -> None:
        other = copy.deepcopy(record())
        other["evidence"]["manifest_census"]["unsupported_stops"] = 623
        self.assertTrue(any("evidence.manifest_census.unsupported_stops" in line
                            for line in frontier.semantic_diff(record(), other)))

    def test_native_module_digest_change_is_a_difference(self) -> None:
        other = copy.deepcopy(record())
        other["source"]["native_module_sha256"] = "b" * 64
        self.assertTrue(any("source.native_module_sha256" in line
                            for line in frontier.semantic_diff(record(), other)))

    def test_normalized_manifest_digest_change_is_a_difference(self) -> None:
        other = copy.deepcopy(record())
        other["source"]["manifest_normalized_sha256"] = "b" * 64
        self.assertTrue(any("source.manifest_normalized_sha256" in line
                            for line in frontier.semantic_diff(record(), other)))

    def test_one_sided_missing_key_is_a_difference(self) -> None:
        left = record()
        right = record()
        del left["title"]["serial"]
        self.assertTrue(any("title.serial" in line
                            for line in frontier.semantic_diff(left, right)))

    def test_two_identically_malformed_records_are_rejected(self) -> None:
        # A field missing from both records is invisible to a pairwise walk, so the
        # comparison itself must refuse records that fail validation.
        left = record()
        right = record()
        del left["title"]["serial"]
        del right["title"]["serial"]
        self.assertEqual(frontier.semantic_diff(left, right), [])
        self.assertEqual(frontier.finish_compare(left, right), 6)

    def test_extra_key_is_a_difference(self) -> None:
        other = copy.deepcopy(record())
        other["result"]["unexpected"] = 1
        self.assertTrue(any("result.unexpected" in line
                            for line in frontier.semantic_diff(record(), other)))


class ManifestNormalizationTests(unittest.TestCase):
    def manifest(self, elf_path: str) -> dict:
        return {
            "manifest_version": 3,
            "input": {"path": elf_path, "size_bytes": 1691684, "sha256": SHA},
            "artifacts": {"initial_ee_ram": "boot.ram.bin", "ram_image_bytes": 33554432},
        }

    def test_output_directory_does_not_change_the_normalized_digest(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            first = Path(folder) / "probe-01"
            second = Path(folder) / "probe-02"
            first.mkdir()
            second.mkdir()
            (first / "manifest.json").write_text(
                json.dumps(self.manifest(str(first / "boot.elf"))), encoding="utf-8")
            (second / "manifest.json").write_text(
                json.dumps(self.manifest(str(second / "boot.elf"))), encoding="utf-8")
            self.assertNotEqual(frontier.sha256_file(first / "manifest.json"),
                               frontier.sha256_file(second / "manifest.json"))
            self.assertEqual(frontier.normalized_manifest_digest(first / "manifest.json"),
                             frontier.normalized_manifest_digest(second / "manifest.json"))

    def test_uncovered_absolute_path_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "manifest.json"
            manifest = self.manifest(str(Path(folder) / "boot.elf"))
            manifest["artifacts"]["initial_ee_ram"] = "D:\\elsewhere\\boot.ram.bin"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(frontier.StageError) as context:
                frontier.normalized_manifest_digest(path)
            self.assertEqual(context.exception.code, 4)


class ProvenanceTests(unittest.TestCase):
    def args(self, frontend: str = FRONTEND_VERSION, engine: str = ENGINE_VERSION) -> types.SimpleNamespace:
        return types.SimpleNamespace(frontend_reported_version=frontend,
                                     engine_reported_version=engine)

    def report(self) -> dict:
        return {"engine": {"version": "0.2.0"}}

    def test_version_query_returns_the_first_reported_line(self) -> None:
        completed = types.SimpleNamespace(stdout="\n" + FRONTEND_VERSION + "\n")
        with mock.patch.object(frontier, "run", return_value=completed) as runner:
            self.assertEqual(frontier.executable_version("reps2.exe", "version", "frontend"),
                             FRONTEND_VERSION)
        runner.assert_called_once_with(["reps2.exe", "version"], "frontend version query", 8)

    def test_version_query_without_output_fails_closed(self) -> None:
        completed = types.SimpleNamespace(stdout="\n")
        with mock.patch.object(frontier, "run", return_value=completed):
            with self.assertRaises(frontier.StageError) as context:
                frontier.executable_version("reps2.exe", "version", "frontend")
        self.assertEqual(context.exception.code, 8)

    def test_matching_provenance_passes(self) -> None:
        frontier.verify_provenance(self.args(), {"tool": {"version": "0.7.0"}}, self.report())

    def test_frontend_version_mismatch_fails_closed(self) -> None:
        args = self.args(frontend="REPS2 0.6.0 (Rust frontend, experimental)")
        with self.assertRaises(frontier.StageError) as context:
            frontier.verify_provenance(args, {"tool": {"version": "0.7.0"}}, self.report())
        self.assertEqual(context.exception.code, 8)

    def test_engine_version_mismatch_fails_closed(self) -> None:
        args = self.args(engine="RG REPS2 ENGINE v0.1.0 (ABI 1)")
        with self.assertRaises(frontier.StageError) as context:
            frontier.verify_provenance(args, {"tool": {"version": "0.7.0"}}, self.report())
        self.assertEqual(context.exception.code, 8)


class CliTests(unittest.TestCase):
    def test_validate_subcommand_accepts_valid_record(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "frontier.json"
            path.write_text(json.dumps(record()), encoding="utf-8")
            self.assertEqual(frontier.main(["validate", str(path)]), 0)

    def test_validate_subcommand_rejects_invalid_record(self) -> None:
        broken = record()
        broken["source"]["runtime_abi"] = "19"
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "frontier.json"
            path.write_text(json.dumps(broken), encoding="utf-8")
            self.assertEqual(frontier.main(["validate", str(path)]), 6)

    def test_probe_rejects_hash_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            iso = Path(folder) / "synthetic.iso"
            iso.write_bytes(b"\0" * 4096)
            code = frontier.main([
                "probe", "--iso", str(iso), "--serial", "TEST_000.01",
                "--expect-iso-sha256", SHA, "--out", str(Path(folder) / "out"),
                "--reps2", "reps2", "--engine", "engine",
                "--runtime-include", folder, "--engine-include", folder,
                "--reps2-commit", COMMIT,
            ])
            self.assertEqual(code, 2)
            self.assertFalse((Path(folder) / "out").exists())

    def test_compare_subcommand_reports_no_difference(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            first = Path(folder) / "first.json"
            second = Path(folder) / "second.json"
            first.write_text(json.dumps(record()), encoding="utf-8")
            second.write_text(json.dumps(record()), encoding="utf-8")
            self.assertEqual(frontier.main(["compare", str(first), str(second)]), 0)

    def test_compare_subcommand_reports_a_difference(self) -> None:
        changed = record()
        changed["result"]["instructions_executed"] += 1
        with tempfile.TemporaryDirectory() as folder:
            first = Path(folder) / "first.json"
            second = Path(folder) / "second.json"
            first.write_text(json.dumps(record()), encoding="utf-8")
            second.write_text(json.dumps(changed), encoding="utf-8")
            self.assertEqual(frontier.main(["compare", str(first), str(second)]), 7)

    def test_compare_subcommand_rejects_a_malformed_record(self) -> None:
        broken = record()
        del broken["source"]["engine_sha256"]
        with tempfile.TemporaryDirectory() as folder:
            first = Path(folder) / "first.json"
            second = Path(folder) / "second.json"
            first.write_text(json.dumps(record()), encoding="utf-8")
            second.write_text(json.dumps(broken), encoding="utf-8")
            self.assertEqual(frontier.main(["compare", str(first), str(second)]), 6)


if __name__ == "__main__":
    unittest.main()