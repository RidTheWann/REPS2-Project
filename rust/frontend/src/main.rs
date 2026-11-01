// SPDX-License-Identifier: GPL-3.0-or-later
use std::env;
use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use reps2_frontend::analysis::{self, AnalysisLimits};
use reps2_frontend::build_manifest::{BuildManifest, DiscoveryStats, ProgramStats};
use reps2_frontend::codegen;
use reps2_frontend::control_codegen;
use reps2_frontend::control_ir;
use reps2_frontend::coverage;
use reps2_frontend::digest;
use reps2_frontend::elf::{self, ParseLimits};
use reps2_frontend::ir;
use reps2_frontend::iso9660::{self, Iso9660};
use reps2_frontend::library;
use reps2_frontend::mips;
use reps2_frontend::native_build;

const MAX_INPUT_SIZE: u64 = 512 * 1024 * 1024;

fn parse_hex(value: &str) -> Result<u32, String> {
    let value = value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
        .unwrap_or(value);
    if value.is_empty() {
        return Err("expected a hexadecimal 32-bit value".to_owned());
    }
    u32::from_str_radix(value, 16).map_err(|_| "invalid hexadecimal 32-bit value".to_owned())
}

fn read_input(path: &Path) -> Result<Vec<u8>, String> {
    let metadata =
        fs::metadata(path).map_err(|error| format!("cannot inspect input file: {error}"))?;
    if metadata.len() > MAX_INPUT_SIZE {
        return Err("input file exceeds the 512 MiB safety limit".to_owned());
    }
    fs::read(path).map_err(|error| format!("cannot read input file: {error}"))
}

fn elf_info(path: &Path) -> Result<(), String> {
    let bytes = read_input(path)?;
    let image = elf::parse(&bytes, ParseLimits::default()).map_err(|error| error.to_string())?;

    println!("ELF32 little-endian image");
    println!(
        "  Type: {}{}",
        image.file_type,
        if image.is_executable() {
            " (executable)"
        } else {
            ""
        }
    );
    println!(
        "  Machine: {}{}",
        image.machine,
        if image.is_mips() {
            " (MIPS)"
        } else {
            " (not MIPS)"
        }
    );
    println!("  Entry point: 0x{:08X}", image.entry_point);
    println!("  Program headers: {}", image.program_headers.len());
    println!("  Section headers: {}", image.section_headers.len());
    println!("  PT_LOAD segments: {}", image.loadable_segment_count());
    println!("  Symbols: {}", image.symbols.len());
    println!("  Relocations: {}", image.relocations.len());

    for (index, section) in image.section_headers.iter().enumerate() {
        println!(
            "  [{index:>3}] {:<20} type={:<3} addr=0x{:08X} size=0x{:X}",
            section.name, section.kind, section.virtual_address, section.size
        );
    }

    if !image.is_mips() {
        eprintln!("Warning: ELF machine is not EM_MIPS; this is not a supported PS2 EE target.");
    }
    Ok(())
}

fn decode_word(word_text: &str, pc_text: Option<&str>) -> Result<(), String> {
    let word = parse_hex(word_text)?;
    let pc = pc_text.map(parse_hex).transpose()?.unwrap_or(0);
    let instruction = mips::decode(word, pc);
    println!(
        "0x{word:08X}  {}  rs={} rt={} rd={}",
        instruction.opcode.mnemonic(),
        instruction.rs,
        instruction.rt,
        instruction.rd
    );
    if instruction.has_delay_slot {
        print!("  [delay-slot]");
    }
    if matches!(
        instruction.opcode,
        mips::Opcode::J
            | mips::Opcode::Jal
            | mips::Opcode::Beq
            | mips::Opcode::Bne
            | mips::Opcode::Blez
            | mips::Opcode::Bgtz
            | mips::Opcode::Bltz
            | mips::Opcode::Bgez
            | mips::Opcode::Bltzal
            | mips::Opcode::Bgezal
            | mips::Opcode::Beql
            | mips::Opcode::Bnel
            | mips::Opcode::Blezl
            | mips::Opcode::Bgtzl
            | mips::Opcode::Bltzl
            | mips::Opcode::Bgezl
            | mips::Opcode::Bltzall
            | mips::Opcode::Bgezall
    ) {
        print!("  target=0x{:08X}", instruction.target);
    }
    if matches!(
        instruction.opcode,
        mips::Opcode::Addi
            | mips::Opcode::Addiu
            | mips::Opcode::Slti
            | mips::Opcode::Sltiu
            | mips::Opcode::Lb
            | mips::Opcode::Lh
            | mips::Opcode::Lw
            | mips::Opcode::Lbu
            | mips::Opcode::Lhu
            | mips::Opcode::Lwu
            | mips::Opcode::Ld
            | mips::Opcode::Sd
            | mips::Opcode::Lwl
            | mips::Opcode::Lwr
            | mips::Opcode::Swl
            | mips::Opcode::Swr
            | mips::Opcode::Ldl
            | mips::Opcode::Ldr
            | mips::Opcode::Sdl
            | mips::Opcode::Sdr
            | mips::Opcode::Sb
            | mips::Opcode::Sh
            | mips::Opcode::Sw
    ) {
        print!("  imm={}", instruction.signed_immediate());
    }
    println!();
    if !instruction.is_supported() {
        return Err("instruction is outside the currently implemented decoder subset".to_owned());
    }
    Ok(())
}

fn analyze_elf(path: &Path, options: AnalyzeOptions) -> Result<(), String> {
    let bytes = read_input(path)?;
    let image = elf::parse(&bytes, ParseLimits::default()).map_err(|error| error.to_string())?;
    if !image.is_mips() {
        return Err("ELF machine is not EM_MIPS; refusing PS2 EE analysis".to_owned());
    }

    let limits = options.limits.to_limits();
    let (graph, discovery) = if options.discover {
        let analysis::DiscoveryAnalysis {
            graph,
            discovery,
            accepted_roots,
            skipped_roots,
            skipped_blocks,
        } = analysis::discover_elf_with_discovery(&image, &bytes, limits)
            .map_err(analysis_error_message)?;
        (
            graph,
            Some((discovery, accepted_roots, skipped_roots, skipped_blocks)),
        )
    } else {
        let graph =
            analysis::discover_elf(&image, &bytes, limits).map_err(analysis_error_message)?;
        (graph, None)
    };
    let edges = graph.edges();

    println!("REPS2 reachable executable analysis");
    println!(
        "  Limits: blocks={}, instructions={}, function roots={}",
        limits.max_blocks, limits.max_instructions, limits.max_function_roots
    );
    if let Some((found, accepted_roots, skipped_roots, skipped_blocks)) = &discovery {
        println!("  Root discovery: enabled (static scan of executable bytes)");
        println!("    Scanned words: {}", found.scanned_words);
        println!("    JAL call-target roots: {}", found.call_targets.len());
        println!(
            "    Stack-prologue roots: {}",
            found.prologue_candidates.len()
        );
        println!("    Distinct candidates: {}", found.candidate_count());
        println!("    Truncated by limit: {}", found.truncated);
        println!("    Accepted roots: {accepted_roots}");
        println!("    Skipped candidates: {}", skipped_roots.len());
        println!("    Skipped heuristic blocks: {}", skipped_blocks.len());
    } else {
        println!("  Root discovery: disabled (ELF entry and STT_FUNC symbols only)");
    }
    println!("  Entry roots: {}", graph.entry_points.len());
    println!("  Basic blocks: {}", graph.blocks.len());
    println!(
        "  Decoded instructions: {}",
        graph.decoded_instruction_count
    );
    println!("  Static edges: {}", edges.len());
    println!("  Unsupported blocks: {}", graph.unsupported_block_count());
    println!(
        "  Unresolved indirect jumps: {}",
        graph.unresolved_indirect_jumps.len()
    );
    println!(
        "  External/unmapped targets: {}",
        graph.external_targets.len()
    );

    for entry in &graph.entry_points {
        let role = if entry.is_elf_entry {
            "ELF entry"
        } else {
            "function"
        };
        let labels = if entry.labels.is_empty() {
            String::new()
        } else {
            format!(" ({})", entry.labels.join(", "))
        };
        println!("  {role}: 0x{:08X}{labels}", entry.address);
    }

    println!("  Block preview:");
    for block in graph.blocks.values().take(64) {
        println!(
            "    0x{:08X}: {} instruction(s), {}",
            block.start_pc,
            block.instruction_count(),
            block.terminator.label()
        );
    }
    if graph.blocks.len() > 64 {
        println!("    ... {} more block(s)", graph.blocks.len() - 64);
    }

    Ok(())
}

