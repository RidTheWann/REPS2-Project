//! REPS2's independent static-recompilation frontend.
//!
//! The frontend owns file parsing, instruction decoding, analysis, IR, and
//! code generation. The later C++20 runtime consumes generated C++ and a
//! machine-readable build manifest; no per-instruction FFI boundary is used.

pub mod address;
pub mod analysis;
pub mod build_manifest;
pub mod codegen;
pub mod control_codegen;
pub mod control_ir;
pub mod coverage;
pub mod digest;
pub mod elf;
pub mod ir;
pub mod iso9660;
pub mod json;
pub mod library;
pub mod mips;
pub mod native_build;

/// Frontend version reported by the command-line tool.
pub const VERSION: &str = env!("CARGO_PKG_VERSION");

/// Exact Rust compilation target triple recorded in build manifests.
pub const BUILD_TARGET: &str = env!("REPS2_BUILD_TARGET");
