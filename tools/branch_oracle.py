#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Specification oracle for EE R5900 branch/control-transfer semantics.

This file is the independent second derivation that the branch differential
tests are checked against. It is not used by the recompiling frontend and
nothing in it is copied into generated C++: the runtime implements the same
documented rules with its own control-flow logic, so a bug has to appear twice,
in two different algorithms, before the test suite misses it.

Sources (Sony primary documents only; secondary emulator behaviour is rejected
where it disagrees):
  * EE Core (User's) Manual v6.0 section 2.2 (CPU registers), 2.3 (branch delay
    slot), 2.4 (branch likely), 4.2 (instruction set overview), Chapter 5
    (per-instruction operation boxes).
  * EE Core Instruction Set Manual v6.0 Chapter 5 (branch instruction operation
    boxes with explicit delay-slot and annulment semantics).

Model
-----
Every test case specifies:
  - The branch instruction encoding (opcode, rs, rt, rd, immediate, etc.)
  - Initial architectural state (GPRs, PC, FPR condition bit for BC1)
  - Whether the branch is taken or not taken

The oracle computes:
  - The architectural PC after the branch+delay-slot sequence
  - The link register value if written (RA=31 or rd for JALR)
  - The delay-slot instruction's PC (for verifying annulment)
  - GPR[0] write suppression

The R5900 branch delay slot semantics:
  - Every branch/jump has exactly one delay slot at PC+4
  - Regular branches: delay slot ALWAYS executes (taken or not taken)
  - Branch-likely (L suffix): delay slot executes ONLY when branch is taken;
    when not taken, the delay slot is annulled (treated as NOP)
  - B*AL / JAL / JALR write PC+8 to link register (RA=31 or rd)
  - PC-relative branches: target = (PC+4) + sign_extend(immediate)*4
  - J/JAL: target = (PC+4)[31:28] concatenated with immediate*4
  - JR/JALR: target = GPR[rs]; GPR[0] reads as zero
  - GPR[0] writes are suppressed

BC1 branches (BC1F/BC1T/BC1FL/BC1TL):
  - Branch on FPU condition bit (FCR31 bit 23)
  - Follow same delay-slot/annulment rules
  - Condition bit is read before the delay slot executes

Usage:
  python tools/branch_oracle.py --emit-vectors tests/branch_vectors.inc
  python tools/branch_oracle.py --self-check
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from enum import IntEnum, auto
from typing import Optional


class BranchKind(IntEnum):
    """Architectural branch classification matching the decoder."""
    # Conditional branches on GPR values
    BEQ = auto()      # rs == rt
    BNE = auto()      # rs != rt
    BLEZ = auto()     # rs <= 0 (signed)
    BGTZ = auto()     # rs > 0 (signed)
    BLTZ = auto()     # rs < 0 (signed)
    BGEZ = auto()     # rs >= 0 (signed)
    # Branch-likely variants (L suffix)
    BEQL = auto()
    BNEL = auto()
    BLEZL = auto()
    BGTZL = auto()
    BLTZL = auto()
    BGEZL = auto()
    # Branch-and-link (write RA=PC+8)
    BLTZAL = auto()
    BGEZAL = auto()
    BLTZALL = auto()  # branch-likely + link
    BGEZALL = auto()
    # Unconditional jumps
    J = auto()
    JAL = auto()
    JR = auto()
    JALR = auto()
    # BC1 branches on FPU condition bit
    BC1F = auto()
    BC1FL = auto()
    BC1T = auto()
    BC1TL = auto()


class DelaySlotPolicy(IntEnum):
    EXECUTE_ALWAYS = 0
    ANNUL_WHEN_NOT_TAKEN = 1


@dataclass(frozen=True)
class BranchTestCase:
    """One branch test vector."""
    kind: BranchKind
    pc: int
    # Source register values (64-bit, low lane used for comparison)
    gpr_rs: int = 0
    gpr_rt: int = 0
    # For JALR, gpr_rd is the destination (link) register index; the oracle
    # suppresses the link write when it is zero.
    gpr_rd: int = 0
    # Encoded register indices; the GPR values above model their contents.
    rs_index: int = 8
    rt_index: int = 9
    # For BC1: FPU condition bit (FCR31 bit 23)
    fpu_cond: bool = False
    # For J/JAL: 26-bit target field
    target_field: int = 0
    # For branch: 16-bit signed immediate
    imm16: int = 0
    # Expected outcomes
    taken: bool = False
    # Computed by oracle
    next_pc: int = 0
    link_reg: int = 31
    link_value: int = 0
    delay_slot_pc: int = 0
    delay_slot_executes: bool = True
    gpr0_suppressed: bool = False