fn profile_elf(path: &Path) -> Result<(), String> {
    let bytes = read_input(path)?;
    let image = elf::parse(&bytes, ParseLimits::default()).map_err(|error| error.to_string())?;
    if !image.is_mips() {
        return Err("ELF machine is not EM_MIPS; refusing PS2 EE profiling".to_owned());
    }
    let profile = coverage::profile(&image, &bytes).map_err(|error| error.to_string())?;

    let percentage = |count: u64| {
        if profile.scanned_words == 0 {
            0.0
        } else {
            100.0 * count as f64 / profile.scanned_words as f64
        }
    };

    println!("REPS2 decoder coverage profile");
    println!("  Entry point: 0x{:08X}", image.entry_point);
    println!(
        "  File-backed executable bytes: {}",
        profile.file_backed_executable_bytes
    );
    println!("  Scanned complete words: {}", profile.scanned_words);
    println!(
        "  Supported words: {} ({:.2}%)",
        profile.supported_words,
        percentage(profile.supported_words)
    );
    println!(
        "  Trap words (syscall/break): {} ({:.2}%)",
        profile.trap_words,
        percentage(profile.trap_words)
    );
    println!(
        "  Unsupported words: {} ({:.2}%)",
        profile.unsupported_words,
        percentage(profile.unsupported_words)
    );
    println!(
        "  Scope: every aligned word in executable PT_LOAD file bytes; embedded data is included and no word is claimed reachable."
    );

    println!("  Supported opcodes:");
    for (mnemonic, count) in &profile.supported_opcodes {
        println!("    {mnemonic:<10} {count}");
    }

    println!("  Unsupported families:");
    for (family, count) in &profile.unsupported_families {
        println!("    {:<10} {count}", family.label());
    }

    let mut unsupported: Vec<(u32, u64)> = profile
        .unsupported_encodings
        .iter()
        .map(|(word, count)| (*word, *count))
        .collect();
    unsupported.sort_by(|(left_word, left_count), (right_word, right_count)| {
        right_count
            .cmp(left_count)
            .then_with(|| left_word.cmp(right_word))
    });
    println!("  Unsupported encodings (most frequent first):");
    for (word, count) in unsupported.iter().take(32) {
        println!(
            "    0x{word:08X} count={count:<8} family={}",
            mips::encoding_family(*word).label()
        );
    }
    if unsupported.len() > 32 {
        println!("    ... {} additional encoding(s)", unsupported.len() - 32);
    }

    match &profile.entry_scan {
        Some(scan) => {
            println!("  Linear entry scan (bounded, not reachability):");
            println!(
                "    {} decoded word(s) from 0x{:08X} before stop",
                scan.decoded_words, image.entry_point
            );
            match scan.stop {
                coverage::EntryScanStop::Unsupported(family) => println!(
                    "    stop at 0x{:08X}: word 0x{:08X} is unsupported ({})",
                    scan.stop_pc,
                    scan.stop_word.unwrap_or(0),
                    family.label()
                ),
                coverage::EntryScanStop::Trap(mnemonic) => println!(
                    "    stop at 0x{:08X}: word 0x{:08X} is a trap ({mnemonic})",
                    scan.stop_pc,
                    scan.stop_word.unwrap_or(0)
                ),
                coverage::EntryScanStop::SegmentEnd => println!(
                    "    stop at 0x{:08X}: end of file-backed executable bytes",
                    scan.stop_pc
                ),
                coverage::EntryScanStop::WordLimit => println!(
                    "    stop at 0x{:08X}: reached the {}-word linear scan limit",
                    scan.stop_pc,
                    coverage::LINEAR_ENTRY_SCAN_LIMIT
                ),
            }
        }
        None => {
            println!("  Linear entry scan: entry point is not inside file-backed executable bytes")
        }
    }

    Ok(())
}

fn open_iso(path: &Path) -> Result<Iso9660<File>, String> {
    let file = File::open(path).map_err(|error| {
        format!(
            "cannot open read-only disc image {}: {error}",
            path.display()
        )
    })?;
    Iso9660::open(file).map_err(|error| error.to_string())
}

fn disc_info(path: &Path) -> Result<(), String> {
    let mut iso = open_iso(path)?;
    let info = iso.volume_info().clone();

    println!("REPS2 read-only ISO 9660 inspection");
    println!("  Image: {}", path.display());
    println!("  Volume identifier: {}", info.volume_identifier);
    println!("  Logical block size: {} bytes", info.logical_block_size);
    println!(
        "  Declared volume: {} blocks ({} bytes)",
        info.volume_space_blocks, info.volume_bytes
    );
    println!("  Image size: {} bytes", info.image_bytes);
    println!("  Root directory:");

    let entries = iso.root_entries().map_err(|error| error.to_string())?;
    for entry in entries.iter().take(128) {
        let kind = if entry.is_directory() { "DIR " } else { "FILE" };
        println!(
            "    [{kind}] {:<24} {} byte(s)",
            entry.name, entry.size_bytes
        );
    }
    if entries.len() > 128 {
        println!(
            "    ... {} additional root entr(y/ies)",
            entries.len() - 128
        );
    }

    match iso
        .boot_executable_path()
        .map_err(|error| error.to_string())?
    {
        Some(boot_path) => println!("  SYSTEM.CNF boot executable: {boot_path}"),
        None => println!("  SYSTEM.CNF: not present in the ISO root"),
    }

    println!("  Mode: read-only; no disc sectors are modified");
    Ok(())
}

