// SPDX-License-Identifier: GPL-3.0-or-later
//! Black-box robustness checks for the public decoder API.

use reps2_frontend::mips;

/// Deterministic xorshift64 so failures reproduce exactly.
fn next_random(state: &mut u64) -> u64 {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    *state
}

#[test]
fn decode_is_total_over_a_structured_sweep_of_opcode_and_field_space() {
    // Every (opcode, rs, rt, funct) combination with two representative values
    // for the remaining rd/sa bits: 64 * 32 * 32 * 64 * 2 = 8,388,608 words.
    for opcode in 0u32..64 {
        for rs in 0u32..32 {
            for rt in 0u32..32 {
                for funct in 0u32..64 {
                    for rd_sa in [0u32, 0x1F << 6 | 0x1F << 11] {
                        let word = opcode << 26 | rs << 21 | rt << 16 | rd_sa | funct;
                        let _ = mips::decode(word, 0x0010_0000);
                    }
                }
            }
        }
    }
}

#[test]
fn decode_is_total_for_random_words_and_extreme_program_counters() {
    let mut rng = 0x2545_F491_4F6C_DD1D_u64;
    for _ in 0..2_000_000 {
        let word = next_random(&mut rng) as u32;
        let pc = match next_random(&mut rng) % 4 {
            0 => 0,
            1 => u32::MAX - 3,
            2 => 0x8000_0000,
            _ => (next_random(&mut rng) as u32) & !3,
        };
        let _ = mips::decode(word, pc);
    }
}