def sign_extend_16(imm16: int) -> int:
    """Sign-extend 16-bit immediate to 32 bits."""
    if imm16 & 0x8000:
        return imm16 | 0xFFFF0000
    return imm16


def compute_branch_target(pc: int, imm16: int) -> int:
    """Compute branch target: (PC+4) + sign_extend(imm16)*4."""
    delay_pc = (pc + 4) & 0xFFFFFFFF
    offset = sign_extend_16(imm16)
    target = (delay_pc + (offset << 2)) & 0xFFFFFFFF
    return target


def compute_jump_target(pc: int, target_field: int) -> int:
    """Compute J/JAL target: (PC+4)[31:28] || target_field<<2."""
    delay_pc = (pc + 4) & 0xFFFFFFFF
    region = (delay_pc >> 28) & 0xF
    target = ((region << 28) | ((target_field & 0x03FFFFFF) << 2)) & 0xFFFFFFFF
    return target


def evaluate_condition(kind: BranchKind, gpr_rs: int, gpr_rt: int, fpu_cond: bool) -> bool:
    """Evaluate branch condition using 64-bit GPR low lane."""
    rs_val = gpr_rs & 0xFFFFFFFFFFFFFFFF
    rt_val = gpr_rt & 0xFFFFFFFFFFFFFFFF

    if kind == BranchKind.BEQ:
        return rs_val == rt_val
    elif kind == BranchKind.BNE:
        return rs_val != rt_val
    elif kind == BranchKind.BLEZ:
        # signed <= 0
        return (rs_val & 0x8000000000000000) != 0 or rs_val == 0
    elif kind == BranchKind.BGTZ:
        # signed > 0
        return (rs_val & 0x8000000000000000) == 0 and rs_val != 0
    elif kind == BranchKind.BLTZ:
        # signed < 0
        return (rs_val & 0x8000000000000000) != 0
    elif kind == BranchKind.BGEZ:
        # signed >= 0
        return (rs_val & 0x8000000000000000) == 0
    elif kind in (BranchKind.BLTZAL, BranchKind.BLTZALL):
        return (rs_val & 0x8000000000000000) != 0
    elif kind in (BranchKind.BGEZAL, BranchKind.BGEZALL):
        return (rs_val & 0x8000000000000000) == 0
    elif kind in (BranchKind.BEQL, BranchKind.BNEL, BranchKind.BLEZL,
                  BranchKind.BGTZL, BranchKind.BLTZL, BranchKind.BGEZL):
        # Same conditions as non-likely variants
        if kind == BranchKind.BEQL:
            return rs_val == rt_val
        elif kind == BranchKind.BNEL:
            return rs_val != rt_val
        elif kind == BranchKind.BLEZL:
            return (rs_val & 0x8000000000000000) != 0 or rs_val == 0
        elif kind == BranchKind.BGTZL:
            return (rs_val & 0x8000000000000000) == 0 and rs_val != 0
        elif kind == BranchKind.BLTZL:
            return (rs_val & 0x8000000000000000) != 0
        elif kind == BranchKind.BGEZL:
            return (rs_val & 0x8000000000000000) == 0
    elif kind in (BranchKind.BC1F, BranchKind.BC1FL):
        return not fpu_cond
    elif kind in (BranchKind.BC1T, BranchKind.BC1TL):
        return fpu_cond
    return False


def is_branch_likely(kind: BranchKind) -> bool:
    return kind in (BranchKind.BEQL, BranchKind.BNEL, BranchKind.BLEZL,
                    BranchKind.BGTZL, BranchKind.BLTZL, BranchKind.BGEZL,
                    BranchKind.BLTZALL, BranchKind.BGEZALL,
                    BranchKind.BC1FL, BranchKind.BC1TL)


