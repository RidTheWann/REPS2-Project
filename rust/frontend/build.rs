// SPDX-License-Identifier: GPL-3.0-or-later
//! Embeds the exact compilation target triple for build-manifest provenance.

fn main() {
    let target = std::env::var("TARGET").expect("Cargo sets TARGET for build scripts");
    println!("cargo:rustc-env=REPS2_BUILD_TARGET={target}");
    println!("cargo:rerun-if-changed=build.rs");
}
