// SPDX-License-Identifier: GPL-3.0-or-later
use std::env;
use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::Path;
use std::process::ExitCode;

use reps2_frontend::analysis::{self, AnalysisLimits};
use reps2_frontend::codegen;
use reps2_frontend::control_codegen;
use reps2_frontend::control_ir;
use reps2_frontend::elf::{self, ParseLimits};
use reps2_frontend::ir;
use reps2_frontend::iso9660::{self, Iso9660};
use reps2_frontend::mips;

const MAX_INPUT_SIZE: u64 = 512 * 1024 * 1024;

fn parse_hex(value: &str) -> Result<u32, String> {
    let value = value.strip_prefix("0x").or_else(|| value.strip_prefix("0X")).unwrap_or(value);
    if value.is_empty() {
        return Err("expected a hexadecimal 32-bit value".to_owned());
    }
    u32::from_str_radix(value, 16).map_err(|_| "invalid hexadecimal 32-bit value".to_owned())
}

fn read_input(path: &Path) -> Result<Vec<u8>, String> {
    let metadata = fs::metadata(path).map_err(|error| format!("cannot inspect input file: {error}"))?;
    if metadata.len() > MAX_INPUT_SIZE {
        return Err("input file exceeds the 512 MiB safety limit".to_owned());
    }
    fs::read(path).map_err(|error| format!("cannot read input file: {error}"))
}