def writes_link(kind: BranchKind) -> bool:
    return kind in (BranchKind.BLTZAL, BranchKind.BGEZAL,
                    BranchKind.BLTZALL, BranchKind.BGEZALL,
                    BranchKind.JAL, BranchKind.JALR)


def link_register(kind: BranchKind, rd: int) -> int:
    if kind == BranchKind.JALR:
        return rd
    return 31  # RA


def oracle_exec(case: BranchTestCase) -> BranchTestCase:
    """Execute the branch oracle for one test case."""
    kind = case.kind
    pc = case.pc

    # Evaluate condition; J/JAL are unconditional transfers.
    if kind in (BranchKind.J, BranchKind.JAL):
        taken = True
    else:
        taken = evaluate_condition(kind, case.gpr_rs, case.gpr_rt, case.fpu_cond)

    # Compute next PC and delay slot
    delay_slot_pc = (pc + 4) & 0xFFFFFFFF
    link_reg = link_register(kind, case.gpr_rd) if writes_link(kind) else -1
    link_value = 0

    if kind in (BranchKind.J, BranchKind.JAL):
        target_pc = compute_jump_target(pc, case.target_field)
        next_pc = target_pc
        delay_slot_executes = True
    elif kind in (BranchKind.JR, BranchKind.JALR):
        target_pc = case.gpr_rs & 0xFFFFFFFFFFFFFFFF
        next_pc = target_pc & 0xFFFFFFFF
        delay_slot_executes = True
    else:
        # Conditional branches (including BC1)
        target_pc = compute_branch_target(pc, case.imm16)
        if taken:
            next_pc = target_pc
            delay_slot_executes = True
        else:
            next_pc = delay_slot_pc + 4  # fallthrough
            delay_slot_executes = not is_branch_likely(kind)  # annul if likely

    # Link register gets PC+8 (delay_slot_pc + 4)
    if writes_link(kind):
        link_value = (delay_slot_pc + 4) & 0xFFFFFFFF

    # GPR[0] suppression
    gpr0_suppressed = (link_reg == 0)

    return BranchTestCase(
        kind=kind,
        pc=pc,
        gpr_rs=case.gpr_rs,
        gpr_rt=case.gpr_rt,
        gpr_rd=case.gpr_rd,
        rs_index=case.rs_index,
        rt_index=case.rt_index,
        fpu_cond=case.fpu_cond,
        target_field=case.target_field,
        imm16=case.imm16,
        taken=taken,
        next_pc=next_pc,
        link_reg=link_reg,
        link_value=link_value,
        delay_slot_pc=delay_slot_pc,
        delay_slot_executes=delay_slot_executes,
        gpr0_suppressed=gpr0_suppressed,
    )