fn extract_boot_elf(input: &Path, output: &Path) -> Result<(), String> {
    if output.exists() {
        let input_canonical = input
            .canonicalize()
            .map_err(|error| format!("cannot resolve disc image path: {error}"))?;
        let output_canonical = output
            .canonicalize()
            .map_err(|error| format!("cannot resolve existing output path: {error}"))?;
        if input_canonical == output_canonical {
            return Err(
                "output path resolves to the input ISO; refusing to overwrite the disc image"
                    .to_owned(),
            );
        }
        return Err(format!(
            "output already exists; refusing to overwrite: {}",
            output.display()
        ));
    }

    let mut iso = open_iso(input)?;
    let boot_path = iso
        .boot_executable_path()
        .map_err(|error| error.to_string())?
        .ok_or_else(|| {
            "ISO root does not contain SYSTEM.CNF with a BOOT2 or BOOT entry".to_owned()
        })?;

    let executable = iso
        .read_file(&boot_path, iso9660::MAX_BOOT_EXECUTABLE_BYTES)
        .map_err(|error| format!("cannot extract SYSTEM.CNF boot target {boot_path}: {error}"))?;
    let image = elf::parse(&executable, ParseLimits::default()).map_err(|error| {
        format!("SYSTEM.CNF target {boot_path} is not a valid supported ELF32 image: {error}")
    })?;
    if !image.is_mips() {
        return Err(format!(
            "SYSTEM.CNF target {boot_path} is not EM_MIPS; refusing extraction as a PS2 EE boot ELF"
        ));
    }
    if !image.is_executable() {
        return Err(format!(
            "SYSTEM.CNF target {boot_path} is not ET_EXEC; only static executable boot images are supported"
        ));
    }

    let mut destination = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(output)
        .map_err(|error| {
            format!(
                "cannot create new output file {}: {error}",
                output.display()
            )
        })?;
    if let Err(error) = destination
        .write_all(&executable)
        .and_then(|()| destination.sync_all())
    {
        drop(destination);
        let _ = fs::remove_file(output);
        return Err(format!(
            "cannot write extracted ELF to {}: {error}",
            output.display()
        ));
    }

    println!("Extracted PS2 boot executable");
    println!("  Disc image: {}", input.display());
    println!("  SYSTEM.CNF target: {boot_path}");
    println!("  ELF output: {}", output.display());
    println!("  Extracted bytes: {}", executable.len());
    println!("  ELF entry point: 0x{:08X}", image.entry_point);
    println!("  ELF PT_LOAD segments: {}", image.loadable_segment_count());
    println!("  Source image was opened read-only and was not modified");
    Ok(())
}

fn compile_elf(input: &Path, output: &Path, options: RecompileOptions) -> Result<(), String> {
    let bytes = read_input(input)?;
    let input_digest = digest::sha256_bytes(&bytes);
    let image = elf::parse(&bytes, ParseLimits::default()).map_err(|error| error.to_string())?;
    if !image.is_mips() {
        return Err("ELF machine is not EM_MIPS; refusing PS2 EE compilation".to_owned());
    }

    let limits = options.limits.to_limits();
    let ram_image = elf::build_main_ram_image(&bytes, &image)
        .map_err(|error| format!("cannot construct initial EE main-RAM image: {error}"))?;
    let (graph, discovery) = if options.discover {
        let analysis::DiscoveryAnalysis {
            graph,
            discovery,
            accepted_roots,
            skipped_roots,
            skipped_blocks,
        } = analysis::discover_elf_with_discovery(&image, &bytes, limits)
            .map_err(analysis_error_message)?;
        (
            graph,
            Some((discovery, accepted_roots, skipped_roots, skipped_blocks)),
        )
    } else {
        let graph =
            analysis::discover_elf(&image, &bytes, limits).map_err(analysis_error_message)?;
        (graph, None)
    };
    let program = control_ir::ControlProgram::lift(&graph).map_err(|error| error.to_string())?;
    let units = control_codegen::emit_cpp_units_with_entry(
        &program,
        image.entry_point,
        options.translation_units,
    )
    .map_err(|error| error.to_string())?;
    let ram_output = output.with_extension("ram.bin");
    fs::write(&ram_output, &ram_image.bytes)
        .map_err(|error| format!("cannot write initial EE RAM image: {error}"))?;
    let parts = write_translation_units(output, &units)?;

    let discovery_stats =
        discovery
            .as_ref()
            .map(
                |(found, accepted_roots, skipped_roots, skipped_blocks)| DiscoveryStats {
                    candidate_count: found.candidate_count(),
                    call_target_count: found.call_targets.len(),
                    prologue_candidate_count: found.prologue_candidates.len(),
                    scanned_words: found.scanned_words,
                    truncated: found.truncated,
                    accepted_root_count: *accepted_roots,
                    skipped_root_count: skipped_roots.len(),
                    skipped_block_count: skipped_blocks.len(),
                },
            );
    let primary_output = output_file_name(output);
    let part_outputs: Vec<String> = parts.iter().map(|part| output_file_name(part)).collect();
    let ram_output_name = output_file_name(&ram_output);
    let manifest = BuildManifest {
        input_path: input.to_string_lossy().into_owned(),
        input_size_bytes: bytes.len() as u64,
        input_sha256: digest::hex_digest(&input_digest),
        entry_point: image.entry_point,
        machine: image.machine,
        file_type: image.file_type,
        loadable_segment_count: image.loadable_segment_count(),
        limits: limits.into(),
        discovery: discovery_stats,
        program: ProgramStats {
            entry_point_count: program.entry_points.len(),
            basic_block_count: program.blocks.len(),
            guest_instruction_count: program.instruction_count(),
            unsupported_stop_count: program.unsupported_count(),
            static_edge_count: graph.edges().len(),
            unresolved_indirect_target_count: graph.unresolved_indirect_jumps.len(),
            external_target_count: graph.external_targets.len(),
        },
        translation_unit_count: units.len(),
        primary_output: primary_output.clone(),
        part_outputs: part_outputs.clone(),
        ram_output: ram_output_name.clone(),
        ram_image_bytes: ram_image.bytes.len(),
    };
    let manifest_output = output.with_extension("manifest.json");
    fs::write(&manifest_output, manifest.to_json()).map_err(|error| {
        format!(
            "cannot write build manifest to {}: {error}",
            manifest_output.display()
        )
    })?;

    let native_directory = if options.native_build {
        let mut sources = Vec::with_capacity(part_outputs.len() + 1);
        sources.push(primary_output.clone());
        sources.extend(part_outputs.iter().cloned());
        Some(write_native_project(
            output,
            &sources,
            Some(&ram_output_name),
        )?)
    } else {
        None
    };

    println!("Generated CFG-aware C++20 translation and initial EE RAM image");
    println!("  Input: {}", input.display());
    println!("  Input SHA-256: {}", manifest.input_sha256);
    println!("  C++ output: {}", output.display());
    println!("  RAM image: {}", ram_output.display());
    println!("  RAM image bytes: {}", ram_image.bytes.len());
    println!("  Build manifest: {}", manifest_output.display());
    println!(
        "  Analysis limits: blocks={}, instructions={}, function roots={}",
        limits.max_blocks, limits.max_instructions, limits.max_function_roots
    );
    if let Some(directory) = &native_directory {
        println!("  Native build project: {}", directory.display());
    }
    println!("  ELF entry point: 0x{:08X}", image.entry_point);
    println!("  PT_LOAD segments placed: {}", ram_image.segments.len());
    if let Some((found, accepted_roots, skipped_roots, skipped_blocks)) = &discovery {
        println!(
            "  Root discovery: enabled ({} candidate(s), {accepted_roots} accepted, {} candidate(s) skipped, {} heuristic block(s) skipped)",
            found.candidate_count(),
            skipped_roots.len(),
            skipped_blocks.len()
        );
    } else {
        println!("  Root discovery: disabled (ELF entry and STT_FUNC symbols only)");
    }
    println!("  Entry points: {}", program.entry_points.len());
    println!("  Basic blocks: {}", program.blocks.len());
    println!(
        "  Guest instructions represented: {}",
        program.instruction_count()
    );
    println!(
        "  Explicit unsupported stops: {}",
        program.unsupported_count()
    );
    println!(
        "  Guest trap stops (syscall/break): {}",
        program.trap_count()
    );
    println!(
        "  Unresolved indirect targets: {}",
        graph.unresolved_indirect_jumps.len()
    );
    println!(
        "  External/unmapped targets: {}",
        graph.external_targets.len()
    );
    print_translation_unit_summary(&parts);
    println!("  Runtime entry: reps2::generated::initialize_ee_state(EeState&, ram_image)");
    println!("  Execution entry: reps2::generated::execute_cfg(EeState&, instruction_budget)");
    Ok(())
}

