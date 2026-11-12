#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Mutation harness for the R5900 branch/control-transfer decoder and CFG.

Each mutation is applied to the Rust decoder/analysis source, the frontend
test suite is rebuilt and run, and original source is restored afterwards.
A mutant is KILLED only when tests execute and fail. Compile errors, bad
anchors, timeouts and infrastructure errors are NOT kills. Every non-KILLED
outcome counts against the harness. The unmutated frontend suite must pass
first so a pre-existing failure cannot fake kills.

Run: python tools/mutate_branch_analysis.py
Exit status is 0 only when all mutants are killed and the restored workspace
suite passes.
"""

import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "rust", "Cargo.toml")
ANALYSIS = os.path.join(REPO, "rust", "frontend", "src", "analysis.rs")
MIPS = os.path.join(REPO, "rust", "frontend", "src", "mips.rs")

MUTATIONS = [
    ("branch-likely always executes delay slot", ANALYSIS,
     "                delay_slot_policy: if instruction.is_branch_likely() {\n"
     "                    DelaySlotPolicy::AnnulWhenNotTaken",
     "                delay_slot_policy: if instruction.is_branch_likely() {\n"
     "                    DelaySlotPolicy::ExecuteAlways"),
    ("fallthrough PC is PC+4 instead of PC+8", ANALYSIS,
     "            let fallthrough_pc = instruction\n"
     "                .pc\n"
     "                .checked_add(8)",
     "            let fallthrough_pc = instruction\n"
     "                .pc\n"
     "                .checked_add(4)"),
    ("JAL return PC is PC+4 instead of PC+8", ANALYSIS,
     "                let return_pc = if instruction.opcode == Opcode::Jal {\n"
     "                    Some(\n"
     "                        instruction\n"
     "                            .pc\n"
     "                            .checked_add(8)",
     "                let return_pc = if instruction.opcode == Opcode::Jal {\n"
     "                    Some(\n"
     "                        instruction\n"
     "                            .pc\n"
     "                            .checked_add(4)"),
    ("B*AL link register not written", ANALYSIS,
     "                link_register: matches!(\n"
     "                    instruction.opcode,\n"
     "                    Opcode::Bltzal | Opcode::Bgezal | Opcode::Bltzall | Opcode::Bgezall\n"
     "                )\n"
     "                .then_some(31),",
     "                link_register: None,"),
    ("BC1F branch condition inverted", ANALYSIS,
     "Opcode::Bc1f | Opcode::Bc1fl => Some(BranchCondition::FpuCondition { when_set: false }),",
     "Opcode::Bc1f | Opcode::Bc1fl => Some(BranchCondition::FpuCondition { when_set: true }),"),
    ("BC1T branch condition inverted", ANALYSIS,
     "Opcode::Bc1t | Opcode::Bc1tl => Some(BranchCondition::FpuCondition { when_set: true }),",
     "Opcode::Bc1t | Opcode::Bc1tl => Some(BranchCondition::FpuCondition { when_set: false }),"),
    ("BLEZ condition becomes BGTZ", ANALYSIS,
     "Opcode::Blez | Opcode::Blezl => Some(BranchCondition::LessThanOrEqualZero {",
     "Opcode::Blez | Opcode::Blezl => Some(BranchCondition::GreaterThanZero {"),
    ("BEQ condition becomes BNE", ANALYSIS,
     "Opcode::Beq | Opcode::Beql => Some(BranchCondition::Equal {",
     "Opcode::Beq | Opcode::Beql => Some(BranchCondition::NotEqual {"),
    ("JR $ra treated as IndirectJump", ANALYSIS,
     "Opcode::Jr if instruction.rs == 31 => {",
     "Opcode::Jr if false => {"),
    ("JALR link register is rs not rd", ANALYSIS,
     "                let link_register = if instruction.opcode == Opcode::Jalr && instruction.rd != 0 {\n"
     "                    Some(instruction.rd)",
     "                let link_register = if instruction.opcode == Opcode::Jalr && instruction.rd != 0 {\n"
     "                    Some(instruction.rs)"),
    ("delay slot fetched from PC+8", ANALYSIS,
     "    let delay_pc = control\n"
     "        .pc\n"
     "        .checked_add(4)",
     "    let delay_pc = control\n"
     "        .pc\n"
     "        .checked_add(8)"),
    ("REGIMM branch offset scaled by 1", MIPS,
     "                    instruction.target = pc\n"
     "                        .wrapping_add(4)\n"
     "                        .wrapping_add(((immediate as i16 as i32).wrapping_mul(4)) as u32);",
     "                    instruction.target = pc\n"
     "                        .wrapping_add(4)\n"
     "                        .wrapping_add(((immediate as i16 as i32).wrapping_mul(1)) as u32);"),
    ("branch immediate not sign-extended", MIPS,
     "            0x04..=0x07 => {\n"
     "                let offset = ((immediate as i16 as i32).wrapping_mul(4)) as u32;",
     "            0x04..=0x07 => {\n"
     "                let offset = (immediate as u32).wrapping_mul(4);"),
    # The high-3-bit mutant is caught by the unit test and KSEG jump vectors.
    ("jump target region masked to 3 bits", MIPS,
     "    let region = (delay_pc >> 28) as u32;",
     "    let region = ((delay_pc >> 28) & 0x7) as u32;"),
]


def store(path, text):
    tmp = path + ".mutate.tmp"
    for attempt in range(20):
        try:
            with open(tmp, "w", encoding="utf-8", newline="") as handle:
                handle.write(text)
            os.replace(tmp, path)
            return
        except OSError:
            if attempt == 19:
                raise
            time.sleep(0.25)


def cargo_test(*extra):
    try:
        result = subprocess.run(
            ["cargo", "test", "--manifest-path", MANIFEST, *extra],
            cwd=REPO, capture_output=True, text=True, timeout=900,
        )
    except subprocess.TimeoutExpired:
        return "TIMEOUT"
    output = result.stdout + result.stderr
    if result.returncode == 0:
        return "PASSED-SOFTLY"
    if "test result: FAILED" in output or "error: test failed" in output:
        # Record which tests caught the mutant so the evidence is auditable.
        names = re.findall(r"^test (\S+) \.\.\. FAILED$", output, re.MULTILINE)
        shown = ",".join(dict.fromkeys(names))
        return "KILLED[%s]" % shown if shown else "KILLED"
    if "could not compile" in output or "error[E" in output:
        return "COMPILE-ERROR"
    return "ERROR(rc=%d)" % result.returncode


def main():
    originals = {}
    for path in (ANALYSIS, MIPS):
        with open(path, "r", encoding="utf-8", newline="") as handle:
            originals[path] = handle.read()

    bad_anchors = [(name, originals[path].count(old))
                   for name, path, old, _new in MUTATIONS
                   if originals[path].count(old) != 1]
    if bad_anchors:
        for name, count in bad_anchors:
            print("BAD-ANCHOR %-52s matches=%d" % (name, count))
        print("%d/%d anchors are not unique; nothing was run"
              % (len(bad_anchors), len(MUTATIONS)))
        return 1

    print("baseline: cargo test -p reps2-frontend --locked", flush=True)
    baseline = cargo_test("-p", "reps2-frontend", "--locked")
    if baseline != "PASSED-SOFTLY":
        print("baseline is not green (%s); refusing to run mutants" % baseline)
        return 1
    print("baseline green", flush=True)

    results = []
    try:
        for name, path, old, new in MUTATIONS:
            print("running %s" % name, flush=True)
            store(path, originals[path].replace(old, new))
            outcome = cargo_test("-p", "reps2-frontend", "--locked")
            results.append((name, outcome))
            store(path, originals[path])
            print("  %s" % outcome, flush=True)
    finally:
        for path, text in originals.items():
            store(path, text)

    failures = 0
    for name, outcome in results:
        killed = outcome.startswith("KILLED")
        failures += not killed
        print("%-12s %-52s %s" %
              ("KILLED" if killed else "NOT-KILLED", name, outcome))
    print("\n%d mutants, %d killed, %d not killed" %
          (len(results), len(results) - failures, failures))

    print("restored tree: cargo test --workspace --locked", flush=True)
    restored = cargo_test("--workspace", "--locked")
    if restored != "PASSED-SOFTLY":
        print("restored tree is not green (%s)" % restored)
        return 1
    print("restored tree rebuilt and green")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())