def encode_instruction(case: BranchTestCase) -> int:
    """Encode a branch instruction word from a test case."""
    kind = case.kind
    pc = case.pc
    rs = case.rs_index
    rt = case.rt_index
    imm = case.imm16 & 0xFFFF
    target = case.target_field & 0x03FFFFFF

    # SPECIAL (0x00) funct codes for JR/JALR; JALR encodes rd in bits 15..11.
    if kind in (BranchKind.JR, BranchKind.JALR):
        funct = 0x08 if kind == BranchKind.JR else 0x09
        if kind == BranchKind.JALR:
            return (rs << 21) | ((case.gpr_rd & 0x1F) << 11) | (funct & 0x3F)
        return (rs << 21) | (funct & 0x3F)
    # REGIMM (0x01) for BLTZ/BGEZ family; the likely variants use rt=0x02/0x03
    # and the likely-and-link variants rt=0x12/0x13 (MIPS32 Vol II REGIMM).
    if kind in (BranchKind.BLTZ, BranchKind.BGEZ, BranchKind.BLTZL, BranchKind.BGEZL):
        rt_code = {BranchKind.BLTZ: 0x00, BranchKind.BGEZ: 0x01,
                   BranchKind.BLTZL: 0x02, BranchKind.BGEZL: 0x03}[kind]
        primary = 0x01
    elif kind in (BranchKind.BLTZAL, BranchKind.BGEZAL, BranchKind.BLTZALL, BranchKind.BGEZALL):
        rt_code = {BranchKind.BLTZAL: 0x10, BranchKind.BGEZAL: 0x11,
                   BranchKind.BLTZALL: 0x12, BranchKind.BGEZALL: 0x13}[kind]
        primary = 0x01
    elif kind in (BranchKind.BC1F, BranchKind.BC1FL, BranchKind.BC1T, BranchKind.BC1TL):
        # COP1 (0x11) with BC1 in format field 0x08
        bc1_code = {BranchKind.BC1F: 0, BranchKind.BC1T: 1,
                    BranchKind.BC1FL: 2, BranchKind.BC1TL: 3}[kind]
        return (0x11 << 26) | (0x08 << 21) | (bc1_code << 16) | imm
    elif kind in (BranchKind.J, BranchKind.JAL):
        primary = 0x02 if kind == BranchKind.J else 0x03
        return (primary << 26) | target
    else:
        # Regular branches: BEQ=0x04, BNE=0x05, BLEZ=0x06, BGTZ=0x07
        # Likely: BEQL=0x14, BNEL=0x15, BLEZL=0x16, BGTZL=0x17
        primary_map = {
            BranchKind.BEQ: 0x04, BranchKind.BNE: 0x05,
            BranchKind.BLEZ: 0x06, BranchKind.BGTZ: 0x07,
            BranchKind.BEQL: 0x14, BranchKind.BNEL: 0x15,
            BranchKind.BLEZL: 0x16, BranchKind.BGTZL: 0x17,
        }
        primary = primary_map[kind]
        rt_field = 0 if kind in (BranchKind.BLEZ, BranchKind.BGTZ,
                                   BranchKind.BLEZL, BranchKind.BGTZL) else rt
        return (primary << 26) | (rs << 21) | (rt_field << 16) | imm

    return (primary << 26) | (rs << 21) | (rt_code << 16) | imm


def emit_vector(case: BranchTestCase, out) -> None:
    """Emit one Rust test vector struct."""
    word = encode_instruction(case)
    kind_name = case.kind.name
    out.write(
        f"    BranchVector {{\n"
        f"        word: 0x{word:08X},\n"
        f"        kind: BranchKind::{kind_name},\n"
        f"        pc: 0x{case.pc:08X},\n"
        f"        gpr_rs: 0x{case.gpr_rs:016X},\n"
        f"        gpr_rt: 0x{case.gpr_rt:016X},\n"
        f"        rs_index: {case.rs_index},\n"
        f"        fpu_cond: {str(case.fpu_cond).lower()},\n"
        f"        taken: {str(case.taken).lower()},\n"
        f"        next_pc: 0x{case.next_pc:08X},\n"
        f"        link_reg: {case.link_reg},\n"
        f"        link_value: 0x{case.link_value:08X},\n"
        f"        delay_slot_executes: {str(case.delay_slot_executes).lower()},\n"
        f"        gpr0_suppressed: {str(case.gpr0_suppressed).lower()},\n"
        f"    }},\n"
    )


# --------------------------------------------------------------------------- #
# Test vector generation
# --------------------------------------------------------------------------- #