/// Largest analysis-limit override accepted by the developer CLI. A 32 MiB EE
/// main-RAM image can hold at most 8,388,608 aligned words, so these ceilings
/// stay above every physically reachable count while keeping an explicit bound
/// on caller-selected work over untrusted input.
const MAX_BLOCKS_OVERRIDE: usize = 1 << 24;
const MAX_INSTRUCTIONS_OVERRIDE: usize = 1 << 25;
const MAX_FUNCTION_ROOTS_OVERRIDE: usize = 1 << 24;

/// Caller-selected bounds for bounded ELF analysis.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct LimitOptions {
    max_blocks: usize,
    max_instructions: usize,
    max_function_roots: usize,
}

impl Default for LimitOptions {
    fn default() -> Self {
        let limits = AnalysisLimits::default();
        Self {
            max_blocks: limits.max_blocks,
            max_instructions: limits.max_instructions,
            max_function_roots: limits.max_function_roots,
        }
    }
}

impl LimitOptions {
    fn to_limits(self) -> AnalysisLimits {
        AnalysisLimits {
            max_blocks: self.max_blocks,
            max_instructions: self.max_instructions,
            max_function_roots: self.max_function_roots,
            ..AnalysisLimits::default()
        }
    }
}

fn parse_limit(value: &str, flag: &str, ceiling: usize) -> Result<usize, String> {
    let parsed = value
        .parse::<usize>()
        .map_err(|_| format!("invalid {flag} value {value:?}; expected a positive integer"))?;
    if parsed == 0 {
        return Err(format!("{flag} must be at least 1"));
    }
    if parsed > ceiling {
        return Err(format!("{flag} exceeds the supported maximum of {ceiling}"));
    }
    Ok(parsed)
}

fn parse_limit_option(flag: &str, value: &str, limits: &mut LimitOptions) -> Result<(), String> {
    match flag {
        "--max-blocks" => {
            limits.max_blocks = parse_limit(value, flag, MAX_BLOCKS_OVERRIDE)?;
        }
        "--max-instructions" => {
            limits.max_instructions = parse_limit(value, flag, MAX_INSTRUCTIONS_OVERRIDE)?;
        }
        "--max-roots" => {
            limits.max_function_roots = parse_limit(value, flag, MAX_FUNCTION_ROOTS_OVERRIDE)?;
        }
        _ => return Err(format!("unknown analysis-limit option {flag:?}")),
    }
    Ok(())
}

/// Adds an actionable override hint to a limit failure without hiding the
/// analyzer's original diagnostic.
fn analysis_error_message(error: analysis::AnalysisError) -> String {
    match &error {
        analysis::AnalysisError::BlockLimitExceeded { limit } => {
            format!("{error}; raise --max-blocks above {limit} to analyze more")
        }
        analysis::AnalysisError::InstructionLimitExceeded { limit } => {
            format!("{error}; raise --max-instructions above {limit} to analyze more")
        }
        _ => error.to_string(),
    }
}

/// Parsed `analyze-elf` options.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
struct AnalyzeOptions {
    discover: bool,
    limits: LimitOptions,
}

fn parse_analyze_options(arguments: &[String]) -> Result<AnalyzeOptions, String> {
    let mut options = AnalyzeOptions::default();
    let mut index = 0;
    while index < arguments.len() {
        match arguments[index].as_str() {
            "--discover" => {
                options.discover = true;
                index += 1;
            }
            flag @ ("--max-blocks" | "--max-instructions" | "--max-roots") => {
                let value = arguments
                    .get(index + 1)
                    .ok_or_else(|| format!("{flag} requires a value"))?;
                parse_limit_option(flag, value, &mut options.limits)?;
                index += 2;
            }
            other => return Err(format!("unknown analyze-elf option {other:?}")),
        }
    }
    Ok(options)
}

/// Parsed `recompile-elf` options.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct RecompileOptions {
    discover: bool,
    translation_units: usize,
    native_build: bool,
    limits: LimitOptions,
}

impl Default for RecompileOptions {
    fn default() -> Self {
        Self {
            discover: false,
            translation_units: 1,
            native_build: false,
            limits: LimitOptions::default(),
        }
    }
}

fn parse_recompile_options(arguments: &[String]) -> Result<RecompileOptions, String> {
    let mut options = RecompileOptions::default();
    let mut index = 0;
    while index < arguments.len() {
        match arguments[index].as_str() {
            "--discover" => {
                options.discover = true;
                index += 1;
            }
            "--tu-count" => {
                let value = arguments
                    .get(index + 1)
                    .ok_or_else(|| "--tu-count requires a value".to_owned())?;
                options.translation_units = value.parse::<usize>().map_err(|_| {
                    format!("invalid translation-unit count {value:?}; expected a positive integer")
                })?;
                index += 2;
            }
            "--native-build" => {
                options.native_build = true;
                index += 1;
            }
            flag @ ("--max-blocks" | "--max-instructions" | "--max-roots") => {
                let value = arguments
                    .get(index + 1)
                    .ok_or_else(|| format!("{flag} requires a value"))?;
                parse_limit_option(flag, value, &mut options.limits)?;
                index += 2;
            }
            other => return Err(format!("unknown recompile-elf option {other:?}")),
        }
    }
    Ok(options)
}

/// Parsed `compile-cfg-fixture` options.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct FixtureOptions {
    translation_units: usize,
    native_build: bool,
}

impl Default for FixtureOptions {
    fn default() -> Self {
        Self {
            translation_units: 1,
            native_build: false,
        }
    }
}

fn parse_fixture_options(arguments: &[String]) -> Result<FixtureOptions, String> {
    let mut options = FixtureOptions::default();
    let mut index = 0;
    while index < arguments.len() {
        match arguments[index].as_str() {
            "--tu-count" => {
                let value = arguments
                    .get(index + 1)
                    .ok_or_else(|| "--tu-count requires a value".to_owned())?;
                options.translation_units = value.parse::<usize>().map_err(|_| {
                    format!("invalid translation-unit count {value:?}; expected a positive integer")
                })?;
                index += 2;
            }
            "--native-build" => {
                options.native_build = true;
                index += 1;
            }
            other => return Err(format!("unknown compile-cfg-fixture option {other:?}")),
        }
    }
    Ok(options)
}

/// Splits an output path into the stem and extension used for part files.
fn generated_output_parts(output: &Path) -> (String, String) {
    let stem = output
        .file_stem()
        .map(|value| value.to_string_lossy().into_owned())
        .unwrap_or_else(|| "output".to_owned());
    let extension = output
        .extension()
        .map(|value| value.to_string_lossy().into_owned())
        .unwrap_or_else(|| "cpp".to_owned());
    (stem, extension)
}

fn part_output_path(output: &Path, index: usize) -> PathBuf {
    let (stem, extension) = generated_output_parts(output);
    output.with_file_name(format!("{stem}.part{index:04}.{extension}"))
}

/// A file name suitable for recording in the build manifest.
fn output_file_name(path: &Path) -> String {
    path.file_name()
        .map(|value| value.to_string_lossy().into_owned())
        .unwrap_or_else(|| path.display().to_string())
}

