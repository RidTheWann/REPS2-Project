//! REPS2's independent static-recompilation frontend.
//!
//! The frontend owns file parsing, instruction decoding, analysis, IR, and
//! code generation. The later C++20 runtime consumes generated C++ and a
//! machine-readable build manifest; no per-instruction FFI boundary is used.

pub mod elf;
pub mod mips;

/// Frontend version reported by the command-line tool.
pub const VERSION: &str = env!("CARGO_PKG_VERSION");