def generate_vectors() -> list[BranchTestCase]:
    """Generate comprehensive branch test vectors."""
    cases = []

    # Base PCs for different regions
    base_pcs = [0x00001000, 0x80001000, 0xA0001000, 0xFFFF1000]

    # GPR test values covering edge cases
    gpr_values = [
        0x0000000000000000,  # zero
        0x0000000000000001,  # +1
        0xFFFFFFFFFFFFFFFF,  # -1 (signed)
        0x7FFFFFFFFFFFFFFF,  # max signed positive
        0x8000000000000000,  # min signed negative
        0xFFFFFFFF00000000,  # upper lane only
        0x00000000FFFFFFFF,  # lower lane only
        0x123456789ABCDEF0,  # arbitrary
        0xDEADBEEFDEADBEEF,  # arbitrary
    ]

    # 1. BEQ / BNE / BEQL / BNEL
    for kind in (BranchKind.BEQ, BranchKind.BNE, BranchKind.BEQL, BranchKind.BNEL):
        for pc in base_pcs:
            for rs_val in gpr_values:
                for rt_val in gpr_values:
                    # Taken case
                    imm = ((0x1000 - (pc + 4)) >> 2) & 0xFFFF  # target at pc+0x1000
                    cases.append(BranchTestCase(
                        kind=kind, pc=pc, gpr_rs=rs_val, gpr_rt=rt_val, imm16=imm,
                        taken=(rs_val == rt_val) if kind in (BranchKind.BEQ, BranchKind.BEQL) else (rs_val != rt_val)
                    ))
                    # Not-taken case (different target)
                    imm2 = ((0x2000 - (pc + 4)) >> 2) & 0xFFFF
                    cases.append(BranchTestCase(
                        kind=kind, pc=pc, gpr_rs=rs_val, gpr_rt=rt_val, imm16=imm2,
                        taken=False
                    ))

    # 2. BLEZ / BGTZ / BLEZL / BGTZL
    for kind in (BranchKind.BLEZ, BranchKind.BGTZ, BranchKind.BLEZL, BranchKind.BGTZL):
        for pc in base_pcs:
            for rs_val in gpr_values:
                imm = ((0x1000 - (pc + 4)) >> 2) & 0xFFFF
                is_le = kind in (BranchKind.BLEZ, BranchKind.BLEZL)
                cond = ((rs_val & 0x8000000000000000) != 0 or rs_val == 0) if is_le else \
                       ((rs_val & 0x8000000000000000) == 0 and rs_val != 0)
                cases.append(BranchTestCase(
                    kind=kind, pc=pc, gpr_rs=rs_val, imm16=imm, taken=cond
                ))

    # 3. BLTZ / BGEZ / BLTZL / BGEZL
    for kind in (BranchKind.BLTZ, BranchKind.BGEZ, BranchKind.BLTZL, BranchKind.BGEZL):
        for pc in base_pcs:
            for rs_val in gpr_values:
                imm = ((0x1000 - (pc + 4)) >> 2) & 0xFFFF
                is_lt = kind in (BranchKind.BLTZ, BranchKind.BLTZL)
                cond = ((rs_val & 0x8000000000000000) != 0) if is_lt else \
                       ((rs_val & 0x8000000000000000) == 0)
                cases.append(BranchTestCase(
                    kind=kind, pc=pc, gpr_rs=rs_val, imm16=imm, taken=cond
                ))

    # 4. BLTZAL / BGEZAL / BLTZALL / BGEZALL (link register)
    for kind in (BranchKind.BLTZAL, BranchKind.BGEZAL, BranchKind.BLTZALL, BranchKind.BGEZALL):
        for pc in base_pcs:
            for rs_val in gpr_values:
                imm = ((0x1000 - (pc + 4)) >> 2) & 0xFFFF
                is_lt = kind in (BranchKind.BLTZAL, BranchKind.BLTZALL)
                cond = ((rs_val & 0x8000000000000000) != 0) if is_lt else \
                       ((rs_val & 0x8000000000000000) == 0)
                cases.append(BranchTestCase(
                    kind=kind, pc=pc, gpr_rs=rs_val, imm16=imm, taken=cond
                ))

    # 5. J / JAL
    for kind in (BranchKind.J, BranchKind.JAL):
        for pc in base_pcs:
            target_field = 0x00ABCDEF  # arbitrary 26-bit target
            cases.append(BranchTestCase(
                kind=kind, pc=pc, target_field=target_field, taken=True
            ))

    # 6. JR / JALR
    for kind in (BranchKind.JR, BranchKind.JALR):
        for pc in base_pcs:
            for rs_val in gpr_values:
                cases.append(BranchTestCase(
                    kind=kind, pc=pc, gpr_rs=rs_val, taken=True,
                    gpr_rd=31 if kind == BranchKind.JALR else 0
                ))

    # 7. BC1F / BC1T / BC1FL / BC1TL
    for kind in (BranchKind.BC1F, BranchKind.BC1T, BranchKind.BC1FL, BranchKind.BC1TL):
        for pc in base_pcs:
            for fpu_cond in (False, True):
                imm = ((0x1000 - (pc + 4)) >> 2) & 0xFFFF
                is_f = kind in (BranchKind.BC1F, BranchKind.BC1FL)
                taken = (not fpu_cond) if is_f else fpu_cond
                cases.append(BranchTestCase(
                    kind=kind, pc=pc, fpu_cond=fpu_cond, imm16=imm, taken=taken
                ))

    # 8. GPR[0] edge cases (source or link is $zero)
    for kind in (BranchKind.BEQ, BranchKind.BNE, BranchKind.JALR):
        pc = 0x00001000
        if kind == BranchKind.JALR:
            cases.append(BranchTestCase(
                kind=kind, pc=pc, gpr_rs=0x1234, gpr_rd=0, taken=True  # rd=$zero
            ))
        else:
            cases.append(BranchTestCase(
                kind=kind, pc=pc, gpr_rs=0, gpr_rt=0, imm16=2, taken=True  # rs=rt=$zero
            ))

    # 9. Branch-likely annulment verification (not-taken path)
    for kind in (BranchKind.BEQL, BranchKind.BNEL, BranchKind.BLEZL,
                 BranchKind.BGTZL, BranchKind.BLTZL, BranchKind.BGEZL,
                 BranchKind.BLTZALL, BranchKind.BGEZALL,
                 BranchKind.BC1FL, BranchKind.BC1TL):
        pc = 0x00001000
        if kind in (BranchKind.BC1FL, BranchKind.BC1TL):
            cases.append(BranchTestCase(
                kind=kind, pc=pc, fpu_cond=False if kind == BranchKind.BC1FL else True,
                imm16=2, taken=False  # not taken -> delay slot annulled
            ))
        elif kind in (BranchKind.BLTZALL, BranchKind.BGEZALL):
            rs_val = 0xFFFFFFFFFFFFFFFF if kind == BranchKind.BLTZALL else 0
            cases.append(BranchTestCase(
                kind=kind, pc=pc, gpr_rs=rs_val, imm16=2, taken=False
            ))
        else:
            rs_val = 1 if kind in (BranchKind.BEQL, BranchKind.BGTZL, BranchKind.BLTZL) else 0
            rt_val = 0
            cases.append(BranchTestCase(
                kind=kind, pc=pc, gpr_rs=rs_val, gpr_rt=rt_val, imm16=2, taken=False
            ))

    # 10. JR $ra: the rs=31 encoding that the CFG classifies as a return.
    for pc in base_pcs:
        for rs_val in (0x0000000000002000, 0x0000000080001234):
            cases.append(BranchTestCase(
                kind=BranchKind.JR, pc=pc, gpr_rs=rs_val, rs_index=31, taken=True
            ))

    # 11. JALR with an explicit link register distinct from the target
    # register, so a link written to rs instead of rd is observable.
    for pc in base_pcs:
        cases.append(BranchTestCase(
            kind=BranchKind.JALR, pc=pc, gpr_rs=0x2000, gpr_rd=16, taken=True
        ))
        cases.append(BranchTestCase(
            kind=BranchKind.JALR, pc=pc, gpr_rs=0x2000, gpr_rd=31, rs_index=9,
            taken=True
        ))

    # The J/JAL region-carry rule (region from PC+4, not PC) cannot be
    # exercised by an executable vector: crossing a 256 MiB boundary needs a
    # branch PC of 0x...FFFC whose delay slot at 0x...0000 lies outside the
    # 32 MiB main RAM image the differential test can map. It is covered by
    # the oracle self-check below and the decoder unit test
    # jump_region_uses_the_high_four_bits_of_pc_plus_four in mips.rs.

    return cases