/// Removes `<stem>.partNNNN.<ext>` files left by an earlier, larger split.
fn remove_stale_part_files(output: &Path) -> Result<(), String> {
    let directory = match output.parent() {
        Some(parent) if !parent.as_os_str().is_empty() => parent,
        _ => Path::new("."),
    };
    let (stem, extension) = generated_output_parts(output);
    let prefix = format!("{stem}.part");
    let suffix = format!(".{extension}");

    let entries = fs::read_dir(directory).map_err(|error| {
        format!(
            "cannot scan {} for stale generated parts: {error}",
            directory.display()
        )
    })?;
    for entry in entries {
        let entry = entry.map_err(|error| {
            format!(
                "cannot inspect {} for stale generated parts: {error}",
                directory.display()
            )
        })?;
        let name = entry.file_name();
        let name = name.to_string_lossy();
        let is_generated_part = name
            .strip_prefix(&prefix)
            .and_then(|rest| rest.strip_suffix(&suffix))
            .is_some_and(|digits| {
                digits.len() >= 4 && digits.bytes().all(|byte| byte.is_ascii_digit())
            });
        if is_generated_part {
            fs::remove_file(entry.path()).map_err(|error| {
                format!(
                    "cannot remove stale generated part {}: {error}",
                    entry.path().display()
                )
            })?;
        }
    }
    Ok(())
}

/// Writes `units[0]` to `output` and every remaining unit to a numbered part
/// file. Stale parts from an earlier run are removed first.
fn write_translation_units(output: &Path, units: &[String]) -> Result<Vec<PathBuf>, String> {
    remove_stale_part_files(output)?;
    fs::write(output, &units[0]).map_err(|error| {
        format!(
            "cannot write generated C++ to {}: {error}",
            output.display()
        )
    })?;

    let mut parts = Vec::with_capacity(units.len().saturating_sub(1));
    for (offset, source) in units.iter().enumerate().skip(1) {
        let path = part_output_path(output, offset);
        fs::write(&path, source)
            .map_err(|error| format!("cannot write generated part {}: {error}", path.display()))?;
        parts.push(path);
    }
    Ok(parts)
}

fn print_translation_unit_summary(parts: &[PathBuf]) {
    if parts.is_empty() {
        println!("  Translation units: 1 (single file)");
        return;
    }
    let first = parts[0].file_name().unwrap_or_default().to_string_lossy();
    let last = parts[parts.len() - 1]
        .file_name()
        .unwrap_or_default()
        .to_string_lossy();
    println!(
        "  Translation units: {} (1 dispatcher + {} block part(s))",
        parts.len() + 1,
        parts.len()
    );
    println!("  Part files: {first} .. {last}");
}

/// Writes a deterministic standalone CMake project beside the translation.
fn write_native_project(
    output: &Path,
    sources: &[String],
    ram_image: Option<&str>,
) -> Result<PathBuf, String> {
    let (stem, _) = generated_output_parts(output);
    let directory = output.with_file_name(format!("{stem}.native"));
    fs::create_dir_all(&directory).map_err(|error| {
        format!(
            "cannot create native build directory {}: {error}",
            directory.display()
        )
    })?;

    let project = native_build::render(&stem, sources, ram_image);
    fs::write(directory.join("CMakeLists.txt"), project.cmake_lists).map_err(|error| {
        format!(
            "cannot write native build CMakeLists.txt in {}: {error}",
            directory.display()
        )
    })?;
    fs::write(directory.join("probe_main.cpp"), project.probe_source).map_err(|error| {
        format!(
            "cannot write native build probe in {}: {error}",
            directory.display()
        )
    })?;
    Ok(directory)
}

fn default_library_root() -> Result<std::path::PathBuf, String> {
    if let Some(path) = env::var_os("REPS2_LIBRARY_DIR").filter(|value| !value.is_empty()) {
        return Ok(Path::new(&path).to_path_buf());
    }

    #[cfg(windows)]
    {
        let root = env::var_os("LOCALAPPDATA").ok_or_else(|| {
            "LOCALAPPDATA is unavailable; set REPS2_LIBRARY_DIR to choose a library folder"
                .to_owned()
        })?;
        Ok(Path::new(&root).join("REPS2").join("Library"))
    }

    #[cfg(target_os = "macos")]
    {
        let home = env::var_os("HOME").ok_or_else(|| {
            "HOME is unavailable; set REPS2_LIBRARY_DIR to choose a library folder".to_owned()
        })?;
        Ok(Path::new(&home)
            .join("Library")
            .join("Application Support")
            .join("REPS2")
            .join("Library"))
    }

    #[cfg(all(not(windows), not(target_os = "macos")))]
    {
        if let Some(root) = env::var_os("XDG_DATA_HOME").filter(|value| !value.is_empty()) {
            return Ok(Path::new(&root).join("reps2").join("Library"));
        }
        let home = env::var_os("HOME").ok_or_else(|| {
            "HOME is unavailable; set REPS2_LIBRARY_DIR to choose a library folder".to_owned()
        })?;
        Ok(Path::new(&home)
            .join(".local")
            .join("share")
            .join("reps2")
            .join("Library"))
    }
}

fn add_game(input: &Path, library_override: Option<&Path>) -> Result<(), String> {
    let library_root = match library_override {
        Some(path) => path.to_path_buf(),
        None => default_library_root()?,
    };
    let result = library::import_disc(input, &library_root)?;
    if result.already_imported {
        println!("Game is already present in the REPS2 library");
    } else {
        println!("Imported one PS2 game into the REPS2 library");
    }
    println!("  Title: {}", result.title);
    println!("  Game ID: {}", result.game_id);
    println!("  Game directory: {}", result.game_directory.display());
    println!("  Library root: {}", library_root.display());
    println!(
        "  Boot ELF: {}",
        result.game_directory.join("boot.elf").display()
    );
    println!(
        "  Generated C++: {}",
        if result.generated_cpp {
            "yes (experimental/partial)"
        } else {
            "not yet"
        }
    );
    println!(
        "  Initial EE RAM image: {}",
        if result.generated_ram_image {
            "yes"
        } else {
            "not yet"
        }
    );
    println!("  Playable: no — the PS2 runtime and title-stage validation are not complete");
    Ok(())
}

