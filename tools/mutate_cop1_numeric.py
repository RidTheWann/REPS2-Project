#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Mutation harness for the COP1 numeric core, the cause-flag model and CTC1.

Each mutation is applied to the header sources, the affected CTest targets are
rebuilt and run, and the original bytes are restored afterwards. The result
that matters is the test exit code, not a grep of the file: a mutant that still
passes means the evidence chain does not actually constrain that rule.

Run from anywhere after a normal CMake configure:

    python tools/mutate_cop1_numeric.py [--build-dir build]

Exit status is 0 only when every mutant was killed and the restored tree was
rebuilt and passed the full suite.
"""

import argparse
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(REPO, "runtime", "include", "reps2", "runtime", "ee_fpu.hpp")
STATE = os.path.join(REPO, "runtime", "include", "reps2", "runtime", "ee_state.hpp")

MUTATIONS = [
    ("overflow threshold off by one", HEADER,
     "    if (biased > 255) {", "    if (biased >= 255) {"),
    ("wide-spread difference keeps the larger operand", HEADER,
     "        } else if (larger.mantissa > kFpuHiddenBit) {\n"
     "            magnitude = larger.mantissa - 1ULL;",
     "        } else if (larger.mantissa >= kFpuHiddenBit) {\n"
     "            magnitude = larger.mantissa;"),
    ("renormalized difference loses one mantissa step", HEADER,
     "            magnitude = 0x00FF'FFFFULL;", "            magnitude = 0x0100'0000ULL;"),
    ("Table 8-5 zero-difference sign keeps the minuend alone", HEADER,
     "signed_zero(minuend.sign & ~subtrahend.sign)", "signed_zero(minuend.sign)"),
    ("Table 8-5 zero-difference sign follows the subtrahend", HEADER,
     "signed_zero(minuend.sign & ~subtrahend.sign)",
     "signed_zero(decode(b).sign)"),
    ("sqrt guard one bit short", HEADER,
     "    const auto guard = 23U - (bit_length_of(radicand) - 1U) / 2U;",
     "    const auto guard = 22U - (bit_length_of(radicand) - 1U) / 2U;"),
    ("divide exponent not corrected for the scaling", HEADER,
     "lhs.exponent - rhs.exponent - 24", "lhs.exponent - rhs.exponent - 23"),
    ("sticky flags shift by twelve instead of eleven", HEADER,
     "(result.set_flags >> 11U)", "(result.set_flags >> 12U)"),
    ("unnamed cause flags cleared by every operation", HEADER,
     "    const auto causes = ((control & kFpuCauseMask) & ~result.clear_flags) | result.set_flags;",
     "    const auto causes = (control & kFpuCauseMask & ~kFpuCauseMask) | result.set_flags;"),
    ("cvt.w.s clamp threshold one exponent too high", HEADER,
     "    if (biased > 0x9DU) {", "    if (biased > 0x9EU) {"),
    ("NEG.S stops clearing O and U", HEADER,
     "    return {a ^ 0x8000'0000U, 0U, kFpuCauseOverflow | kFpuCauseUnderflow};",
     "    return {a ^ 0x8000'0000U, 0U, 0U};"),
    ("ABS.S stops clearing O and U", HEADER,
     "    return {a & 0x7FFF'FFFFU, 0U, kFpuCauseOverflow | kFpuCauseUnderflow};",
     "    return {a & 0x7FFF'FFFFU, 0U, 0U};"),
    ("SQRT.S stops clearing D", HEADER,
     "            set,\n            kFpuCauseDivideByZero};",
     "            set,\n            0U};"),
    ("MAX.S stops clearing O and U", HEADER,
     "    return {compare(a, b) >= 0 ? a : b, 0U, kFpuCauseOverflow | kFpuCauseUnderflow};",
     "    return {compare(a, b) >= 0 ? a : b, 0U, 0U};"),
    ("MIN.S stops clearing O and U", HEADER,
     "    return {compare(a, b) <= 0 ? a : b, 0U, kFpuCauseOverflow | kFpuCauseUnderflow};",
     "    return {compare(a, b) <= 0 ? a : b, 0U, 0U};"),
    ("MAX.S zero pair takes the MIN sign rule", HEADER,
     "signed_zero(decode(a).sign & decode(b).sign)",
     "signed_zero(decode(a).sign | decode(b).sign)"),
    ("MIN.S zero pair takes the MAX sign rule", HEADER,
     "signed_zero(decode(a).sign | decode(b).sign)",
     "signed_zero(decode(a).sign & decode(b).sign)"),
    ("zero dividend raises underflow", HEADER,
     "        // zero operand the same way.\n        return {signed_zero(sign), 0U, 0U};",
     "        // zero operand the same way.\n"
     "        return {signed_zero(sign), kFpuCauseUnderflow, 0U};"),
    ("RSQRT ignores the zero divisor's sign", HEADER,
     "        const auto sign = ((a >> 31U) & 1U) ^ divisor.sign;",
     "        const auto sign = ((a >> 31U) & 1U);"),
    ("CTC1 keeps reserved-zero fields", STATE,
     "    control = (value & kFpuControlWritableMask) | kFpuControlResetValue;",
     "    control = value | 0x0000'0001U;"),
]

TARGETS = ["reps2_cop1_arith_smoke", "reps2_cop1_codegen_smoke"]


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


def build_and_run(build_dir):
    for target in TARGETS:
        rc = subprocess.call(
            ["cmake", "--build", build_dir, "--config", "Release", "--target", target],
            cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if rc != 0:
            return "build-failed(%d)" % rc
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

    originals = {}
    for path in {HEADER, STATE}:
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
            outcome = build_and_run(build_dir)
            results.append((name, outcome))
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
    print("%d mutants, %d survived (a survivor passed the tests with the mutation in place)"
          % (len(results), survivors))

    # Restore is not enough: rebuild and run everything so the reported green
    # tree is the one that actually runs.
    if not full_suite(build_dir):
        print("restored tree is not green")
        return 1
    print("restored tree rebuilt and green")
    return 1 if survivors else 0


if __name__ == "__main__":
    sys.exit(main())