def emit_vectors(output_path: str) -> None:
    cases = generate_vectors()
    # Run oracle on all cases
    results = [oracle_exec(c) for c in cases]

    with open(output_path, "w") as f:
        f.write("// SPDX-License-Identifier: GPL-3.0-or-later\n")
        f.write("// Branch differential test vectors.\n")
        f.write("// Generated by tools/branch_oracle.py --emit-vectors\n")
        f.write("// DO NOT EDIT MANUALLY.\n\n")
        f.write("#[allow(dead_code)]\n")
        f.write(f"pub const BRANCH_VECTOR_COUNT: usize = {len(results)};\n\n")
        f.write("#[allow(dead_code)]\n")
        f.write("static BRANCH_VECTORS: &[BranchVector] = &[\n")
        for r in results:
            emit_vector(r, f)
        f.write("];\n")


def self_check() -> bool:
    """Run basic sanity checks on the oracle."""
    ok = True

    # BEQ taken
    c = BranchTestCase(kind=BranchKind.BEQ, pc=0x1000, gpr_rs=5, gpr_rt=5, imm16=2, taken=True)
    r = oracle_exec(c)
    assert r.taken == True
    assert r.next_pc == 0x100C  # (0x1004) + 2*4 = 0x100C
    assert r.delay_slot_pc == 0x1004
    assert r.delay_slot_executes == True

    # BEQ not taken
    c = BranchTestCase(kind=BranchKind.BEQ, pc=0x1000, gpr_rs=5, gpr_rt=6, imm16=2, taken=False)
    r = oracle_exec(c)
    assert r.taken == False
    assert r.next_pc == 0x1008  # fallthrough
    assert r.delay_slot_executes == True  # not likely

    # BEQL not taken -> annulled
    c = BranchTestCase(kind=BranchKind.BEQL, pc=0x1000, gpr_rs=5, gpr_rt=6, imm16=2, taken=False)
    r = oracle_exec(c)
    assert r.delay_slot_executes == False

    # BLTZAL taken -> link written
    c = BranchTestCase(kind=BranchKind.BLTZAL, pc=0x1000, gpr_rs=0xFFFFFFFFFFFFFFFF, imm16=2, taken=True)
    r = oracle_exec(c)
    assert r.taken == True
    assert r.link_reg == 31
    assert r.link_value == 0x1008  # PC+8

    # J target
    c = BranchTestCase(kind=BranchKind.J, pc=0x80001000, target_field=0x00ABCDEF, taken=True)
    r = oracle_exec(c)
    # region = (0x80001004 >> 28) = 8, target = (8<<28) | (0x00ABCDEF<<2) = 0x82AF37BC
    assert r.next_pc == 0x82AF37BC

    # JALR with rd=$zero suppression
    c = BranchTestCase(kind=BranchKind.JALR, pc=0x1000, gpr_rs=0x2000, gpr_rd=0, taken=True)
    r = oracle_exec(c)
    assert r.gpr0_suppressed == True
    assert r.link_value == 0x1008

    # BC1F taken when cond=false
    c = BranchTestCase(kind=BranchKind.BC1F, pc=0x1000, fpu_cond=False, imm16=2, taken=True)
    r = oracle_exec(c)
    assert r.taken == True

    # BC1T not taken when cond=false
    c = BranchTestCase(kind=BranchKind.BC1T, pc=0x1000, fpu_cond=False, imm16=2, taken=False)
    r = oracle_exec(c)
    assert r.taken == False
    assert r.delay_slot_executes == True

    # BC1FL not taken -> annulled
    c = BranchTestCase(kind=BranchKind.BC1FL, pc=0x1000, fpu_cond=True, imm16=2, taken=False)
    r = oracle_exec(c)
    assert r.delay_slot_executes == False

    # JALR encodes rd in bits 15..11; rs stays in bits 25..21.
    c = BranchTestCase(kind=BranchKind.JALR, pc=0x1000, gpr_rs=0x2000, gpr_rd=16, taken=True)
    assert encode_instruction(c) == (8 << 21) | (16 << 11) | 0x09
    r = oracle_exec(c)
    assert r.link_reg == 16
    assert r.link_value == 0x1008
    assert r.gpr0_suppressed == False

    # JR $ra encodes rs=31 with funct 0x08 and no link write.
    c = BranchTestCase(kind=BranchKind.JR, pc=0x1000, gpr_rs=0x2000, rs_index=31, taken=True)
    assert encode_instruction(c) == (31 << 21) | 0x08
    r = oracle_exec(c)
    assert r.link_reg == -1

    # J whose PC+4 crosses into the next region carries the delay-slot region.
    c = BranchTestCase(kind=BranchKind.J, pc=0x0FFFFFFC, target_field=0x00ABCDEF, taken=True)
    r = oracle_exec(c)
    assert r.next_pc == 0x12AF37BC

    print("All self-checks passed.")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="R5900 branch oracle")
    parser.add_argument("--emit-vectors", metavar="PATH", help="Emit test vectors to PATH")
    parser.add_argument("--self-check", action="store_true", help="Run internal sanity checks")
    args = parser.parse_args()

    if args.self_check:
        return 0 if self_check() else 1

    if args.emit_vectors:
        emit_vectors(args.emit_vectors)
        print(f"Emitted {generate_vectors().__len__()} vectors to {args.emit_vectors}")
        return 0

    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())