fn compile_cfg_fixture(output: &Path, options: FixtureOptions) -> Result<(), String> {
    use reps2_frontend::analysis::{self, AnalysisLimits, EntryPoint};
    use reps2_frontend::elf::LoadedSegment;

    fn segment(base: u32, words: &[u32]) -> LoadedSegment {
        let mut bytes = Vec::with_capacity(words.len() * 4);
        for word in words {
            bytes.extend_from_slice(&word.to_le_bytes());
        }
        let file_size = bytes.len();
        LoadedSegment {
            virtual_address: base,
            flags: 1,
            file_size,
            bytes,
        }
    }

    fn entry(address: u32, label: &str) -> EntryPoint {
        EntryPoint {
            address,
            labels: vec![label.to_owned()],
            is_elf_entry: address == 0x1000,
        }
    }

    let branch_and_jump = [
        0x1100_0004, // beq t0, zero, 0x1014
        0x2409_0007, // delay slot: t1 = 7
        0x240A_0005, // t2 = 5 (not-taken path)
        0x0800_0406, // j 0x1018
        0x240B_0009, // delay slot: t3 = 9
        0x240C_000B, // t4 = 11 (taken path)
        0x03E0_0008, // jr ra
        0x240D_000D, // return delay slot: t5 = 13
    ];
    let jal_and_return = [
        0x0C00_0804, // jal 0x2010
        0x240E_0011, // delay slot: t6 = 17
        0x0000_000C, // syscall: explicit unsupported stop after return
        0x0000_0000, // unreachable filler
        0x03E0_0008, // callee: jr ra
        0x240F_0013, // return delay slot: t7 = 19
    ];
    let branch_and_link = [
        (0x01 << 26) | (8 << 21) | (0x10 << 16) | 2, // bltzal t0, 0x300C
        0x2409_0017,                                 // delay slot: t1 = 23
        0x0000_000C,                                 // syscall on not-taken path
        0x0000_000C,                                 // syscall on taken path
    ];
    let branch_likely = [
        (0x14 << 26) | (8 << 21) | 4, // beql t0, zero, 0x3114
        0x2409_0007,                  // delay slot: t1 = 7 (taken only)
        0x240A_0005,                  // not-taken path: t2 = 5
        0x0800_0C48,                  // j 0x3120
        0x240F_0009,                  // jump delay slot: t7 = 9
        0x240B_000B,                  // taken path: t3 = 11
        0x0800_0C48,                  // j 0x3120
        0x0000_0000,                  // jump delay slot
        0x03E0_0008,                  // common return
        0x240D_000D,                  // return delay slot: t5 = 13
    ];
    let branch_likely_link = [
        (0x01 << 26) | (8 << 21) | (0x12 << 16) | 4, // bltzall t0, 0x3214
        0x2409_0007,                                 // delay slot: t1 = 7 (taken only)
        0x240A_0005,                                 // not-taken path: t2 = 5
        0x0800_0C88,                                 // j 0x3220
        0x240F_0009,                                 // jump delay slot: t7 = 9
        0x240B_000B,                                 // taken path: t3 = 11
        0x0800_0C88,                                 // j 0x3220
        0x0000_0000,                                 // jump delay slot
        0x03C0_0008,                                 // common return via fp
        0x240D_000D,                                 // return delay slot: t5 = 13
    ];
    let jalr_alias = [
        (8 << 21) | (8 << 11) | 0x09, // jalr t0, t0
        0x2409_001F,                  // delay slot: t1 = 31
    ];
    let memory_ops = [
        (0x2B << 26) | (8 << 21) | (9 << 16) | 0xFFFC, // sw t1, -4(t0)
        (0x20 << 26) | (8 << 21) | (10 << 16) | 0xFFFD, // lb t2, -3(t0)
        (0x24 << 26) | (8 << 21) | (11 << 16) | 0xFFFE, // lbu t3, -2(t0)
        (0x21 << 26) | (8 << 21) | (12 << 16) | 0xFFFE, // lh t4, -2(t0)
        (0x25 << 26) | (8 << 21) | (13 << 16) | 0xFFFE, // lhu t5, -2(t0)
        (0x23 << 26) | (8 << 21) | (14 << 16) | 0xFFFC, // lw t6, -4(t0)
        (0x27 << 26) | (8 << 21) | (15 << 16) | 0xFFFC, // lwu t7, -4(t0)
        (0x28 << 26) | (8 << 21) | (9 << 16),          // sb t1, 0(t0)
        (0x29 << 26) | (8 << 21) | (9 << 16) | 2,      // sh t1, 2(t0)
        (0x2B << 26) | (8 << 21) | (9 << 16) | 4,      // sw t1, 4(t0)
        (0x1E << 26) | (8 << 21) | (16 << 16),         // lq s0, 0(t0)
        (0x1F << 26) | (8 << 21) | (16 << 16) | 0x20,  // sq s0, 0x20(t0)
        (0x37 << 26) | (8 << 21) | (17 << 16) | 4,     // ld s1, 4(t0)
        (0x3F << 26) | (8 << 21) | (17 << 16) | 0x34,  // sd s1, 0x34(t0)
        0x03E0_0008,                                   // jr ra
        0x0000_0000,                                   // delay slot
    ];
    let unaligned_load = [
        (0x23 << 26) | (8 << 21) | (14 << 16), // lw t6, 0(t0)
        0x03E0_0008,                           // jr ra
        0x0000_0000,                           // delay slot
    ];
    let unaligned_doubleword = [
        (0x37 << 26) | (8 << 21) | (14 << 16), // ld t6, 0(t0)
        0x03E0_0008,                           // jr ra
        0x0000_0000,                           // delay slot
    ];
    let mut unaligned_word_merge = Vec::with_capacity(18);
    for lane in 0u32..4 {
        let offset = lane * 4 + lane;
        unaligned_word_merge.push((0x22u32 << 26) | (8 << 21) | ((10 + lane) << 16) | offset);
    }
    for lane in 0u32..4 {
        let offset = 0x10 + lane * 4 + lane;
        unaligned_word_merge.push((0x26u32 << 26) | (8 << 21) | ((14 + lane) << 16) | offset);
    }
    for lane in 0u32..4 {
        let offset = 0x20 + lane * 4 + lane;
        unaligned_word_merge.push((0x2Au32 << 26) | (8 << 21) | (9 << 16) | offset);
    }
    for lane in 0u32..4 {
        let offset = 0x30 + lane * 4 + lane;
        unaligned_word_merge.push((0x2Eu32 << 26) | (8 << 21) | (9 << 16) | offset);
    }
    unaligned_word_merge.extend([0x03E0_0008, 0x0000_0000]);

    let mut unaligned_doubleword_merge = Vec::with_capacity(34);
    for lane in 0u32..8 {
        let offset = lane * 8 + lane;
        unaligned_doubleword_merge.push((0x1Au32 << 26) | (8 << 21) | ((10 + lane) << 16) | offset);
    }
    for lane in 0u32..8 {
        let offset = 0x40 + lane * 8 + lane;
        unaligned_doubleword_merge.push((0x1Bu32 << 26) | (8 << 21) | ((18 + lane) << 16) | offset);
    }
    for lane in 0u32..8 {
        let offset = 0x80 + lane * 8 + lane;
        unaligned_doubleword_merge.push((0x2Cu32 << 26) | (8 << 21) | (9 << 16) | offset);
    }
    for lane in 0u32..8 {
        let offset = 0xC0 + lane * 8 + lane;
        unaligned_doubleword_merge.push((0x2Du32 << 26) | (8 << 21) | (9 << 16) | offset);
    }
    unaligned_doubleword_merge.extend([0x03E0_0008, 0x0000_0000]);

    let fpu_branch = [
        0x4601_0034, // c.lt.s f0, f1
        0x4501_0002, // bc1t 0x7010
        0x2409_0007, // delay slot: t1 = 7
        0x240A_0005, // not-taken path: t2 = 5
        0x03E0_0008, // common return
        0x240B_0009, // return delay slot: t3 = 9
    ];
    let fpu_branch_false = [
        0x4601_0030, // c.f.s f0, f1
        0x4500_0002, // bc1f 0x7110
        0x2409_0007, // delay slot: t1 = 7
        0x240A_0005, // fallthrough when the condition is set: t2 = 5
        0x03E0_0008, // common return
        0x240B_0009, // return delay slot: t3 = 9
    ];
    let fpu_branch_likely = [
        0x4601_0032, // c.eq.s f0, f1
        0x4502_0004, // bc1fl 0x7218
        0x2409_0007, // delay slot: t1 = 7 (taken only)
        0x240A_0005, // not-taken path: t2 = 5
        0x03E0_0008, // not-taken return
        0x240E_000D, // return delay slot: t6 = 13
        0x240C_0009, // taken path: t4 = 9
        0x03E0_0008, // taken return
        0x240D_000B, // return delay slot: t5 = 11
    ];
    let fpu_branch_likely_true = [
        0x4601_0036, // c.le.s f0, f1
        0x4503_0004, // bc1tl 0x7318
        0x2409_0007, // delay slot: t1 = 7 (taken only)
        0x240A_0005, // not-taken path: t2 = 5
        0x03E0_0008, // not-taken return
        0x240E_000D, // return delay slot: t6 = 13
        0x240C_0009, // taken path: t4 = 9
        0x03E0_0008, // taken return
        0x240D_000B, // return delay slot: t5 = 11
    ];
    let break_trap = [
        0x0000_004D, // break code 1: a guest trap, not an unknown encoding
    ];
    let eret_return = [
        0x4200_0018, // eret: restore PC from COP0.EPC and clear EXL
        0x0120_0008, // jr t1: dynamically leave the handler-return fixture
        0x2408_002A, // delay slot: t0 = 42
    ];

    let segments = [
        segment(0x1000, &branch_and_jump),
        segment(0x2000, &jal_and_return),
        segment(0x3000, &branch_and_link),
        segment(0x3100, &branch_likely),
        segment(0x3200, &branch_likely_link),
        segment(0x4000, &jalr_alias),
        segment(0x5000, &memory_ops),
        segment(0x5100, &unaligned_load),
        segment(0x5200, &unaligned_doubleword),
        segment(0x5300, &unaligned_word_merge),
        segment(0x5400, &unaligned_doubleword_merge),
        segment(0x7000, &fpu_branch),
        segment(0x7100, &fpu_branch_false),
        segment(0x7200, &fpu_branch_likely),
        segment(0x7300, &fpu_branch_likely_true),
        segment(0x8000, &break_trap),
        segment(0x9000, &eret_return),
    ];
    let entries = vec![
        entry(0x1000, "branch-fixture"),
        entry(0x2000, "call-fixture"),
        entry(0x3000, "branch-link-fixture"),
        entry(0x3100, "branch-likely-fixture"),
        entry(0x3200, "branch-likely-link-fixture"),
        entry(0x4000, "jalr-alias-fixture"),
        entry(0x5000, "ee-memory-fixture"),
        entry(0x5100, "unaligned-load-fixture"),
        entry(0x5200, "unaligned-doubleword-fixture"),
        entry(0x5300, "unaligned-word-merge-fixture"),
        entry(0x5400, "unaligned-doubleword-merge-fixture"),
        entry(0x7000, "fpu-branch-fixture"),
        entry(0x7100, "fpu-branch-false-fixture"),
        entry(0x7200, "fpu-branch-likely-fixture"),
        entry(0x7300, "fpu-branch-likely-true-fixture"),
        entry(0x8000, "break-trap-fixture"),
        entry(0x9000, "eret-fixture"),
        entry(0x9004, "eret-return-continuation-fixture"),
    ];
    let roots = [
        0x1000, 0x2000, 0x3000, 0x3100, 0x3200, 0x4000, 0x5000, 0x5100, 0x5200, 0x5300, 0x5400,
        0x7000, 0x7100, 0x7200, 0x7300, 0x8000, 0x9000, 0x9004,
    ];
    let graph = analysis::discover_cfg(&segments, entries, &roots, AnalysisLimits::default())
        .map_err(|error| error.to_string())?;
    let program = control_ir::ControlProgram::lift(&graph).map_err(|error| error.to_string())?;
    let units =
        control_codegen::emit_cpp_units_with_entry(&program, 0x1000, options.translation_units)
            .map_err(|error| error.to_string())?;
    let parts = write_translation_units(output, &units)?;
    println!(
        "Generated synthetic CFG fixture: {} block(s), {} guest instruction(s), {} unsupported stop(s) ({} guest trap(s))",
        program.blocks.len(),
        program.instruction_count(),
        program.unsupported_count(),
        program.trap_count()
    );
    print_translation_unit_summary(&parts);
    if options.native_build {
        let mut sources = Vec::with_capacity(parts.len() + 1);
        sources.push(output_file_name(output));
        sources.extend(parts.iter().map(|part| output_file_name(part)));
        let directory = write_native_project(output, &sources, None)?;
        println!("  Native build project: {}", directory.display());
    }
    Ok(())
}

