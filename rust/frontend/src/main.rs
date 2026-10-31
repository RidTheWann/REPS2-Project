// SPDX-License-Identifier: GPL-3.0-or-later
use std::env;
use std::fs;
use std::path::Path;
use std::process::ExitCode;

use reps2_frontend::analysis::{self, AnalysisLimits};
use reps2_frontend::codegen;
use reps2_frontend::control_codegen;
use reps2_frontend::control_ir;
use reps2_frontend::elf::{self, ParseLimits};
use reps2_frontend::ir;
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

fn compile_elf(input: &Path, output: &Path) -> Result<(), String> {
    let bytes = read_input(input)?;
    let image = elf::parse(&bytes, ParseLimits::default()).map_err(|error| error.to_string())?;
    if !image.is_mips() {
        return Err("ELF machine is not EM_MIPS; refusing PS2 EE compilation".to_owned());
    }

    let graph = analysis::discover_elf(&image, &bytes, AnalysisLimits::default())
        .map_err(|error| error.to_string())?;
    let program = control_ir::ControlProgram::lift(&graph)
        .map_err(|error| error.to_string())?;
    let cpp = control_codegen::emit_cpp(&program)
        .map_err(|error| error.to_string())?;
    fs::write(output, cpp).map_err(|error| format!("cannot write generated C++: {error}"))?;

    println!("Generated CFG-aware C++20 translation");
    println!("  Input: {}", input.display());
    println!("  Output: {}", output.display());
    println!("  Entry points: {}", program.entry_points.len());
    println!("  Basic blocks: {}", program.blocks.len());
    println!("  Guest instructions represented: {}", program.instruction_count());
    println!("  Explicit unsupported stops: {}", program.unsupported_count());
    println!("  Unresolved indirect targets: {}", graph.unresolved_indirect_jumps.len());
    println!("  External/unmapped targets: {}", graph.external_targets.len());
    println!("  Runtime entry: reps2::generated::execute_cfg(EeState&, instruction_budget)");
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
    let segments = [
        segment(0x1000, &branch_and_jump),
        segment(0x2000, &jal_and_return),
        segment(0x3000, &branch_and_link),
        segment(0x4000, &jalr_alias),
    ];
    let entries = vec![
        entry(0x1000, "branch-fixture"),
        entry(0x2000, "call-fixture"),
        entry(0x3000, "branch-link-fixture"),
        entry(0x4000, "jalr-alias-fixture"),
    ];
    let roots = [0x1000, 0x2000, 0x3000, 0x4000];
    let graph = analysis::discover_cfg(&segments, entries, &roots, AnalysisLimits::default())
        .map_err(|error| error.to_string())?;
    let program = control_ir::ControlProgram::lift(&graph)
        .map_err(|error| error.to_string())?;
    let cpp = control_codegen::emit_cpp(&program)
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
    println!("  reps2 elf-info <elf-file>");
    println!("  reps2 analyze-elf <elf-file>");
    println!("  reps2 recompile-elf <input-elf> <output.cpp>");
    println!("  reps2 decode <instruction-word-hex> [pc-hex]");
    println!("  reps2 compile-cfg-fixture <output.cpp>");
    println!("  reps2 compile-words <output.cpp> <base-pc-hex> <instruction-word-hex>...");
    println!();
    println!("recompile-elf lowers the supported CFG/scalar subset to a bounded C++20 dispatcher.");
    println!("Memory, full R5900 semantics, branch-likely instructions and PS2 runtime services remain unsupported.");
}

fn run() -> Result<(), String> {
    let arguments: Vec<String> = env::args().collect();
    match arguments.get(1).map(String::as_str) {
        Some("version") if arguments.len() == 2 => {
            println!("REPS2 {} (Rust frontend, experimental)", reps2_frontend::VERSION);
            Ok(())
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
