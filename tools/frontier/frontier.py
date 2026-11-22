#!/usr/bin/env python3
"""Deterministic local frontier probe for REPS2 title modules.

Pipeline: recompile-iso -> native build -> engine run -> frontier.json.

The tool only orchestrates existing entry points; it does not decode, translate,
or emulate anything itself. Every stage failure returns a distinct exit code so
callers can separate an analysis failure from a guest execution result.

Exit codes:
    0  probe succeeded and the record validates
    2  invalid input (missing file, ISO digest mismatch, existing output dir)
    3  frontend recompile failed
    4  native module build failed, or the manifest kept an absolute path after
       normalization
    5  engine runner failed (host error, non-zero exit, unparsable report)
    6  produced record, or a record passed to a comparison, failed schema validation
    7  a comparison found a semantic difference
    8  provenance check failed: the frontend executable does not report the
       version the manifest claims, or the engine executable does not report the
       version its own run report claims

The record is deliberately path-free: it binds identities by digest and never
contains a disc path, a user directory, or any guest data.

Provenance: `reps2_commit` is asserted by the caller and cannot be read out of a
binary, so the record also carries `frontend_sha256` and `engine_sha256` of the
exact executables that were invoked. The commit claim is then bound to specific
binaries instead of standing alone.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterator

SCHEMA_VERSION = 2
KIND = "reps2.frontier.v2"
HEX_U64 = re.compile(r"^0x[0-9A-F]{16}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
RELOCATED = re.compile(r"^0x[0-9A-F]{8}:0x[0-9A-F]{8}:0x[0-9A-F]+$")
ABS_PATH = re.compile(r"^(?:[A-Za-z]:[\\/]|/|\\\\)")
VERSION = re.compile(r"(?<![\d.])(\d+\.\d+\.\d+)(?![\d.])")

OUTCOMES = {"guest_stopped", "budget_limit_reached", "guest_completed"}
GUEST_STATUSES = {
    "running",
    "guest_completed",
    "instruction_budget_exhausted",
    "budget_exhausted",
    "unsupported_instruction",
    "unresolved_control_flow",
    "guest_exception",
    "unhandled_syscall",
    "semaphore_wait_blocked",
    "thread_wait_blocked",
    "address_error",
    "unmapped_memory",
    "unsupported_memory_access",
    "invalid_memory_image",
    "invalid_exception_return",
    "syscall_handler_overflow",
    "integer_overflow",
    "undefined_word_operand",
    "syscall",
    "breakpoint",
    "ok",
}

# Fields that legitimately vary between two runs of the same configuration.
# `report_sha256` hashes the run report of that specific execution, which carries
# elapsed time and host detail. `manifest_sha256` is the raw digest of a file that
# embeds the absolute path of the extracted boot ELF, so it varies with the probe
# output directory name; `manifest_normalized_sha256` is the comparable identity.
# `reproducible` is the result of the comparison itself.
VOLATILE_PATHS = frozenset({
    "record.evidence.report_sha256",
    "record.source.manifest_sha256",
    "record.evidence.reproducible",
})


class StageError(Exception):
    def __init__(self, code: int, message: str) -> None:
        super().__init__(message)
        self.code = code


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run(command: list[str], stage: str, code: int) -> subprocess.CompletedProcess[str]:
    try:
        completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                                   errors="replace", check=False)
    except OSError as error:
        raise StageError(code, f"{stage}: cannot start {command[0]}: {error}") from error
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "").strip().splitlines()
        tail = detail[-3:] if detail else ["no diagnostic output"]
        raise StageError(code, f"{stage} failed ({completed.returncode}): " + " | ".join(tail))
    return completed


def read_json(path: Path, stage: str, code: int) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise StageError(code, f"{stage}: cannot read {path.name}: {error}") from error


def parse_hex_address(text: str) -> str:
    return f"0x{int(text, 0) & 0xFFFFFFFFFFFFFFFF:016X}"


def normalized_manifest_digest(path: Path) -> str:
    """Digest the manifest with its output-directory path canonicalized.

    `recompile-iso` embeds the absolute path of the extracted boot ELF in
    `input.path`, so the raw manifest digest changes with the probe output
    directory name and cannot serve as an identity. The canonical form replaces
    that single field; the ELF's own digest is already bound by
    `title.boot_elf_sha256`, so nothing is lost. Any other absolute path fails
    closed rather than silently reintroducing the same defect later.
    """
    manifest = read_json(path, "manifest", 3)
    input_record = manifest.get("input")
    if isinstance(input_record, dict) and "path" in input_record:
        input_record["path"] = "<probe-output>/boot.elf"
    canonical = json.dumps(manifest, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    leaks = [where for where, value in leaves(manifest)
             if isinstance(value, str) and ABS_PATH.match(value)]
    if leaks:
        raise StageError(4, "manifest carries an absolute path that normalization does not "
                            "cover: " + ", ".join(leaks))
    return sha256_bytes(canonical.encode("utf-8"))


def executable_version(exe: str, flag: str, label: str) -> str:
    """Ask an executable for its own version, so a claim can be checked."""
    completed = run([exe, flag], f"{label} version query", 8)
    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    if not lines:
        raise StageError(8, f"{label} reported no version")
    return lines[0][:120]


def verify_provenance(args: argparse.Namespace, manifest: dict[str, Any],
                      report: dict[str, Any]) -> None:
    """Fail closed when a recorded version does not match the binary that ran."""
    claimed = manifest.get("tool", {}).get("version")
    reported = VERSION.search(args.frontend_reported_version)
    if not reported or reported.group(1) != claimed:
        raise StageError(8, "frontend reports "
                            f"{args.frontend_reported_version!r} but its manifest claims "
                            f"{claimed!r}")
    engine_reported = VERSION.search(args.engine_reported_version)
    engine_ran = report.get("engine", {}).get("version")
    if not engine_reported or engine_reported.group(1) != engine_ran:
        raise StageError(8, "engine runner reports "
                            f"{args.engine_reported_version!r} but its run report claims "
                            f"{engine_ran!r}")


def build_record(args: argparse.Namespace, out: Path, report: dict[str, Any]) -> dict[str, Any]:
    manifest = read_json(out / "boot.manifest.json", "manifest", 3)
    toolchain = read_json(out / "boot.native" / "build" / "reps2-native-toolchain-Release.json",
                          "toolchain report", 4)
    module_path_file = out / "boot.native" / "build" / "reps2-native-module-Release.path"
    module_path = Path(module_path_file.read_text(encoding="utf-8").strip().strip('"').strip())
    if not module_path.is_file():
        raise StageError(4, f"native module path is missing: {module_path_file.name}")

    session = report["session"]
    batches = report["runs"]
    last_run = batches[-1]
    outcome = report["outcome"]
    guest_status = last_run["guest_status"]
    stop_reason = "budget_exhausted" if outcome == "budget_limit_reached" else guest_status

    record: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "kind": KIND,
        "title": {
            "serial": args.serial,
            "iso_sha256": args.expect_iso_sha256,
            "boot_elf_sha256": sha256_file(out / "boot.elf"),
        },
        "source": {
            "reps2_commit": args.reps2_commit,
            "reps2_version": manifest["tool"]["version"],
            "frontend_reported_version": args.frontend_reported_version,
            "frontend_sha256": sha256_file(Path(args.reps2)),
            "engine_sha256": sha256_file(Path(args.engine)),
            "runtime_abi": toolchain["runtime"]["abi_version"],
            "toolchain_headers_sha256": toolchain["runtime"]["headers_sha256"],
            "manifest_sha256": sha256_file(out / "boot.manifest.json"),
            "manifest_normalized_sha256": normalized_manifest_digest(out / "boot.manifest.json"),
            "native_module_sha256": sha256_file(module_path),
            "ram_image_sha256": sha256_file(out / "boot.ram.bin"),
        },
        "host": {
            "os": str(toolchain["cmake"]["host_system"]).lower(),
            "architecture": str(toolchain["compiler"]["architecture_id"]).lower(),
            "compiler": toolchain["compiler"]["id"],
            "compiler_version": toolchain["compiler"]["version"],
            "cxx_standard": toolchain["compile"]["cxx_standard"],
        },
        "configuration": {
            "discovery": args.discover,
            "tu_count": args.tu_count,
            "hle_enabled": True,
            "instruction_budget": args.probe_budget,
            "max_batches": args.max_batches,
            "extra_roots": [parse_hex_address(root) for root in args.extra_root],
            "relocated_regions": [f"0x{int(dest, 16):08X}:0x{int(src, 16):08X}:0x{int(size, 16):X}"
                                  for dest, src, size in (part.split(":") for part in args.relocated_code)],
        },
        "result": {
            "outcome": outcome,
            "guest_status": guest_status,
            "guest_status_code": last_run["guest_status_code"],
            "stop_reason": stop_reason,
            "final_pc": parse_hex_address(session["pc_hex"]),
            "instructions_executed": session["instructions_executed"],
            "batches": len(batches),
            "syscalls_seen": session["syscalls_seen"],
            "syscalls_handled": session["syscalls_handled"],
            "callbacks_completed": session["callbacks"]["completed"],
            "rpc_call_number": session["sif"]["rpc_call_number"],
            "rpc_call_replied": session["sif"]["rpc_call_replied"],
        },
        "evidence": {
            "report_sha256": sha256_bytes((out / "run-report.json").read_bytes()),
            "manifest_census": {
                "basic_blocks": manifest["program"]["basic_block_count"],
                "guest_instructions": manifest["program"]["guest_instruction_count"],
                "unsupported_stops": manifest["program"]["unsupported_stop_count"],
                "guest_exceptions": manifest["program"]["guest_exception_count"],
                "unresolved_indirect_targets": manifest["program"]["unresolved_indirect_target_count"],
                "translation_units": manifest["translation"]["translation_unit_count"],
                "accepted_roots": manifest["analysis"]["discovery"]["accepted_root_count"],
            },
            "reproducible": False,
        },
    }
    verify_provenance(args, manifest, report)
    return record


def validate(record: Any) -> list[str]:
    errors: list[str] = []

    def require(mapping: Any, key: str, where: str, kind: type | tuple[type, ...]) -> None:
        if not isinstance(mapping, dict) or key not in mapping:
            errors.append(f"{where}.{key} is missing")
        elif not isinstance(mapping[key], kind) or (isinstance(mapping[key], bool) and kind is int):
            errors.append(f"{where}.{key} has the wrong type")

    if not isinstance(record, dict):
        return ["record is not an object"]

    require(record, "schema_version", "record", int)
    require(record, "kind", "record", str)
    if record.get("kind") != KIND:
        errors.append(f"record.kind must be {KIND}")
    if record.get("schema_version") != SCHEMA_VERSION:
        errors.append(f"record.schema_version must be {SCHEMA_VERSION}")

    for section in ("title", "source", "host", "configuration", "result", "evidence"):
        require(record, section, "record", dict)
    if errors:
        return errors

    title, source, host = record["title"], record["source"], record["host"]
    config, result, evidence = record["configuration"], record["result"], record["evidence"]

    require(title, "serial", "title", str)
    if isinstance(title.get("serial"), str) and not title["serial"]:
        errors.append("title.serial is empty")
    for key in ("iso_sha256", "boot_elf_sha256"):
        require(title, key, "title", str)
        if not SHA256.match(str(title.get(key, ""))):
            errors.append(f"title.{key} is not a lowercase SHA-256")

    require(source, "reps2_commit", "source", str)
    if not re.match(r"^[0-9a-f]{40}$", str(source.get("reps2_commit", ""))):
        errors.append("source.reps2_commit is not a 40-character commit id")
    require(source, "runtime_abi", "source", int)
    require(source, "reps2_version", "source", str)
    require(source, "frontend_reported_version", "source", str)
    for key in ("frontend_sha256", "engine_sha256", "toolchain_headers_sha256", "manifest_sha256",
                "manifest_normalized_sha256", "native_module_sha256", "ram_image_sha256"):
        require(source, key, "source", str)
        if not SHA256.match(str(source.get(key, ""))):
            errors.append(f"source.{key} is not a lowercase SHA-256")

    for key in ("os", "architecture", "compiler", "compiler_version"):
        require(host, key, "host", str)
    require(host, "cxx_standard", "host", int)

    require(config, "hle_enabled", "configuration", bool)
    require(config, "discovery", "configuration", bool)
    require(config, "tu_count", "configuration", int)
    require(config, "instruction_budget", "configuration", int)
    require(config, "max_batches", "configuration", int)
    require(config, "extra_roots", "configuration", list)
    require(config, "relocated_regions", "configuration", list)
    for root in config.get("extra_roots", []):
        if not HEX_U64.match(str(root)):
            errors.append(f"configuration.extra_roots entry {root!r} is not 0x + 16 hex digits")
    for region in config.get("relocated_regions", []):
        if not RELOCATED.match(str(region)):
            errors.append(f"configuration.relocated_regions entry {region!r} is not DEST:SOURCE:SIZE")

    require(result, "outcome", "result", str)
    if result.get("outcome") not in OUTCOMES:
        errors.append(f"result.outcome {result.get('outcome')!r} is not a known outcome")
    require(result, "guest_status", "result", str)
    if result.get("guest_status") not in GUEST_STATUSES:
        errors.append(f"result.guest_status {result.get('guest_status')!r} is not a known status")
    require(result, "stop_reason", "result", str)
    require(result, "final_pc", "result", str)
    if not HEX_U64.match(str(result.get("final_pc", ""))):
        errors.append("result.final_pc is not 0x + 16 hex digits")
    for key in ("guest_status_code", "instructions_executed", "batches", "syscalls_seen",
                "syscalls_handled", "callbacks_completed", "rpc_call_number", "rpc_call_replied"):
        require(result, key, "result", int)

    require(evidence, "report_sha256", "evidence", str)
    if not SHA256.match(str(evidence.get("report_sha256", ""))):
        errors.append("evidence.report_sha256 is not a lowercase SHA-256")
    require(evidence, "reproducible", "evidence", bool)
    require(evidence, "manifest_census", "evidence", dict)
    census = evidence.get("manifest_census", {})
    for key in ("basic_blocks", "guest_instructions", "unsupported_stops", "guest_exceptions",
                "unresolved_indirect_targets", "translation_units", "accepted_roots"):
        require(census, key, "evidence.manifest_census", int)

    if result.get("outcome") == "guest_stopped" and result.get("stop_reason") == "guest_completed":
        errors.append("result.stop_reason cannot be guest_completed for a guest_stopped outcome")
    if result.get("batches", 0) < 1:
        errors.append("result.batches must be at least 1")

    errors.extend(find_paths(record))
    return errors


def leaves(value: Any, where: str = "record") -> Iterator[tuple[str, Any]]:
    """Walk every scalar in the record, naming each one by its full path."""
    if isinstance(value, dict):
        for key, item in value.items():
            yield from leaves(item, f"{where}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from leaves(item, f"{where}[{index}]")
    else:
        yield where, value


def find_paths(value: Any, where: str = "record") -> list[str]:
    """Reject absolute host paths anywhere in the record."""
    return [f"{path} contains an absolute path"
            for path, item in leaves(value, where)
            if isinstance(item, str) and ABS_PATH.match(item)]


def semantic_diff(left: dict[str, Any], right: dict[str, Any]) -> list[str]:
    """Compare every field that must be identical for a reproducible probe.

    The comparison walks the whole record and subtracts VOLATILE_PATHS, so a field
    added later is compared by default instead of being silently ignored. A key
    present on one side only is a difference, which stops a missing value from
    looking like a matching `None`.

    Both records must be validated before they reach here: a field missing from
    *both* records is invisible to a pairwise walk, and `validate` is what rejects
    it. A pairwise comparison cannot substitute for schema validation.
    """
    left_leaves = dict(leaves(left))
    right_leaves = dict(leaves(right))
    diffs: list[str] = []
    for path in sorted(set(left_leaves) | set(right_leaves)):
        if path in VOLATILE_PATHS:
            continue
        if path not in left_leaves:
            diffs.append(f"{path}: absent on the left, {right_leaves[path]!r} on the right")
        elif path not in right_leaves:
            diffs.append(f"{path}: {left_leaves[path]!r} on the left, absent on the right")
        elif left_leaves[path] != right_leaves[path]:
            diffs.append(f"{path}: {left_leaves[path]!r} != {right_leaves[path]!r}")
    return diffs


def native_build_command(args: argparse.Namespace, out: Path) -> list[str]:
    command = ["cmake", "--build", str(out / "boot.native" / "build"), "--config", "Release"]
    if args.jobs is not None:
        if args.jobs < 1:
            raise StageError(2, "--jobs must be at least 1")
        command += ["--parallel", str(args.jobs)]
    return command


def probe(args: argparse.Namespace) -> int:
    iso = Path(args.iso)
    if not iso.is_file():
        raise StageError(2, f"ISO not found: {iso.name}")
    if iso.stat().st_size % 2048 != 0:
        raise StageError(2, "ISO size is not a multiple of 2048 bytes")
    if sha256_file(iso) != args.expect_iso_sha256:
        raise StageError(2, "ISO SHA-256 does not match --expect-iso-sha256")

    out = Path(args.out)
    if out.exists():
        raise StageError(2, f"output directory already exists: {out.name}")

    # Bind the supported commit claim to the executables that actually run.
    args.frontend_reported_version = executable_version(args.reps2, "version", "frontend")
    args.engine_reported_version = executable_version(args.engine, "--version", "engine runner")

    recompile = [args.reps2, "recompile-iso", str(iso), str(out),
                 "--probe-budget", str(args.probe_budget),
                 "--hle-syscalls"]
    if args.discover:
        recompile.append("--discover")
        recompile += ["--tu-count", str(args.tu_count)]
    for region in args.relocated_code:
        recompile += ["--relocated-code", region]
    for root in args.extra_root:
        recompile += ["--extra-root", root]
    run(recompile, "frontend recompile-iso", 3)

    configure = ["cmake", "-S", str(out / "boot.native"), "-B", str(out / "boot.native" / "build"),
                 "-DREPS2_RUNTIME_INCLUDE=" + args.runtime_include,
                 "-DREPS2_ENGINE_INCLUDE=" + args.engine_include]
    run(configure, "native configure", 4)
    run(native_build_command(args, out), "native build", 4)

    module_path_file = out / "boot.native" / "build" / "reps2-native-module-Release.path"
    if not module_path_file.is_file():
        raise StageError(4, "native build did not publish reps2-native-module-Release.path")
    module = module_path_file.read_text(encoding="utf-8").strip().strip('"').strip()

    engine_command = [args.engine, module,
                      "--ram", str(out / "boot.ram.bin"),
                      "--media", str(iso),
                      "--budget", str(args.probe_budget),
                      "--max-batches", str(args.max_batches),
                      "--hle-syscalls"]
    if args.syscall_trace > 0:
        engine_command += ["--syscall-trace", str(args.syscall_trace)]
    if args.instruction_trace:
        engine_command += ["--instruction-trace"]
    completed = run(engine_command, "engine run", 5)
    (out / "run-report.json").write_text(completed.stdout, encoding="utf-8", newline="\n")
    try:
        report = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise StageError(5, f"engine run produced unparsable JSON: {error}") from error
    if report.get("kind") != "rg.reps2.engine.run_report.v1":
        raise StageError(5, f"unexpected run report kind {report.get('kind')!r}")

    record = build_record(args, out, report)
    errors = validate(record)
    if errors:
        for line in errors:
            print("schema: " + line, file=sys.stderr)
        raise StageError(6, f"{len(errors)} schema violation(s)")

    if args.compare_with:
        previous = read_json(Path(args.compare_with), "previous frontier record", 6)
        previous_errors = validate(previous)
        if previous_errors:
            for line in previous_errors:
                print("previous record: " + line, file=sys.stderr)
            raise StageError(6, f"{len(previous_errors)} schema violation(s) in the previous record")
        diffs = semantic_diff(previous, record)
        record["evidence"]["reproducible"] = not diffs
        if diffs:
            print("semantic difference against " + Path(args.compare_with).name, file=sys.stderr)
            for line in diffs:
                print("  " + line, file=sys.stderr)

    (out / "frontier.json").write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(record["result"], sort_keys=True))
    if args.compare_with and not record["evidence"]["reproducible"]:
        return 7
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    subparsers = parser.add_subparsers(dest="command", required=True)

    probe_parser = subparsers.add_parser("probe", help="recompile, build, run and write frontier.json")
    probe_parser.add_argument("--iso", required=True)
    probe_parser.add_argument("--serial", required=True, help="disc serial, for example SCUS_971.77")
    probe_parser.add_argument("--expect-iso-sha256", required=True)
    probe_parser.add_argument("--out", required=True, help="new output directory")
    probe_parser.add_argument("--reps2", required=True, help="reps2 frontend executable")
    probe_parser.add_argument("--engine", required=True, help="rg-reps2-engine executable")
    probe_parser.add_argument("--runtime-include", required=True)
    probe_parser.add_argument("--engine-include", required=True)
    probe_parser.add_argument("--reps2-commit", required=True)
    probe_parser.add_argument("--discover", action="store_true")
    probe_parser.add_argument("--tu-count", type=int, default=8)
    probe_parser.add_argument("--probe-budget", type=int, default=5000000)
    probe_parser.add_argument("--max-batches", type=int, default=16)
    probe_parser.add_argument("--jobs", type=int, default=None,
                              help="parallel native build jobs; default is the build tool default")
    probe_parser.add_argument("--relocated-code", action="append", default=[],
                              help="DEST:SOURCE:SIZE in hex, repeatable")
    probe_parser.add_argument("--extra-root", action="append", default=[],
                              help="guest PC in hex, repeatable")
    probe_parser.add_argument("--compare-with", default=None,
                              help="previous frontier.json; sets evidence.reproducible")
    probe_parser.add_argument("--syscall-trace", type=int, default=0,
                              help="retain the last n syscall boundaries in the run report (0..64)")
    probe_parser.add_argument("--instruction-trace", action="store_true",
                              help="include the bounded EE instruction-PC trace in the run report")
    probe_parser.set_defaults(handler=probe)

    validate_parser = subparsers.add_parser("validate", help="validate a frontier record")
    validate_parser.add_argument("record")
    validate_parser.set_defaults(
        handler=lambda args: finish_validation(read_json(Path(args.record), "frontier record", 6)))

    compare_parser = subparsers.add_parser("compare", help="compare two frontier records")
    compare_parser.add_argument("left")
    compare_parser.add_argument("right")
    compare_parser.set_defaults(handler=lambda args: finish_compare(
        read_json(Path(args.left), "frontier record", 6),
        read_json(Path(args.right), "frontier record", 6)))

    args = parser.parse_args(argv)
    if args.command == "probe":
        for region in args.relocated_code:
            if not re.match(r"^[0-9A-Fa-f]+:[0-9A-Fa-f]+:[0-9A-Fa-f]+$", region):
                print(f"--relocated-code expects DEST:SOURCE:SIZE hex, got {region!r}", file=sys.stderr)
                return 2
    try:
        return args.handler(args)
    except StageError as error:
        print(f"error: {error}", file=sys.stderr)
        return error.code


def finish_validation(record: Any) -> int:
    errors = validate(record)
    for line in errors:
        print("schema: " + line, file=sys.stderr)
    return 0 if not errors else 6


def finish_compare(left: Any, right: Any) -> int:
    errors = [f"left: {line}" for line in validate(left)]
    errors += [f"right: {line}" for line in validate(right)]
    if errors:
        for line in errors:
            print("schema: " + line, file=sys.stderr)
        return 6
    diffs = semantic_diff(left, right)
    for line in diffs:
        print(line)
    return 0 if not diffs else 7


if __name__ == "__main__":
    sys.exit(main())