fn compile_words(output: &Path, base_pc_text: &str, word_texts: &[String]) -> Result<(), String> {
    if word_texts.is_empty() {
        return Err("compile-words requires at least one instruction word".to_owned());
    }

    let base_pc = parse_hex(base_pc_text)?;
    if base_pc & 3 != 0 {
        return Err("base guest PC must be 4-byte aligned".to_owned());
    }

    let mut decoded = Vec::with_capacity(word_texts.len());
    for (index, word_text) in word_texts.iter().enumerate() {
        let offset = u32::try_from(index)
            .ok()
            .and_then(|value| value.checked_mul(4))
            .ok_or_else(|| "instruction stream is too large".to_owned())?;
        let pc = base_pc
            .checked_add(offset)
            .ok_or_else(|| "instruction stream guest PC overflows".to_owned())?;
        decoded.push(mips::decode(parse_hex(word_text)?, pc));
    }

    let program = ir::lift(&decoded).map_err(|error| error.to_string())?;
    let cpp = codegen::emit_cpp(&program).map_err(|error| error.to_string())?;
    fs::write(output, cpp).map_err(|error| format!("cannot write generated C++: {error}"))?;
    println!(
        "Generated {} supported instruction(s) to {}",
        program.instructions().len(),
        output.display()
    );
    Ok(())
}

fn usage() {
    println!("REPS2 - PlayStation 2 static recompilation frontend");
    println!();
    println!("Usage:");
    println!("  reps2 version");
    println!("  reps2 disc-info <disc-image.iso>");
    println!("  reps2 add-game <disc-image.iso> [--library <directory>]");
    println!("  reps2 extract-boot-elf <disc-image.iso> <output.elf>");
    println!("  reps2 elf-info <elf-file>");
    println!(
        "  reps2 analyze-elf <elf-file> [--discover] [--max-blocks N] [--max-instructions N] [--max-roots N]"
    );
    println!("  reps2 profile-elf <elf-file>");
    println!(
        "  reps2 recompile-elf <input-elf> <output.cpp> [--discover] [--tu-count N] [--native-build] [--max-blocks N] [--max-instructions N] [--max-roots N]"
    );
    println!("  reps2 decode <instruction-word-hex> [pc-hex]");
    println!("  reps2 compile-cfg-fixture <output.cpp> [--tu-count N] [--native-build]");
    println!("  reps2 compile-words <output.cpp> <base-pc-hex> <instruction-word-hex>...");
    println!();
    println!(
        "add-game imports one ISO into the default per-user REPS2 library; --library overrides the location."
    );
    println!(
        "The original ISO remains at its source path; generated artifacts are experimental and do not imply playability."
    );
    println!(
        "disc-info and extract-boot-elf read raw 2048-byte-sector ISO 9660 images without loading the whole disc into memory."
    );
    println!(
        "extract-boot-elf resolves SYSTEM.CNF BOOT2 (preferred) or BOOT and accepts only MIPS ET_EXEC images."
    );
    println!(
        "recompile-elf emits C++ plus a companion 32 MiB .ram.bin for eligible static EE ET_EXEC images."
    );
    println!(
        "It also writes <stem>.manifest.json: a deterministic record of the input SHA-256, analysis limits, program shape, and translation-unit layout."
    );
    println!(
        "--discover augments the ELF entry and STT_FUNC roots with static function discovery (JAL targets and ABI stack prologues) for stripped executables."
    );
    println!(
        "--tu-count N splits generated C++ into a dispatcher plus N block part files (<stem>.partNNNN.cpp); the default is 1."
    );
    println!(
        "--native-build writes a deterministic <stem>.native/ CMake project plus a host probe that compiles and runs the emitted units; pass -DREPS2_RUNTIME_INCLUDE=<repo>/runtime/include when configuring it."
    );
    println!(
        "--max-blocks/--max-instructions/--max-roots override the default analysis bounds (250000 blocks, 2000000 instructions, 500000 discovered roots). Overrides must be positive and stay below the CLI ceilings."
    );
    println!(
        "Allocated relocations, non-main-RAM PT_LOAD segments, and PS2 hardware services remain unsupported."
    );
}

