// SPDX-License-Identifier: GPL-3.0-or-later
use std::env;
use std::fs;
use std::path::Path;
use std::process::ExitCode;

use reps2_frontend::elf::{self, ParseLimits};
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
        mips::Opcode::Bne | mips::Opcode::Blez | mips::Opcode::Bgtz) {
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

fn usage() {
    println!("REPS2 - PlayStation 2 static recompilation frontend");
    println!();
    println!("Usage:");
    println!("  reps2 version");
    println!("  reps2 elf-info <elf-file>");
    println!("  reps2 decode <instruction-word-hex> [pc-hex]");
    println!();
    println!("This is an experimental frontend; complete ELF recompilation and PS2 runtime are not implemented yet.");
}

fn run() -> Result<(), String> {
    let arguments: Vec<String> = env::args().collect();
    match arguments.get(1).map(String::as_str) {
        Some("version") if arguments.len() == 2 => {
            println!("REPS2 {} (Rust frontend, experimental)", reps2_frontend::VERSION);
            Ok(())
        }
        Some("elf-info") if arguments.len() == 3 => elf_info(Path::new(&arguments[2])),
        Some("decode") if (3..=4).contains(&arguments.len()) => {
            decode_word(&arguments[2], arguments.get(3).map(String::as_str))
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