fn elf_info(path: &Path) -> Result<(), String> {
    let bytes = read_input(path)?;
    let image = elf::parse(&bytes, ParseLimits::default()).map_err(|error| error.to_string())?;

    println!("ELF32 little-endian image");
    println!("  Type: {}{}", image.file_type, if image.is_executable() { " (executable)" } else { "" });
    println!("  Machine: {}{}", image.machine, if image.is_mips() { " (MIPS)" } else { " (not MIPS)" });
    println!("  Entry point: 0x{:08X}", image.entry_point);
    println!("  Program headers: {}", image.program_headers.len());
    println!("  Section headers: {}", image.section_headers.len());
    println!("  PT_LOAD segments: {}", image.loadable_segment_count());
    println!("  Symbols: {}", image.symbols.len());
    println!("  Relocations: {}", image.relocations.len());

    for (index, section) in image.section_headers.iter().enumerate() {
        println!("  [{index:>3}] {:<20} type={:<3} addr=0x{:08X} size=0x{:X}",
            section.name, section.kind, section.virtual_address, section.size);
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
    println!("0x{word:08X}  {}  rs={} rt={} rd={}",
        instruction.opcode.mnemonic(), instruction.rs, instruction.rt, instruction.rd);
    if instruction.has_delay_slot {
        print!("  [delay-slot]");
    }
    if matches!(instruction.opcode,
        mips::Opcode::J | mips::Opcode::Jal | mips::Opcode::Beq |
        mips::Opcode::Bne | mips::Opcode::Blez | mips::Opcode::Bgtz |
        mips::Opcode::Bltz | mips::Opcode::Bgez |
        mips::Opcode::Bltzal | mips::Opcode::Bgezal) {
        print!("  target=0x{:08X}", instruction.target);
    }
    if matches!(instruction.opcode,
        mips::Opcode::Addi | mips::Opcode::Addiu | mips::Opcode::Slti |
        mips::Opcode::Sltiu | mips::Opcode::Lb | mips::Opcode::Lh |
        mips::Opcode::Lw | mips::Opcode::Lbu | mips::Opcode::Lhu |
        mips::Opcode::Lwu | mips::Opcode::Ld | mips::Opcode::Sd |
        mips::Opcode::Lwl | mips::Opcode::Lwr | mips::Opcode::Swl | mips::Opcode::Swr |
        mips::Opcode::Ldl | mips::Opcode::Ldr | mips::Opcode::Sdl | mips::Opcode::Sdr |
        mips::Opcode::Sb | mips::Opcode::Sh | mips::Opcode::Sw) {
        print!("  imm={}", instruction.signed_immediate());
    }
    println!();
    if !instruction.is_supported() {
        return Err("instruction is outside the currently implemented decoder subset".to_owned());
    }
    Ok(())
}

fn analyze_elf(path: &Path) -> Result<(), String> {
    let bytes = read_input(path)?;
    let image = elf::parse(&bytes, ParseLimits::default()).map_err(|error| error.to_string())?;
    if !image.is_mips() {
        return Err("ELF machine is not EM_MIPS; refusing PS2 EE analysis".to_owned());
    }

    let graph = analysis::discover_elf(&image, &bytes, AnalysisLimits::default())
        .map_err(|error| error.to_string())?;
    let edges = graph.edges();

    println!("REPS2 reachable executable analysis");
    println!("  Entry roots: {}", graph.entry_points.len());
    println!("  Basic blocks: {}", graph.blocks.len());
    println!("  Decoded instructions: {}", graph.decoded_instruction_count);
    println!("  Static edges: {}", edges.len());
    println!("  Unsupported blocks: {}", graph.unsupported_block_count());
    println!("  Unresolved indirect jumps: {}", graph.unresolved_indirect_jumps.len());
    println!("  External/unmapped targets: {}", graph.external_targets.len());

    for entry in &graph.entry_points {
        let role = if entry.is_elf_entry { "ELF entry" } else { "function" };
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

fn open_iso(path: &Path) -> Result<Iso9660<File>, String> {
    let file = File::open(path)
        .map_err(|error| format!("cannot open read-only disc image {}: {error}", path.display()))?;
    Iso9660::open(file).map_err(|error| error.to_string())
}

fn disc_info(path: &Path) -> Result<(), String> {
    let mut iso = open_iso(path)?;
    let info = iso.volume_info().clone();

    println!("REPS2 read-only ISO 9660 inspection");
    println!("  Image: {}", path.display());
    println!("  Volume identifier: {}", info.volume_identifier);
    println!("  Logical block size: {} bytes", info.logical_block_size);
    println!("  Declared volume: {} blocks ({} bytes)", info.volume_space_blocks, info.volume_bytes);
    println!("  Image size: {} bytes", info.image_bytes);
    println!("  Root directory:");

    let entries = iso.root_entries().map_err(|error| error.to_string())?;
    for entry in entries.iter().take(128) {
        let kind = if entry.is_directory() { "DIR " } else { "FILE" };
        println!("    [{kind}] {:<24} {} byte(s)", entry.name, entry.size_bytes);
    }
    if entries.len() > 128 {
        println!("    ... {} additional root entr(y/ies)", entries.len() - 128);
    }

    match iso.boot_executable_path().map_err(|error| error.to_string())? {
        Some(boot_path) => println!("  SYSTEM.CNF boot executable: {boot_path}"),
        None => println!("  SYSTEM.CNF: not present in the ISO root"),
    }

    println!("  Mode: read-only; no disc sectors are modified");
    Ok(())
}

fn extract_boot_elf(input: &Path, output: &Path) -> Result<(), String> {
    if output.exists() {
        let input_canonical = input.canonicalize()
            .map_err(|error| format!("cannot resolve disc image path: {error}"))?;
        let output_canonical = output.canonicalize()
            .map_err(|error| format!("cannot resolve existing output path: {error}"))?;
        if input_canonical == output_canonical {
            return Err("output path resolves to the input ISO; refusing to overwrite the disc image".to_owned());
        }
        return Err(format!("output already exists; refusing to overwrite: {}", output.display()));
    }

    let mut iso = open_iso(input)?;
    let boot_path = iso.boot_executable_path()
        .map_err(|error| error.to_string())?
        .ok_or_else(|| "ISO root does not contain SYSTEM.CNF with a BOOT2 or BOOT entry".to_owned())?;

    let executable = iso.read_file(&boot_path, iso9660::MAX_BOOT_EXECUTABLE_BYTES)
        .map_err(|error| format!("cannot extract SYSTEM.CNF boot target {boot_path}: {error}"))?;
    let image = elf::parse(&executable, ParseLimits::default())
        .map_err(|error| format!("SYSTEM.CNF target {boot_path} is not a valid supported ELF32 image: {error}"))?;
    if !image.is_mips() {
        return Err(format!("SYSTEM.CNF target {boot_path} is not EM_MIPS; refusing extraction as a PS2 EE boot ELF"));
    }
    if !image.is_executable() {
        return Err(format!("SYSTEM.CNF target {boot_path} is not ET_EXEC; only static executable boot images are supported"));
    }

    let mut destination = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(output)
        .map_err(|error| format!("cannot create new output file {}: {error}", output.display()))?;
    if let Err(error) = destination.write_all(&executable).and_then(|()| destination.sync_all()) {
        drop(destination);
        let _ = fs::remove_file(output);
        return Err(format!("cannot write extracted ELF to {}: {error}", output.display()));
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

fn compile_elf(input: &Path, output: &Path) -> Result<(), String> {
    let bytes = read_input(input)?;
    let image = elf::parse(&bytes, ParseLimits::default()).map_err(|error| error.to_string())?;
    if !image.is_mips() {
        return Err("ELF machine is not EM_MIPS; refusing PS2 EE compilation".to_owned());
    }

    let ram_image = elf::build_main_ram_image(&bytes, &image)
        .map_err(|error| format!("cannot construct initial EE main-RAM image: {error}"))?;
    let graph = analysis::discover_elf(&image, &bytes, AnalysisLimits::default())
        .map_err(|error| error.to_string())?;
    let program = control_ir::ControlProgram::lift(&graph)
        .map_err(|error| error.to_string())?;
    let cpp = control_codegen::emit_cpp_with_entry(&program, image.entry_point)
        .map_err(|error| error.to_string())?;
    let ram_output = output.with_extension("ram.bin");
    fs::write(&ram_output, &ram_image.bytes)
        .map_err(|error| format!("cannot write initial EE RAM image: {error}"))?;
    fs::write(output, cpp).map_err(|error| format!("cannot write generated C++: {error}"))?;

    println!("Generated CFG-aware C++20 translation and initial EE RAM image");
    println!("  Input: {}", input.display());
    println!("  C++ output: {}", output.display());
    println!("  RAM image: {}", ram_output.display());
    println!("  RAM image bytes: {}", ram_image.bytes.len());
    println!("  ELF entry point: 0x{:08X}", image.entry_point);
    println!("  PT_LOAD segments placed: {}", ram_image.segments.len());
    println!("  Entry points: {}", program.entry_points.len());
    println!("  Basic blocks: {}", program.blocks.len());
    println!("  Guest instructions represented: {}", program.instruction_count());
    println!("  Explicit unsupported stops: {}", program.unsupported_count());
    println!("  Unresolved indirect targets: {}", graph.unresolved_indirect_jumps.len());
    println!("  External/unmapped targets: {}", graph.external_targets.len());
    println!("  Runtime entry: reps2::generated::initialize_ee_state(EeState&, ram_image)");
    println!("  Execution entry: reps2::generated::execute_cfg(EeState&, instruction_budget)");
    Ok(())
}

fn compile_cfg_fixture(output: &Path) -> Result<(), String> {
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
        0x2409_0017, // delay slot: t1 = 23
        0x0000_000C, // syscall on not-taken path
        0x0000_000C, // syscall on taken path
    ];
    let jalr_alias = [
        (8 << 21) | (8 << 11) | 0x09, // jalr t0, t0
        0x2409_001F, // delay slot: t1 = 31
    ];
    let memory_ops = [
        (0x2B << 26) | (8 << 21) | (9 << 16) | 0xFFFC, // sw t1, -4(t0)
        (0x20 << 26) | (8 << 21) | (10 << 16) | 0xFFFD, // lb t2, -3(t0)
        (0x24 << 26) | (8 << 21) | (11 << 16) | 0xFFFE, // lbu t3, -2(t0)
        (0x21 << 26) | (8 << 21) | (12 << 16) | 0xFFFE, // lh t4, -2(t0)
        (0x25 << 26) | (8 << 21) | (13 << 16) | 0xFFFE, // lhu t5, -2(t0)
        (0x23 << 26) | (8 << 21) | (14 << 16) | 0xFFFC, // lw t6, -4(t0)
        (0x27 << 26) | (8 << 21) | (15 << 16) | 0xFFFC, // lwu t7, -4(t0)
        (0x28 << 26) | (8 << 21) | (9 << 16), // sb t1, 0(t0)
        (0x29 << 26) | (8 << 21) | (9 << 16) | 2, // sh t1, 2(t0)
        (0x2B << 26) | (8 << 21) | (9 << 16) | 4, // sw t1, 4(t0)
        (0x1E << 26) | (8 << 21) | (16 << 16), // lq s0, 0(t0)
        (0x1F << 26) | (8 << 21) | (16 << 16) | 0x20, // sq s0, 0x20(t0)
        (0x37 << 26) | (8 << 21) | (17 << 16) | 4, // ld s1, 4(t0)
        (0x3F << 26) | (8 << 21) | (17 << 16) | 0x34, // sd s1, 0x34(t0)
        0x03E0_0008, // jr ra
        0x0000_0000, // delay slot
    ];
    let unaligned_load = [
        (0x23 << 26) | (8 << 21) | (14 << 16), // lw t6, 0(t0)
        0x03E0_0008, // jr ra
        0x0000_0000, // delay slot
    ];
    let unaligned_doubleword = [
        (0x37 << 26) | (8 << 21) | (14 << 16), // ld t6, 0(t0)
        0x03E0_0008, // jr ra
        0x0000_0000, // delay slot
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

    let segments = [
        segment(0x1000, &branch_and_jump),
        segment(0x2000, &jal_and_return),
        segment(0x3000, &branch_and_link),
        segment(0x4000, &jalr_alias),
        segment(0x5000, &memory_ops),
        segment(0x5100, &unaligned_load),
        segment(0x5200, &unaligned_doubleword),
        segment(0x5300, &unaligned_word_merge),
        segment(0x5400, &unaligned_doubleword_merge),
    ];
    let entries = vec![
        entry(0x1000, "branch-fixture"),
        entry(0x2000, "call-fixture"),
        entry(0x3000, "branch-link-fixture"),
        entry(0x4000, "jalr-alias-fixture"),
        entry(0x5000, "ee-memory-fixture"),
        entry(0x5100, "unaligned-load-fixture"),
        entry(0x5200, "unaligned-doubleword-fixture"),
        entry(0x5300, "unaligned-word-merge-fixture"),
        entry(0x5400, "unaligned-doubleword-merge-fixture"),
    ];
    let roots = [0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x5100, 0x5200, 0x5300, 0x5400];
    let graph = analysis::discover_cfg(&segments, entries, &roots, AnalysisLimits::default())
        .map_err(|error| error.to_string())?;
    let program = control_ir::ControlProgram::lift(&graph)
        .map_err(|error| error.to_string())?;
    let cpp = control_codegen::emit_cpp_with_entry(&program, 0x1000)
        .map_err(|error| error.to_string())?;
    fs::write(output, cpp).map_err(|error| format!("cannot write generated C++ fixture: {error}"))?;
    println!(
        "Generated synthetic CFG fixture: {} block(s), {} guest instruction(s), {} unsupported stop(s)",
        program.blocks.len(),
        program.instruction_count(),
        program.unsupported_count()
    );
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
        let pc = base_pc.checked_add(offset)
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
    println!("  reps2 extract-boot-elf <disc-image.iso> <output.elf>");
    println!("  reps2 elf-info <elf-file>");
    println!("  reps2 analyze-elf <elf-file>");
    println!("  reps2 recompile-elf <input-elf> <output.cpp>");
    println!("  reps2 decode <instruction-word-hex> [pc-hex]");
    println!("  reps2 compile-cfg-fixture <output.cpp>");
    println!("  reps2 compile-words <output.cpp> <base-pc-hex> <instruction-word-hex>...");
    println!();
    println!("disc-info and extract-boot-elf read raw 2048-byte-sector ISO 9660 images without loading the whole disc into memory.");
    println!("extract-boot-elf resolves SYSTEM.CNF BOOT2 (preferred) or BOOT and accepts only MIPS ET_EXEC images.");
    println!("recompile-elf emits C++ plus a companion 32 MiB .ram.bin for eligible static EE ET_EXEC images.");
    println!("Allocated relocations, non-main-RAM PT_LOAD segments, branch-likely and PS2 hardware services remain unsupported.");
}

fn run() -> Result<(), String> {
    let arguments: Vec<String> = env::args().collect();
    match arguments.get(1).map(String::as_str) {
        Some("version") if arguments.len() == 2 => {
            println!("REPS2 {} (Rust frontend, experimental)", reps2_frontend::VERSION);
            Ok(())
        }
        Some("disc-info") if arguments.len() == 3 => disc_info(Path::new(&arguments[2])),
        Some("extract-boot-elf") if arguments.len() == 4 => {
            extract_boot_elf(Path::new(&arguments[2]), Path::new(&arguments[3]))
        }
        Some("elf-info") if arguments.len() == 3 => elf_info(Path::new(&arguments[2])),
        Some("analyze-elf") if arguments.len() == 3 => analyze_elf(Path::new(&arguments[2])),
        Some("recompile-elf") if arguments.len() == 4 => {
            compile_elf(Path::new(&arguments[2]), Path::new(&arguments[3]))
        }
        Some("compile-cfg-fixture") if arguments.len() == 3 => {
            compile_cfg_fixture(Path::new(&arguments[2]))
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