fn run() -> Result<(), String> {
    let arguments: Vec<String> = env::args().collect();
    match arguments.get(1).map(String::as_str) {
        Some("version") if arguments.len() == 2 => {
            println!(
                "REPS2 {} (Rust frontend, experimental)",
                reps2_frontend::VERSION
            );
            Ok(())
        }
        Some("disc-info") if arguments.len() == 3 => disc_info(Path::new(&arguments[2])),
        Some("add-game") if arguments.len() == 3 => add_game(Path::new(&arguments[2]), None),
        Some("add-game") if arguments.len() == 5 && arguments[3] == "--library" => {
            add_game(Path::new(&arguments[2]), Some(Path::new(&arguments[4])))
        }
        Some("extract-boot-elf") if arguments.len() == 4 => {
            extract_boot_elf(Path::new(&arguments[2]), Path::new(&arguments[3]))
        }
        Some("elf-info") if arguments.len() == 3 => elf_info(Path::new(&arguments[2])),
        Some("analyze-elf") if arguments.len() >= 3 => {
            let options = parse_analyze_options(&arguments[3..])?;
            analyze_elf(Path::new(&arguments[2]), options)
        }
        Some("profile-elf") if arguments.len() == 3 => profile_elf(Path::new(&arguments[2])),
        Some("recompile-elf") if arguments.len() >= 4 => {
            let options = parse_recompile_options(&arguments[4..])?;
            compile_elf(Path::new(&arguments[2]), Path::new(&arguments[3]), options)
        }
        Some("compile-cfg-fixture") if arguments.len() >= 3 => {
            let options = parse_fixture_options(&arguments[3..])?;
            compile_cfg_fixture(Path::new(&arguments[2]), options)
        }
        Some("decode") if (3..=4).contains(&arguments.len()) => {
            decode_word(&arguments[2], arguments.get(3).map(String::as_str))
        }
        Some("compile-words") if arguments.len() >= 5 => {
            compile_words(Path::new(&arguments[2]), &arguments[3], &arguments[4..])
        }
        None => {
            usage();
            Ok(())
        }
        _ => {
            usage();
            Err("invalid command-line arguments".to_owned())
        }
    }
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("Error: {error}");
            ExitCode::from(1)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_to_a_single_translation_unit_without_discovery() {
        let options = parse_recompile_options(&[]).expect("default options");
        assert_eq!(options, RecompileOptions::default());
        assert_eq!(options.translation_units, 1);
        assert!(!options.discover);
        assert!(!options.native_build);
        assert_eq!(options.limits, LimitOptions::default());
    }

    #[test]
    fn parses_recompile_options_in_any_order() {
        let options = parse_recompile_options(&[
            "--tu-count".to_owned(),
            "4".to_owned(),
            "--discover".to_owned(),
            "--native-build".to_owned(),
            "--max-blocks".to_owned(),
            "300000".to_owned(),
            "--max-instructions".to_owned(),
            "2500000".to_owned(),
            "--max-roots".to_owned(),
            "600000".to_owned(),
        ])
        .expect("ordered options");
        assert_eq!(
            options,
            RecompileOptions {
                discover: true,
                translation_units: 4,
                native_build: true,
                limits: LimitOptions {
                    max_blocks: 300_000,
                    max_instructions: 2_500_000,
                    max_function_roots: 600_000,
                },
            }
        );
    }

    #[test]
    fn parses_analyze_options_and_overrides() {
        let options = parse_analyze_options(&[
            "--discover".to_owned(),
            "--max-blocks".to_owned(),
            "500000".to_owned(),
        ])
        .expect("analyze options");
        assert!(options.discover);
        assert_eq!(options.limits.max_blocks, 500_000);
        assert_eq!(
            options.limits.max_instructions,
            AnalysisLimits::default().max_instructions
        );
        assert_eq!(
            parse_analyze_options(&[]).expect("defaults"),
            AnalyzeOptions::default()
        );
    }

    #[test]
    fn rejects_invalid_analysis_limit_overrides() {
        for flag in ["--max-blocks", "--max-instructions", "--max-roots"] {
            assert!(parse_analyze_options(&[flag.to_owned()]).is_err(), "{flag}");
            assert!(
                parse_analyze_options(&[flag.to_owned(), "0".to_owned()]).is_err(),
                "{flag}"
            );
            assert!(
                parse_analyze_options(&[flag.to_owned(), "many".to_owned()]).is_err(),
                "{flag}"
            );
            assert!(
                parse_analyze_options(&[flag.to_owned(), "999999999999".to_owned()]).is_err(),
                "{flag}"
            );
        }
        assert!(parse_recompile_options(&["--max-blocks".to_owned()]).is_err());
        assert!(parse_recompile_options(&["--max-roots".to_owned(), "0".to_owned()]).is_err());
    }

    #[test]
    fn limit_options_preserve_unrelated_defaults() {
        let limits = LimitOptions {
            max_blocks: 42,
            max_instructions: 43,
            max_function_roots: 44,
        }
        .to_limits();
        let defaults = AnalysisLimits::default();
        assert_eq!(limits.max_blocks, 42);
        assert_eq!(limits.max_instructions, 43);
        assert_eq!(limits.max_function_roots, 44);
        assert_eq!(limits.max_loaded_memory, defaults.max_loaded_memory);
        assert_eq!(limits.max_aliased_roots, defaults.max_aliased_roots);
    }

    #[test]
    fn parses_fixture_options_and_native_build() {
        let options = parse_fixture_options(&[
            "--native-build".to_owned(),
            "--tu-count".to_owned(),
            "4".to_owned(),
        ])
        .expect("fixture options");
        assert_eq!(
            options,
            FixtureOptions {
                translation_units: 4,
                native_build: true,
            }
        );
        assert!(parse_fixture_options(&["--unknown".to_owned()]).is_err());
    }

    #[test]
    fn rejects_malformed_recompile_options() {
        assert!(parse_recompile_options(&["--tu-count".to_owned()]).is_err());
        assert!(parse_recompile_options(&["--tu-count".to_owned(), "many".to_owned()]).is_err());
        assert!(parse_recompile_options(&["--unknown".to_owned()]).is_err());
        assert!(parse_analyze_options(&["--unknown".to_owned()]).is_err());
    }

    #[test]
    fn names_part_files_beside_the_primary_output() {
        assert_eq!(
            part_output_path(Path::new("out/game.cpp"), 7),
            Path::new("out/game.part0007.cpp")
        );
        assert_eq!(
            part_output_path(Path::new("game"), 1),
            Path::new("game.part0001.cpp")
        );
    }
}
