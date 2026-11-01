#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Mutation harness for the COP1 wiring (arithmetic, compare and BC1).

Every mutation is applied to a Rust source, the workspace tests are run, and,
only when those stay green, the native COP1 targets are rebuilt and run.
That ordering means a mutant is killed by whichever layer notices it, and the
native run only spends build time when the Rust-level assertions were blind to
the change. All files are restored from memory afterwards, and the restored
tree is rebuilt and re-tested so a stale binary cannot mask a real failure.

Run from anywhere after a normal CMake configure:

    python tools/mutate_cop1_wiring.py [--build-dir build]

Exit status is 0 only when every mutant was killed and the restored tree was
rebuilt and passed the full suite.
"""

import argparse
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MIPS = os.path.join(REPO, "rust", "frontend", "src", "mips.rs")
IR = os.path.join(REPO, "rust", "frontend", "src", "ir.rs")
CODEGEN = os.path.join(REPO, "rust", "frontend", "src", "codegen.rs")
ANALYSIS = os.path.join(REPO, "rust", "frontend", "src", "analysis.rs")
CONTROL_CODEGEN = os.path.join(REPO, "rust", "frontend", "src", "control_codegen.rs")
HEADER = os.path.join(REPO, "runtime", "include", "reps2", "runtime", "ee_fpu.hpp")

MUTATIONS = [
    ("decoder swaps fs and ft", MIPS,
     "    let ft = ((word >> 16) & 0x1F) as u8;\n"
     "    let fs = ((word >> 11) & 0x1F) as u8;",
     "    let fs = ((word >> 16) & 0x1F) as u8;\n"
     "    let ft = ((word >> 11) & 0x1F) as u8;"),
    ("SQRT.S stops reserving fs", MIPS,
     "0x04 if fs == 0 =>", "0x04 =>"),
    ("ADDA.S stops reserving fd", MIPS,
     "0x18 if fd == 0 =>", "0x18 =>"),
    ("CVT.W.S accepts a nonzero ft", MIPS,
     "0x24 if ft == 0 =>", "0x24 =>"),
    ("C.EQ.S accepts a nonzero fd", MIPS,
     "0x32 if fd == 0 =>", "0x32 =>"),
    ("BC1T decodes as BC1F", MIPS,
     "            1 => Opcode::Bc1t,\n            2 => Opcode::Bc1fl,",
     "            1 => Opcode::Bc1f,\n            2 => Opcode::Bc1fl,"),
    ("BC1 drops the word-offset scale", MIPS,
     "        Opcode::Bc1f | Opcode::Bc1t | Opcode::Bc1fl | Opcode::Bc1tl\n"
     "    ) {\n"
     "        let offset = ((immediate as i16 as i32).wrapping_mul(4)) as u32;",
     "        Opcode::Bc1f | Opcode::Bc1t | Opcode::Bc1fl | Opcode::Bc1tl\n"
     "    ) {\n"
     "        let offset = (immediate as i16 as i32) as u32;"),
    ("BC1FL stops annulling", MIPS,
     "            Opcode::Beql\n"
     "                | Opcode::Bnel\n"
     "                | Opcode::Blezl\n"
     "                | Opcode::Bgtzl\n"
     "                | Opcode::Bltzl\n"
     "                | Opcode::Bgezl\n"
     "                | Opcode::Bltzall\n"
     "                | Opcode::Bgezall\n"
     "                | Opcode::Bc1fl\n"
     "                | Opcode::Bc1tl",
     "            Opcode::Beql\n"
     "                | Opcode::Bnel\n"
     "                | Opcode::Blezl\n"
     "                | Opcode::Bgtzl\n"
     "                | Opcode::Bltzl\n"
     "                | Opcode::Bgezl\n"
     "                | Opcode::Bltzall\n"
     "                | Opcode::Bgezall\n"
     "                | Opcode::Bc1tl"),
    ("SUB.S swaps its operands in the IR", IR,
     "            Opcode::FpuSubtractSingle => Operation::FpuSubtractSingle {\n"
     "                dst: fpr(instruction.shift_amount),\n"
     "                lhs: fpr(instruction.rd),\n"
     "                rhs: fpr(instruction.rt),\n"
     "            },",
     "            Opcode::FpuSubtractSingle => Operation::FpuSubtractSingle {\n"
     "                dst: fpr(instruction.shift_amount),\n"
     "                lhs: fpr(instruction.rt),\n"
     "                rhs: fpr(instruction.rd),\n"
     "            },"),
    ("SQRT.S reads fs instead of ft", IR,
     "            Opcode::FpuSquareRootSingle => Operation::FpuSquareRootSingle {\n"
     "                dst: fpr(instruction.shift_amount),\n"
     "                src: fpr(instruction.rt),\n"
     "            },",
     "            Opcode::FpuSquareRootSingle => Operation::FpuSquareRootSingle {\n"
     "                dst: fpr(instruction.shift_amount),\n"
     "                src: fpr(instruction.rd),\n"
     "            },"),
    ("MADDA.S writes an FPR instead of ACC", IR,
     "            Opcode::FpuMultiplyAddAccumulateSingle => Operation::FpuMultiplyAddAccumulateSingle {\n"
     "                lhs: fpr(instruction.rd),\n"
     "                rhs: fpr(instruction.rt),\n"
     "            },",
     "            Opcode::FpuMultiplyAddAccumulateSingle => Operation::FpuMultiplyAddSingle {\n"
     "                dst: fpr(instruction.shift_amount),\n"
     "                lhs: fpr(instruction.rd),\n"
     "                rhs: fpr(instruction.rt),\n"
     "            },"),
    ("C.EQ.S swaps its operands in the IR", IR,
     "            Opcode::FpuCompareEqual => Operation::FpuCompareSingle {\n"
     "                lhs: fpr(instruction.rd),\n"
     "                rhs: fpr(instruction.rt),\n"
     "                comparison: FpuComparison::Equal,\n"
     "            },",
     "            Opcode::FpuCompareEqual => Operation::FpuCompareSingle {\n"
     "                lhs: fpr(instruction.rt),\n"
     "                rhs: fpr(instruction.rd),\n"
     "                comparison: FpuComparison::Equal,\n"
     "            },"),
    ("C.LE.S lowers as C.LT.S", IR,
     "                comparison: FpuComparison::LessThanOrEqual,",
     "                comparison: FpuComparison::LessThan,"),
    ("codegen stops routing MADD through ACC", CODEGEN,
     "emit_fpu_madd(output, index, Some(dst), lhs, rhs, \"fpu_madd\");",
     "emit_fpu_madd(output, index, Some(dst), lhs, rhs, \"fpu_msub\");"),
    ("codegen stores the raw result into FCR31", CODEGEN,
     "state.fcsr = apply_fpu_flags(state.fcsr, fpu_result_{tag});",
     "state.fcsr = fpu_result_{tag}.bits;"),
    ("codegen lowers C.LE.S as C.LT.S", CODEGEN,
     "                FpuComparison::LessThan => \"FpuCompare::LessThan\",\n"
     "                FpuComparison::LessThanOrEqual => \"FpuCompare::LessThanOrEqual\",",
     "                FpuComparison::LessThan => \"FpuCompare::LessThan\",\n"
     "                FpuComparison::LessThanOrEqual => \"FpuCompare::LessThan\","),
    ("BC1T gets BC1F's predicate", ANALYSIS,
     "        Opcode::Bc1f | Opcode::Bc1fl => Some(BranchCondition::FpuCondition { when_set: false }),\n"
     "        Opcode::Bc1t | Opcode::Bc1tl => Some(BranchCondition::FpuCondition { when_set: true }),",
     "        Opcode::Bc1f | Opcode::Bc1fl => Some(BranchCondition::FpuCondition { when_set: false }),\n"
     "        Opcode::Bc1t | Opcode::Bc1tl => Some(BranchCondition::FpuCondition { when_set: false }),"),
    ("control codegen inverts the condition test", CONTROL_CODEGEN,
     "            let relation = if when_set { \"!=\" } else { \"==\" };",
     "            let relation = if when_set { \"==\" } else { \"!=\" };"),
    # Rust never evaluates the runtime, so these have to be caught natively.
    ("runtime MOV.S flips the sign", HEADER,
     "FpuResult fpu_mov(std::uint32_t a) noexcept {\n    return {a, 0U, 0U};",
     "FpuResult fpu_mov(std::uint32_t a) noexcept {\n    return {a ^ 0x8000'0000U, 0U, 0U};"),
    ("runtime C.LT.S accepts equality", HEADER,
     "        return fpu_detail::compare(a, b) < 0;",
     "        return fpu_detail::compare(a, b) <= 0;"),
    ("runtime condition update cannot clear C", HEADER,
     "    return (control & ~kFpuCondition) | (condition ? kFpuCondition : 0U);",
     "    return control | (condition ? kFpuCondition : 0U);"),
]

TARGETS = [
    "reps2_cop1_arith_codegen_smoke",
    "reps2_cop1_arith_smoke",
    "reps2_cop1_compare_smoke",
    "reps2_cfg_codegen_smoke",
]


def store(path, text):
    """Replace a source file atomically, retrying transient Windows locks.

    A compiler or an indexer can hold the file just long enough for an in-place
    truncate to fail, and a failed open on a truncating write would leave the
    mutation harness unable to restore what it changed.
    """
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


def executable(build_dir, target):
    exe = target + (".exe" if os.name == "nt" else "")
    for candidate in (os.path.join(build_dir, "Release", exe),
                      os.path.join(build_dir, exe)):
        if os.path.isfile(candidate):
            return candidate
    return None


def rust_tests():
    return subprocess.call(
        ["cargo", "test", "--workspace", "--locked", "--quiet"],
        cwd=os.path.join(REPO, "rust"),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0


def native_tests(build_dir):
    for target in TARGETS:
        rc = subprocess.call(
            ["cmake", "--build", build_dir, "--config", "Release", "--target", target],
            cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if rc != 0:
            return "native-build-failed(%d)" % rc
    failures = []
    for target in TARGETS:
        path = executable(build_dir, target)
        if path is None:
            return "missing-binary(%s)" % target
        rc = subprocess.call([path], cwd=REPO,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if rc != 0:
            failures.append("%s=%d" % (target, rc))
    return ",".join(failures) if failures else "PASSED-SOFTLY"


def full_suite(build_dir):
    rc = subprocess.call(["cmake", "--build", build_dir, "--config", "Release"],
                         cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if rc != 0:
        return False
    return subprocess.call(
        ["ctest", "--test-dir", build_dir, "-C", "Release", "--output-on-failure"],
        cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", default=os.path.join(REPO, "build"))
    args = parser.parse_args()
    build_dir = os.path.abspath(args.build_dir)

    paths = {MIPS, IR, CODEGEN, ANALYSIS, CONTROL_CODEGEN, HEADER}
    originals = {}
    for path in paths:
        with open(path, "r", encoding="utf-8", newline="") as handle:
            originals[path] = handle.read()

    results = []
    try:
        for name, path, old, new in MUTATIONS:
            text = originals[path]
            count = text.count(old)
            if count != 1:
                results.append((name, "ANCHOR-NOT-UNIQUE(%d)" % count))
                continue
            print("running %s" % name, flush=True)
            store(path, text.replace(old, new))
            os.utime(path, (time.time(), time.time()))
            if not rust_tests():
                results.append((name, "killed-by-rust"))
            else:
                results.append((name, native_tests(build_dir)))
            store(path, text)
            os.utime(path, (time.time(), time.time()))
    finally:
        for path, text in originals.items():
            store(path, text)
            os.utime(path, (time.time(), time.time()))

    survivors = 0
    for name, outcome in results:
        if outcome.startswith("ANCHOR"):
            print("%-12s %-52s %s" % ("BAD-ANCHOR", name, outcome))
            continue
        killed = outcome != "PASSED-SOFTLY" and "build-failed" not in outcome
        if not killed:
            survivors += 1
        print("%-12s %-52s %s" % ("KILLED" if killed else "SURVIVED", name, outcome))
    print("%d mutants, %d survived" % (len(results), survivors))

    # Restore is not enough: rebuild and run everything so the reported green
    # tree is the one that actually runs.
    if not rust_tests():
        print("restored Rust tree is not green")
        return 1
    if not full_suite(build_dir):
        print("restored tree is not green")
        return 1
    print("restored tree rebuilt and green")
    return 1 if survivors else 0


if __name__ == "__main__":
    sys.exit(main())
