//! Defensive ELF32 little-endian parser for PS2 executable research.

use std::{fmt, ops::Range};

pub const EM_MIPS: u16 = 8;
pub const ET_NONE: u16 = 0;
pub const ET_REL: u16 = 1;
pub const ET_EXEC: u16 = 2;
pub const ET_DYN: u16 = 3;
pub const ET_CORE: u16 = 4;
pub const PT_LOAD: u32 = 1;
pub const PT_MIPS_REGINFO: u32 = 0x7000_0000;
pub const PT_MIPS_ABIFLAGS: u32 = 0x7000_0003;
pub const SHF_WRITE: u32 = 0x1;
pub const SHF_ALLOC: u32 = 0x2;
pub const SHF_EXECINSTR: u32 = 0x4;
pub const PF_R: u32 = 0x4;
pub const PF_W: u32 = 0x2;
pub const PF_X: u32 = 0x1;
pub const EE_MAIN_RAM_SIZE: usize = 32 * 1024 * 1024;
pub const SHT_NULL: u32 = 0;
pub const SHT_SYMTAB: u32 = 2;
pub const SHT_PROGBITS: u32 = 1;
pub const SHT_STRTAB: u32 = 3;
pub const SHT_RELA: u32 = 4;
pub const SHT_NOBITS: u32 = 8;
pub const SHT_REL: u32 = 9;
pub const SHT_DYNSYM: u32 = 11;
pub const SHT_SYMTAB_SHNDX: u32 = 18;
pub const SHT_MIPS_REGINFO: u32 = 0x7000_0006;
pub const SHT_MIPS_ABIFLAGS: u32 = 0x7000_002A;
pub const SHT_MIPS_OPTIONS: u32 = 0x7000_000D;

const ELF_HEADER_SIZE: usize = 52;
const PROGRAM_HEADER_SIZE: usize = 32;
const SECTION_HEADER_SIZE: usize = 40;
const SYMBOL_SIZE: usize = 16;
const REL_SIZE: usize = 8;
const MIPS_REGINFO_SIZE: usize = 24;
const MIPS_ABIFLAGS_V0_SIZE: usize = 24;
const MIPS_OPTIONS_HEADER_SIZE: usize = 8;
const MIPS_OPTION_REGINFO_SIZE: usize = 24;
const ODK_NULL: u8 = 0;
const ODK_REGINFO: u8 = 1;
const ODK_EXCEPTIONS: u8 = 2;
const ODK_PAD: u8 = 3;
const ODK_HWPATCH: u8 = 4;
const ODK_FILL: u8 = 5;
const ODK_TAGS: u8 = 6;
const ODK_HWAND: u8 = 7;
const ODK_HWOR: u8 = 8;
const ODK_GP_GROUP: u8 = 9;
const ODK_IDENT: u8 = 10;
const ODK_PAGESIZE: u8 = 11;
const MIPS_AFL_FLAGS1_ODDSPREG: u32 = 1;
const STB_LOCAL: u8 = 0;
const SHN_LORESERVE: u16 = 0xFF00;
const SHN_XINDEX: u16 = 0xFFFF;
const PN_XNUM: u16 = 0xFFFF;
const ET_LOOS: u16 = 0xFE00;
const ET_HIOS: u16 = 0xFEFF;
const ET_LOPROC: u16 = 0xFF00;
const ET_HIPROC: u16 = 0xFFFF;
const RELA_SIZE: usize = 12;

// MIPS ELF e_flags fields (processor-specific).
const EF_MIPS_NOREORDER: u32 = 0x0000_0001;
const EF_MIPS_PIC: u32 = 0x0000_0002;
const EF_MIPS_CPIC: u32 = 0x0000_0004;
const EF_MIPS_XGOT: u32 = 0x0000_0008;
const EF_MIPS_64BIT_WHIRL: u32 = 0x0000_0010;
const EF_MIPS_ABI2: u32 = 0x0000_0020;
const EF_MIPS_ABI_ON32: u32 = 0x0000_0040;
const EF_MIPS_OPTIONS_FIRST: u32 = 0x0000_0080;
const EF_MIPS_32BITMODE: u32 = 0x0000_0100;
const EF_MIPS_FP64: u32 = 0x0000_0200;
const EF_MIPS_NAN2008: u32 = 0x0000_0400;
const EF_MIPS_FEATURE_MASK: u32 = 0x0000_07FF;
const EF_MIPS_ABI_MASK: u32 = 0x0000_F000;
const EF_MIPS_CPU_MASK: u32 = 0x00FF_0000;
const EF_MIPS_ARCH_MASK: u32 = 0xF000_0000;

/// Limits untrusted ELF-derived allocations and table processing.
#[derive(Clone, Copy, Debug)]
pub struct ParseLimits {
    pub max_string_table_bytes: usize,
    pub max_symbols: usize,
    pub max_relocations: usize,
    pub max_sections: usize,
    pub max_program_headers: usize,
    pub max_mips_option_descriptors: usize,
}

impl Default for ParseLimits {
    fn default() -> Self {
        Self {
            max_string_table_bytes: 64 * 1024 * 1024,
            max_symbols: 1_000_000,
            max_relocations: 4_000_000,
            max_sections: 100_000,
            max_program_headers: 100_000,
            max_mips_option_descriptors: 100_000,
        }
    }
}

/// A bounded parsing or validation error.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Error {
    message: String,
}

impl Error {
    fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for Error {}

/// ELF program-header metadata.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProgramHeader {
    pub kind: u32,
    pub offset: u32,
    pub virtual_address: u32,
    pub physical_address: u32,
    pub file_size: u32,
    pub memory_size: u32,
    pub flags: u32,
    pub alignment: u32,
}

/// ELF section-header metadata with a resolved section name.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SectionHeader {
    pub name: String,
    pub name_offset: u32,
    pub kind: u32,
    pub flags: u32,
    pub virtual_address: u32,
    pub offset: u32,
    pub size: u32,
    pub link: u32,
    pub info: u32,
    pub alignment: u32,
    pub entry_size: u32,
}

/// A symbol from SHT_SYMTAB or SHT_DYNSYM.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Symbol {
    pub name: String,
    pub value: u32,
    pub size: u32,
    pub table_section_index: usize,
    pub table_index: u32,
    pub section_index: u32,
    pub binding: u8,
    pub kind: u8,
    pub visibility: u8,
}

/// A REL or RELA relocation entry. R5900-specific relocation application is
/// deliberately separate from ELF parsing.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Relocation {
    pub offset: u32,
    pub symbol_index: u32,
    pub relocation_type: u8,
    pub addend: Option<i32>,
    pub relocation_section_index: usize,
    pub target_section_index: usize,
    pub symbol_table_section_index: usize,
}

/// MIPS ELF register-use metadata from one SHT_MIPS_REGINFO section.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MipsRegInfo {
    pub section_index: usize,
    pub gpr_mask: u32,
    pub coprocessor_masks: [u32; 4],
    pub gp_value: i32,
}

/// MIPS register-use metadata decoded from a PT_MIPS_REGINFO program header.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MipsRegInfoSegment {
    pub program_header_index: usize,
    pub file_offset: u32,
    pub virtual_address: u32,
    pub gpr_mask: u32,
    pub coprocessor_masks: [u32; 4],
    pub gp_value: i32,
}

/// Register metadata carried by an ODK_REGINFO SHT_MIPS_OPTIONS descriptor.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MipsOptionRegInfo {
    pub gpr_mask: u32,
    pub coprocessor_masks: [u32; 4],
    pub gp_value: i32,
}

/// One variable-length descriptor from a SHT_MIPS_OPTIONS section.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MipsOptionDescriptor {
    pub kind: u8,
    pub size: u8,
    /// Zero means global options; nonzero values are raw section indices.
    pub section_index: u16,
    pub info: u32,
    /// Number of bytes following the fixed eight-byte descriptor header.
    pub payload_size: u8,
    /// Present only for an ODK_REGINFO descriptor in ELF32.
    pub register_info: Option<MipsOptionRegInfo>,
}

impl MipsOptionDescriptor {
    #[must_use]
    pub const fn kind_name(&self) -> &'static str {
        match self.kind {
            ODK_NULL => "NULL",
            ODK_REGINFO => "REGINFO",
            ODK_EXCEPTIONS => "EXCEPTIONS",
            ODK_PAD => "PAD",
            ODK_HWPATCH => "HWPATCH",
            ODK_FILL => "FILL",
            ODK_TAGS => "TAGS",
            ODK_HWAND => "HWAND",
            ODK_HWOR => "HWOR",
            ODK_GP_GROUP => "GP_GROUP",
            ODK_IDENT => "IDENT",
            ODK_PAGESIZE => "PAGESIZE",
            _ => "unknown",
        }
    }
}

/// Decoded descriptor list from one SHT_MIPS_OPTIONS section.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MipsOptionsSection {
    pub section_index: usize,
    pub descriptors: Vec<MipsOptionDescriptor>,
}

/// Decoded version-zero payload from a SHT_MIPS_ABIFLAGS section.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MipsAbiFlagsV0 {
    pub isa_level: u8,
    pub isa_revision: u8,
    pub gpr_size: u8,
    pub cpr1_size: u8,
    pub cpr2_size: u8,
    pub fp_abi: u8,
    pub isa_extension: u32,
    pub ases: u32,
    pub flags1: u32,
    pub flags2: u32,
}

impl MipsAbiFlagsV0 {
    #[must_use]
    pub const fn isa_level_name(self) -> &'static str {
        match self.isa_level {
            1 => "MIPS I",
            2 => "MIPS II",
            3 => "MIPS III",
            4 => "MIPS IV",
            5 => "MIPS V",
            32 => "MIPS32",
            64 => "MIPS64",
            _ => "unrecognized",
        }
    }

    #[must_use]
    pub const fn register_size_name(code: u8) -> &'static str {
        match code {
            0 => "none",
            1 => "32-bit",
            2 => "64-bit",
            3 => "128-bit",
            _ => "unrecognized",
        }
    }

    #[must_use]
    pub const fn fp_abi_name(self) -> &'static str {
        match self.fp_abi {
            0 => "any",
            1 => "double",
            2 => "single",
            3 => "soft-float",
            4 => "old-64",
            5 => "FPXX",
            6 => "FP64",
            7 => "FP64A",
            _ => "unrecognized",
        }
    }

    #[must_use]
    pub const fn isa_extension_name(self) -> &'static str {
        match self.isa_extension {
            0 => "none",
            1 => "XLR",
            2 => "Octeon2",
            3 => "OcteonP",
            4 => "Loongson 3A",
            5 => "Octeon",
            6 => "R5900",
            7 => "R4650",
            8 => "R4010",
            9 => "VR4100",
            10 => "R3900",
            11 => "R10000",
            12 => "SB-1",
            13 => "VR4111/VR4181",
            14 => "VR4120",
            15 => "VR5400",
            16 => "VR5500",
            17 => "Loongson 2E",
            18 => "Loongson 2F",
            19 => "Octeon3",
            _ => "unrecognized",
        }
    }

    /// Returns recognized ASE names in ascending bit order.
    #[must_use]
    pub fn ase_names(self) -> Vec<&'static str> {
        let mut names = Vec::new();
        for (mask, name) in [
            (0x0000_0001, "DSP"),
            (0x0000_0002, "DSPR2"),
            (0x0000_0004, "EVA"),
            (0x0000_0008, "MCU"),
            (0x0000_0010, "MDMX"),
            (0x0000_0020, "MIPS3D"),
            (0x0000_0040, "MT"),
            (0x0000_0080, "SmartMIPS"),
            (0x0000_0100, "VZ"),
            (0x0000_0200, "MSA"),
            (0x0000_0400, "MIPS16"),
            (0x0000_0800, "microMIPS"),
            (0x0000_1000, "XPA"),
            (0x0000_8000, "CRC"),
            (0x0002_0000, "GINV"),
        ] {
            if self.ases & mask != 0 {
                names.push(name);
            }
        }
        names
    }

    /// Return ASE bits not named by this generic decoder.
    #[must_use]
    pub const fn unknown_ases(self) -> u32 {
        const KNOWN_ASES: u32 = 0x0000_0001
            | 0x0000_0002
            | 0x0000_0004
            | 0x0000_0008
            | 0x0000_0010
            | 0x0000_0020
            | 0x0000_0040
            | 0x0000_0080
            | 0x0000_0100
            | 0x0000_0200
            | 0x0000_0400
            | 0x0000_0800
            | 0x0000_1000
            | 0x0000_8000
            | 0x0002_0000;
        self.ases & !KNOWN_ASES
    }

    /// Whether flags1 marks odd single-precision floating-point registers.
    #[must_use]
    pub const fn has_odd_single_precision_registers(self) -> bool {
        self.flags1 & MIPS_AFL_FLAGS1_ODDSPREG != 0
    }

    /// Return flags1 bits not named by this generic decoder.
    #[must_use]
    pub const fn unknown_flags1(self) -> u32 {
        self.flags1 & !MIPS_AFL_FLAGS1_ODDSPREG
    }
}

/// One MIPS ABI flags section. Unknown structure versions are retained by
/// their raw version and size while version-zero payloads are decoded.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MipsAbiFlags {
    pub section_index: usize,
    pub section_size: u32,
    pub version: u16,
    pub version_zero: Option<MipsAbiFlagsV0>,
}

/// MIPS ABI flags metadata carried by a PT_MIPS_ABIFLAGS program header.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MipsAbiFlagsSegment {
    pub program_header_index: usize,
    pub file_offset: u32,
    pub virtual_address: u32,
    pub segment_size: u32,
    pub version: u16,
    pub version_zero: Option<MipsAbiFlagsV0>,
}

fn decode_mips_abi_flags_payload(data: &[u8]) -> Result<(u16, Option<MipsAbiFlagsV0>), Error> {
    if data.len() < 2 {
        return Err(Error::new(
            "MIPS ABI flags payload is too small to contain its version field",
        ));
    }
    let version = read_u16(data, 0)?;
    let version_zero = if version == 0 {
        if data.len() != MIPS_ABIFLAGS_V0_SIZE {
            return Err(Error::new(
                "version-zero MIPS ABI flags payload must be exactly 24 bytes",
            ));
        }
        Some(MipsAbiFlagsV0 {
            isa_level: read_u8(data, 2)?,
            isa_revision: read_u8(data, 3)?,
            gpr_size: read_u8(data, 4)?,
            cpr1_size: read_u8(data, 5)?,
            cpr2_size: read_u8(data, 6)?,
            fp_abi: read_u8(data, 7)?,
            isa_extension: read_u32(data, 8)?,
            ases: read_u32(data, 12)?,
            flags1: read_u32(data, 16)?,
            flags2: read_u32(data, 20)?,
        })
    } else {
        None
    };
    Ok((version, version_zero))
}

fn parse_mips_abi_flags_segments(
    bytes: &[u8],
    machine: u16,
    program_headers: &[ProgramHeader],
) -> Result<Vec<MipsAbiFlagsSegment>, Error> {
    if machine != EM_MIPS {
        return Ok(Vec::new());
    }

    let mut result = Vec::new();
    for (program_header_index, header) in program_headers.iter().enumerate() {
        if header.kind != PT_MIPS_ABIFLAGS {
            continue;
        }
        if !result.is_empty() {
            return Err(Error::new(
                "ELF contains more than one PT_MIPS_ABIFLAGS program header",
            ));
        }
        let range = checked_range(
            bytes,
            u64::from(header.offset),
            u64::from(header.file_size),
            "PT_MIPS_ABIFLAGS segment",
        )?;
        let data = &bytes[range];
        let (version, version_zero) = decode_mips_abi_flags_payload(data)?;
        result.push(MipsAbiFlagsSegment {
            program_header_index,
            file_offset: header.offset,
            virtual_address: header.virtual_address,
            segment_size: header.file_size,
            version,
            version_zero,
        });
    }
    Ok(result)
}

fn parse_mips_reginfo_segments(
    bytes: &[u8],
    machine: u16,
    program_headers: &[ProgramHeader],
) -> Result<Vec<MipsRegInfoSegment>, Error> {
    if machine != EM_MIPS {
        return Ok(Vec::new());
    }

    let mut result = Vec::new();
    let mut saw_loadable_segment = false;
    for (program_header_index, header) in program_headers.iter().enumerate() {
        if header.kind == PT_LOAD {
            saw_loadable_segment = true;
            continue;
        }
        if header.kind != PT_MIPS_REGINFO {
            continue;
        }
        if !result.is_empty() {
            return Err(Error::new(
                "ELF contains more than one PT_MIPS_REGINFO program header",
            ));
        }
        if saw_loadable_segment {
            return Err(Error::new(
                "PT_MIPS_REGINFO must precede every PT_LOAD program header",
            ));
        }
        if header.file_size != MIPS_REGINFO_SIZE as u32 {
            return Err(Error::new(
                "PT_MIPS_REGINFO must identify one 24-byte ELF32_RegInfo record",
            ));
        }

        let range = checked_range(
            bytes,
            u64::from(header.offset),
            u64::from(header.file_size),
            "PT_MIPS_REGINFO segment",
        )?;
        let data = &bytes[range];
        result.push(MipsRegInfoSegment {
            program_header_index,
            file_offset: header.offset,
            virtual_address: header.virtual_address,
            gpr_mask: read_u32(data, 0)?,
            coprocessor_masks: [
                read_u32(data, 4)?,
                read_u32(data, 8)?,
                read_u32(data, 12)?,
                read_u32(data, 16)?,
            ],
            gp_value: read_i32(data, 20)?,
        });
    }
    Ok(result)
}

/// Diagnostic interpretation of the MIPS-specific e_flags word.
///
/// Unknown field values and unclassified bits are retained. This object is
/// informational only: it does not accept/reject a target or imply that every
/// flag is implemented by the REPS2 execution model.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MipsElfFlags {
    pub raw: u32,
    /// Eight-bit processor subtype stored in bits 23..16.
    pub cpu_code: u8,
    /// Four-bit MIPS ABI identifier stored in bits 15..12.
    pub abi_code: u16,
    /// Four-bit MIPS architecture level stored in bits 31..28.
    pub architecture_code: u8,
    /// Named low-order feature flags.
    pub features: u32,
    /// Bits not classified by this parser; never silently discarded.
    pub unknown_bits: u32,
}

impl MipsElfFlags {
    #[must_use]
    pub const fn cpu_name(self) -> &'static str {
        match self.cpu_code {
            0x00 => "generic MIPS",
            0x81 => "R3900",
            0x82 => "R4010",
            0x83 => "R4100",
            0x85 => "R4650",
            0x87 => "R4120",
            0x88 => "R4111",
            0x8A => "SB-1",
            0x8B => "Octeon",
            0x8C => "XLR",
            0x8D => "Octeon2",
            0x8E => "Octeon3",
            0x91 => "R5400",
            0x92 => "R5900",
            0x93 => "IAMR2",
            0x98 => "R5500",
            0x99 => "R9000",
            0xA0 => "Loongson 2E",
            0xA1 => "Loongson 2F",
            0xA2 => "GS464",
            0xA3 => "GS464E",
            0xA4 => "GS264E",
            _ => "unrecognized",
        }
    }

    #[must_use]
    pub const fn abi_name(self) -> &'static str {
        match self.abi_code {
            0x0000 => "unspecified",
            0x1000 => "O32",
            0x2000 => "O64",
            0x3000 => "EABI32",
            0x4000 => "EABI64",
            _ => "unrecognized",
        }
    }

    #[must_use]
    pub const fn architecture_name(self) -> &'static str {
        match self.architecture_code {
            0 => "MIPS I",
            1 => "MIPS II",
            2 => "MIPS III",
            3 => "MIPS IV",
            4 => "MIPS V",
            5 => "MIPS32",
            6 => "MIPS64",
            7 => "MIPS32r2",
            8 => "MIPS64r2",
            _ => "unrecognized",
        }
    }

    /// Returns the recognized low-order feature names in stable order.
    #[must_use]
    pub fn feature_names(self) -> Vec<&'static str> {
        let mut names = Vec::new();
        for (mask, name) in [
            (EF_MIPS_NOREORDER, "noreorder"),
            (EF_MIPS_PIC, "pic"),
            (EF_MIPS_CPIC, "cpic"),
            (EF_MIPS_XGOT, "xgot"),
            (EF_MIPS_64BIT_WHIRL, "64bit-whirl"),
            (EF_MIPS_ABI2, "abi2"),
            (EF_MIPS_ABI_ON32, "abi-on32"),
            (EF_MIPS_OPTIONS_FIRST, "options-first"),
            (EF_MIPS_32BITMODE, "32bit-mode"),
            (EF_MIPS_FP64, "fp64"),
            (EF_MIPS_NAN2008, "nan2008"),
        ] {
            if self.features & mask != 0 {
                names.push(name);
            }
        }
        names
    }
}

/// Decodes the MIPS-specific processor flags without interpreting unknown
/// fields as a failure. The caller must establish EM_MIPS separately.
#[must_use]
pub const fn decode_mips_elf_flags(raw: u32) -> MipsElfFlags {
    let known_mask = EF_MIPS_FEATURE_MASK | EF_MIPS_ABI_MASK | EF_MIPS_CPU_MASK | EF_MIPS_ARCH_MASK;
    MipsElfFlags {
        raw,
        cpu_code: ((raw & EF_MIPS_CPU_MASK) >> 16) as u8,
        abi_code: (raw & EF_MIPS_ABI_MASK) as u16,
        architecture_code: ((raw & EF_MIPS_ARCH_MASK) >> 28) as u8,
        features: raw & EF_MIPS_FEATURE_MASK,
        unknown_bits: raw & !known_mask,
    }
}

/// Parsed ELF metadata. This is not yet a complete PS2 ABI loader.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Image {
    pub file_type: u16,
    /// Raw e_ident[EI_OSABI] byte; meaning may be machine-specific.
    pub os_abi: u8,
    /// Raw e_ident[EI_ABIVERSION] byte; interpreted in the context of os_abi.
    pub abi_version: u8,
    pub machine: u16,
    pub entry_point: u32,
    pub flags: u32,
    pub section_name_table_index: u32,
    pub program_headers: Vec<ProgramHeader>,
    pub section_headers: Vec<SectionHeader>,
    pub symbols: Vec<Symbol>,
    pub relocations: Vec<Relocation>,
    /// MIPS register masks and $gp values decoded from SHT_MIPS_REGINFO.
    pub mips_reginfo: Vec<MipsRegInfo>,
    /// Register-use metadata decoded from PT_MIPS_REGINFO program headers.
    pub mips_reginfo_segments: Vec<MipsRegInfoSegment>,
    /// MIPS ABI flags decoded from PT_MIPS_ABIFLAGS program headers.
    pub mips_abi_flags_segments: Vec<MipsAbiFlagsSegment>,
    /// MIPS ISA, register-size and FP ABI metadata decoded from SHT_MIPS_ABIFLAGS.
    pub mips_abi_flags: Vec<MipsAbiFlags>,
    /// Variable-length MIPS option descriptors decoded from SHT_MIPS_OPTIONS.
    pub mips_options: Vec<MipsOptionsSection>,
}

impl Image {
    /// Returns a generic System V ABI label for the raw EI_OSABI byte.
    ///
    /// Processor supplements may assign machine-specific meanings in the
    /// architecture-specific range. Unknown values are preserved in os_abi.
    #[must_use]
    pub const fn os_abi_name(&self) -> &'static str {
        match self.os_abi {
            0 => "System V / unspecified",
            1 => "HP-UX",
            2 => "NetBSD",
            3 => "Linux",
            6 => "Solaris",
            7 => "AIX",
            8 => "IRIX",
            9 => "FreeBSD",
            10 => "Tru64",
            11 => "Novell Modesto",
            12 => "OpenBSD",
            13 => "OpenVMS",
            14 => "NonStop Kernel",
            64..=u8::MAX => "architecture-specific",
            _ => "reserved/unrecognized",
        }
    }

    /// Returns a generic ELF object type label while retaining the raw code.
    #[must_use]
    pub const fn file_type_name(&self) -> &'static str {
        match self.file_type {
            ET_NONE => "no file type",
            ET_REL => "relocatable object",
            ET_EXEC => "executable",
            ET_DYN => "shared object",
            ET_CORE => "core file",
            ET_LOOS..=ET_HIOS => "OS-specific type",
            ET_LOPROC..=ET_HIPROC => "processor-specific type",
            _ => "reserved/unknown type",
        }
    }

    #[must_use]
    pub fn is_mips(&self) -> bool {
        self.machine == EM_MIPS
    }

    #[must_use]
    pub fn is_executable(&self) -> bool {
        self.file_type == ET_EXEC
    }

    /// Returns processor-specific header diagnostics only for EM_MIPS files.
    #[must_use]
    pub fn mips_elf_flags(&self) -> Option<MipsElfFlags> {
        self.is_mips().then(|| decode_mips_elf_flags(self.flags))
    }

    #[must_use]
    pub fn loadable_segment_count(&self) -> usize {
        self.program_headers
            .iter()
            .filter(|header| header.kind == PT_LOAD)
            .count()
    }
}

/// A materialized PT_LOAD segment, including its zero-filled BSS tail.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LoadedSegment {
    pub virtual_address: u32,
    pub flags: u32,
    pub file_size: usize,
    pub bytes: Vec<u8>,
}

/// Metadata describing one PT_LOAD segment after placement in EE main RAM.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MainRamSegment {
    pub virtual_address: u32,
    pub physical_address: u32,
    pub flags: u32,
    pub file_size: usize,
    pub memory_size: usize,
}

/// A complete 32 MiB initial EE main-RAM image constructed from PT_LOAD data.
/// BSS bytes are already zero-filled; no host pointers or guest aliases remain.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MainRamImage {
    pub entry_point: u32,
    pub bytes: Vec<u8>,
    pub segments: Vec<MainRamSegment>,
}

/// Translate only the documented direct main-RAM aliases. This deliberately
/// does not apply COP0/TLB mappings or map MMIO/BIOS/IOP/VU memory.
#[must_use]
pub fn ee_main_ram_physical_address(virtual_address: u32) -> Option<u32> {
    if virtual_address < EE_MAIN_RAM_SIZE as u32 {
        return Some(virtual_address);
    }
    if (0x2000_0000..0x2200_0000).contains(&virtual_address) {
        return Some(virtual_address - 0x2000_0000);
    }
    if (0x3010_0000..0x3200_0000).contains(&virtual_address) {
        return Some(virtual_address - 0x3000_0000);
    }
    if (0x8000_0000..0xA000_0000).contains(&virtual_address)
        || (0xA000_0000..0xC000_0000).contains(&virtual_address)
    {
        let physical = virtual_address & 0x1FFF_FFFF;
        if physical < EE_MAIN_RAM_SIZE as u32 {
            return Some(physical);
        }
    }
    None
}

fn read_u8(bytes: &[u8], offset: usize) -> Result<u8, Error> {
    bytes
        .get(offset)
        .copied()
        .ok_or_else(|| Error::new("ELF field extends beyond the input buffer"))
}

fn read_u16(bytes: &[u8], offset: usize) -> Result<u16, Error> {
    let end = offset
        .checked_add(2)
        .ok_or_else(|| Error::new("ELF offset overflow"))?;
    let part = bytes
        .get(offset..end)
        .ok_or_else(|| Error::new("ELF 16-bit field extends beyond the input buffer"))?;
    Ok(u16::from_le_bytes([part[0], part[1]]))
}

fn read_u32(bytes: &[u8], offset: usize) -> Result<u32, Error> {
    let end = offset
        .checked_add(4)
        .ok_or_else(|| Error::new("ELF offset overflow"))?;
    let part = bytes
        .get(offset..end)
        .ok_or_else(|| Error::new("ELF 32-bit field extends beyond the input buffer"))?;
    Ok(u32::from_le_bytes([part[0], part[1], part[2], part[3]]))
}

fn read_i32(bytes: &[u8], offset: usize) -> Result<i32, Error> {
    Ok(i32::from_le_bytes(read_u32(bytes, offset)?.to_le_bytes()))
}

fn checked_range(bytes: &[u8], offset: u64, size: u64, label: &str) -> Result<Range<usize>, Error> {
    let end = offset
        .checked_add(size)
        .ok_or_else(|| Error::new(format!("{label} range overflows")))?;
    let start = usize::try_from(offset)
        .map_err(|_| Error::new(format!("{label} offset does not fit host size")))?;
    let end = usize::try_from(end)
        .map_err(|_| Error::new(format!("{label} end does not fit host size")))?;
    if end > bytes.len() {
        return Err(Error::new(format!(
            "{label} extends beyond the input buffer"
        )));
    }
    Ok(start..end)
}

fn validate_table(
    bytes: &[u8],
    offset: u32,
    entry_size: u16,
    count: u32,
    minimum_size: usize,
    label: &str,
) -> Result<(), Error> {
    if count == 0 {
        return Ok(());
    }
    if usize::from(entry_size) < minimum_size {
        return Err(Error::new(format!("ELF {label} entry size is too small")));
    }
    let size = u64::from(entry_size) * u64::from(count);
    checked_range(
        bytes,
        u64::from(offset),
        size,
        &format!("ELF {label} table"),
    )?;
    Ok(())
}

fn section_data<'a>(
    bytes: &'a [u8],
    section: &SectionHeader,
    label: &str,
) -> Result<&'a [u8], Error> {
    if section.kind == SHT_NOBITS {
        return Ok(&[]);
    }
    let range = checked_range(
        bytes,
        u64::from(section.offset),
        u64::from(section.size),
        label,
    )?;
    Ok(&bytes[range])
}

fn validate_string_table(table: &[u8], label: &str) -> Result<(), Error> {
    if table.is_empty() {
        return Ok(());
    }
    if table.first() != Some(&0) {
        return Err(Error::new(format!("{label} must begin with a null byte")));
    }
    if table.last() != Some(&0) {
        return Err(Error::new(format!("{label} must end with a null byte")));
    }
    Ok(())
}

fn read_string(table: &[u8], offset: u32, label: &str) -> Result<String, Error> {
    let start = usize::try_from(offset)
        .map_err(|_| Error::new(format!("{label} offset does not fit host size")))?;
    if start >= table.len() {
        return Err(Error::new(format!("{label} offset is out of range")));
    }
    let tail = &table[start..];
    let end = tail
        .iter()
        .position(|value| *value == 0)
        .ok_or_else(|| Error::new(format!("{label} is not null-terminated")))?;
    let value = std::str::from_utf8(&tail[..end])
        .map_err(|_| Error::new(format!("{label} is not valid UTF-8")))?;
    Ok(value.to_owned())
}

fn read_section_header(bytes: &[u8], offset: usize) -> Result<SectionHeader, Error> {
    Ok(SectionHeader {
        name: String::new(),
        name_offset: read_u32(bytes, offset)?,
        kind: read_u32(bytes, offset + 4)?,
        flags: read_u32(bytes, offset + 8)?,
        virtual_address: read_u32(bytes, offset + 12)?,
        offset: read_u32(bytes, offset + 16)?,
        size: read_u32(bytes, offset + 20)?,
        link: read_u32(bytes, offset + 24)?,
        info: read_u32(bytes, offset + 28)?,
        alignment: read_u32(bytes, offset + 32)?,
        entry_size: read_u32(bytes, offset + 36)?,
    })
}

fn parse_symbols(
    bytes: &[u8],
    sections: &[SectionHeader],
    limits: ParseLimits,
) -> Result<Vec<Symbol>, Error> {
    // A SHT_SYMTAB_SHNDX section is associated with exactly one symbol table.
    // Build the association once so malformed duplicate/orphan tables are not
    // silently ignored and lookup remains linear in the section count.
    let mut extended_section_tables = vec![None; sections.len()];
    for (extended_index, extended) in sections.iter().enumerate() {
        if extended.kind != SHT_SYMTAB_SHNDX {
            continue;
        }
        let symbol_table_index = usize::try_from(extended.link)
            .map_err(|_| Error::new("Extended symbol-index link does not fit host size"))?;
        let symbol_table = sections
            .get(symbol_table_index)
            .ok_or_else(|| Error::new("SHT_SYMTAB_SHNDX links to an invalid section"))?;
        if symbol_table.kind != SHT_SYMTAB && symbol_table.kind != SHT_DYNSYM {
            return Err(Error::new(
                "SHT_SYMTAB_SHNDX does not link to SHT_SYMTAB or SHT_DYNSYM",
            ));
        }
        if extended.info != 0 {
            return Err(Error::new("SHT_SYMTAB_SHNDX sh_info must be zero"));
        }
        if extended_section_tables[symbol_table_index]
            .replace(extended_index)
            .is_some()
        {
            return Err(Error::new(
                "Multiple SHT_SYMTAB_SHNDX sections link to one symbol table",
            ));
        }
    }

    let mut symbols = Vec::new();
    for (section_index, section) in sections.iter().enumerate() {
        if section.kind != SHT_SYMTAB && section.kind != SHT_DYNSYM {
            continue;
        }
        if section.entry_size < SYMBOL_SIZE as u32 || section.size % section.entry_size != 0 {
            return Err(Error::new("ELF symbol table has an invalid entry size"));
        }
        let string_index = usize::try_from(section.link)
            .map_err(|_| Error::new("Symbol string-table index overflow"))?;
        let strings = sections
            .get(string_index)
            .ok_or_else(|| Error::new("ELF symbol table links to an invalid string table"))?;
        if strings.kind != SHT_STRTAB {
            return Err(Error::new(
                "ELF symbol table link does not reference SHT_STRTAB",
            ));
        }
        let count = usize::try_from(section.size / section.entry_size)
            .map_err(|_| Error::new("ELF symbol count does not fit host size"))?;
        let local_symbol_end = usize::try_from(section.info)
            .map_err(|_| Error::new("ELF local-symbol boundary does not fit host size"))?;
        if count == 0 {
            // A symbol table with sh_size zero has no entries. ELF defines
            // sh_info as one greater than the index of the last local symbol,
            // so an empty table requires zero, and there is no null symbol at
            // index zero to validate. Production MIPS linkers emit this shape
            // for stripped retail images.
            if local_symbol_end != 0 {
                return Err(Error::new(
                    "ELF symbol table has an invalid local-symbol boundary",
                ));
            }
        } else if local_symbol_end == 0 || local_symbol_end > count {
            return Err(Error::new(
                "ELF symbol table has an invalid local-symbol boundary",
            ));
        }
        if count > limits.max_symbols.saturating_sub(symbols.len()) {
            return Err(Error::new(
                "ELF symbol count exceeds configured parser limit",
            ));
        }

        let extended_indices = if let Some(extended_index) = extended_section_tables[section_index]
        {
            let extended = &sections[extended_index];
            if extended.entry_size != 4 || !extended.size.is_multiple_of(4) {
                return Err(Error::new("SHT_SYMTAB_SHNDX must use four-byte entries"));
            }
            let extended_count = usize::try_from(extended.size / 4)
                .map_err(|_| Error::new("Extended symbol-index count overflow"))?;
            if extended_count != count {
                return Err(Error::new(
                    "SHT_SYMTAB_SHNDX entry count does not match its symbol table",
                ));
            }
            let data = section_data(bytes, extended, "ELF extended symbol-index table")?;
            let mut values = Vec::with_capacity(extended_count);
            for index in 0..extended_count {
                let offset = index
                    .checked_mul(4)
                    .ok_or_else(|| Error::new("Extended symbol-index offset overflow"))?;
                values.push(read_u32(data, offset)?);
            }
            Some(values)
        } else {
            None
        };

        if count == 0 {
            continue;
        }

        let string_data = section_data(bytes, strings, "ELF symbol string table")?;
        let table = section_data(bytes, section, "ELF symbol table")?;
        if table
            .get(..SYMBOL_SIZE)
            .is_none_or(|undefined_symbol| undefined_symbol.iter().any(|byte| *byte != 0))
        {
            return Err(Error::new(
                "ELF symbol table entry zero is not the null symbol",
            ));
        }

        for index in 0..count {
            let base = index
                .checked_mul(section.entry_size as usize)
                .ok_or_else(|| Error::new("ELF symbol entry offset overflow"))?;
            let name_offset = read_u32(table, base)?;
            let info = read_u8(table, base + 12)?;
            let other = read_u8(table, base + 13)?;
            let raw_symbol_section = read_u16(table, base + 14)?;
            let extended_value = extended_indices
                .as_ref()
                .map(|values| values[index])
                .unwrap_or(0);
            let symbol_section = if raw_symbol_section == SHN_XINDEX {
                let resolved = extended_indices
                    .as_ref()
                    .map(|_| extended_value)
                    .ok_or_else(|| {
                        Error::new("SHN_XINDEX symbol has no linked SHT_SYMTAB_SHNDX table")
                    })?;
                if resolved < u32::from(SHN_LORESERVE)
                    || usize::try_from(resolved)
                        .ok()
                        .is_none_or(|resolved_index| resolved_index >= sections.len())
                {
                    return Err(Error::new(
                        "SHT_SYMTAB_SHNDX contains an invalid extended section index",
                    ));
                }
                resolved
            } else {
                if extended_value != 0 {
                    return Err(Error::new(
                        "SHT_SYMTAB_SHNDX entry must be SHN_UNDEF for a non-SHN_XINDEX symbol",
                    ));
                }
                if raw_symbol_section < SHN_LORESERVE
                    && usize::from(raw_symbol_section) >= sections.len()
                {
                    return Err(Error::new("ELF symbol references an invalid section index"));
                }
                u32::from(raw_symbol_section)
            };
            let binding = info >> 4;
            if (index < local_symbol_end) != (binding == STB_LOCAL) {
                return Err(Error::new(
                    "ELF symbol bindings disagree with the symbol table local-symbol boundary",
                ));
            }
            symbols.push(Symbol {
                name: if name_offset == 0 {
                    String::new()
                } else {
                    read_string(string_data, name_offset, "ELF symbol name")?
                },
                value: read_u32(table, base + 4)?,
                size: read_u32(table, base + 8)?,
                table_section_index: section_index,
                table_index: u32::try_from(index)
                    .map_err(|_| Error::new("ELF symbol index overflow"))?,
                section_index: symbol_section,
                binding,
                kind: info & 0x0F,
                visibility: other & 0x03,
            });
        }
    }
    Ok(symbols)
}

fn parse_mips_abi_sections(
    bytes: &[u8],
    machine: u16,
    sections: &[SectionHeader],
) -> Result<(Vec<MipsRegInfo>, Vec<MipsAbiFlags>), Error> {
    if machine != EM_MIPS {
        return Ok((Vec::new(), Vec::new()));
    }

    let mut register_info = Vec::new();
    let mut abi_flags = Vec::new();
    for (section_index, section) in sections.iter().enumerate() {
        match section.kind {
            SHT_MIPS_REGINFO => {
                if section.size != MIPS_REGINFO_SIZE as u32 {
                    return Err(Error::new(
                        "SHT_MIPS_REGINFO must contain one 24-byte Elf32_RegInfo record",
                    ));
                }
                let data = section_data(bytes, section, "MIPS register information")?;
                register_info.push(MipsRegInfo {
                    section_index,
                    gpr_mask: read_u32(data, 0)?,
                    coprocessor_masks: [
                        read_u32(data, 4)?,
                        read_u32(data, 8)?,
                        read_u32(data, 12)?,
                        read_u32(data, 16)?,
                    ],
                    gp_value: read_i32(data, 20)?,
                });
            }
            SHT_MIPS_ABIFLAGS => {
                if !abi_flags.is_empty() {
                    return Err(Error::new(
                        "ELF contains more than one SHT_MIPS_ABIFLAGS section",
                    ));
                }
                let data = section_data(bytes, section, "MIPS ABI flags")?;
                let (version, version_zero) = decode_mips_abi_flags_payload(data)?;
                abi_flags.push(MipsAbiFlags {
                    section_index,
                    section_size: section.size,
                    version,
                    version_zero,
                });
            }
            _ => {}
        }
    }
    Ok((register_info, abi_flags))
}

fn parse_mips_options_sections(
    bytes: &[u8],
    machine: u16,
    sections: &[SectionHeader],
    limits: ParseLimits,
) -> Result<Vec<MipsOptionsSection>, Error> {
    if machine != EM_MIPS {
        return Ok(Vec::new());
    }

    let mut result = Vec::new();
    let mut descriptor_count = 0usize;
    for (section_index, section) in sections.iter().enumerate() {
        if section.kind != SHT_MIPS_OPTIONS {
            continue;
        }
        let data = section_data(bytes, section, "MIPS options")?;
        let mut descriptors = Vec::new();
        let mut offset = 0usize;
        while offset < data.len() {
            let remaining = data.len() - offset;
            if remaining < MIPS_OPTIONS_HEADER_SIZE {
                return Err(Error::new(
                    "SHT_MIPS_OPTIONS ends with a truncated descriptor header",
                ));
            }

            let kind = read_u8(data, offset)?;
            let size = read_u8(data, offset + 1)?;
            let descriptor_size = usize::from(size);
            if descriptor_size < MIPS_OPTIONS_HEADER_SIZE {
                return Err(Error::new(
                    "SHT_MIPS_OPTIONS descriptor size is smaller than its eight-byte header",
                ));
            }
            let descriptor_end = offset
                .checked_add(descriptor_size)
                .ok_or_else(|| Error::new("SHT_MIPS_OPTIONS descriptor end overflows"))?;
            if descriptor_end > data.len() {
                return Err(Error::new(
                    "SHT_MIPS_OPTIONS descriptor extends beyond its section",
                ));
            }

            let section_target = read_u16(data, offset + 2)?;
            if section_target != 0 && usize::from(section_target) >= sections.len() {
                return Err(Error::new(
                    "SHT_MIPS_OPTIONS descriptor references an invalid section index",
                ));
            }
            let info = read_u32(data, offset + 4)?;
            let register_info = if kind == ODK_REGINFO {
                if descriptor_size < MIPS_OPTIONS_HEADER_SIZE + MIPS_OPTION_REGINFO_SIZE {
                    return Err(Error::new(
                        "ODK_REGINFO descriptor is too small for an ELF32_RegInfo payload",
                    ));
                }
                let base = offset + MIPS_OPTIONS_HEADER_SIZE;
                Some(MipsOptionRegInfo {
                    gpr_mask: read_u32(data, base)?,
                    coprocessor_masks: [
                        read_u32(data, base + 4)?,
                        read_u32(data, base + 8)?,
                        read_u32(data, base + 12)?,
                        read_u32(data, base + 16)?,
                    ],
                    gp_value: read_i32(data, base + 20)?,
                })
            } else {
                None
            };

            descriptor_count = descriptor_count
                .checked_add(1)
                .ok_or_else(|| Error::new("SHT_MIPS_OPTIONS descriptor count overflows"))?;
            if descriptor_count > limits.max_mips_option_descriptors {
                return Err(Error::new(
                    "MIPS option descriptors exceed configured parser limit",
                ));
            }
            descriptors.push(MipsOptionDescriptor {
                kind,
                size,
                section_index: section_target,
                info,
                payload_size: u8::try_from(descriptor_size - MIPS_OPTIONS_HEADER_SIZE)
                    .map_err(|_| Error::new("MIPS option payload size overflow"))?,
                register_info,
            });
            offset = descriptor_end;
        }
        result.push(MipsOptionsSection {
            section_index,
            descriptors,
        });
    }
    Ok(result)
}

fn parse_relocations(
    bytes: &[u8],
    sections: &[SectionHeader],
    limits: ParseLimits,
) -> Result<Vec<Relocation>, Error> {
    let mut relocations = Vec::new();
    for (section_index, section) in sections.iter().enumerate() {
        let rela = section.kind == SHT_RELA;
        if section.kind != SHT_REL && !rela {
            continue;
        }
        let minimum = if rela { RELA_SIZE } else { REL_SIZE };
        if section.entry_size < minimum as u32 || section.size % section.entry_size != 0 {
            return Err(Error::new("ELF relocation table has an invalid entry size"));
        }
        let symbol_table_index = usize::try_from(section.link)
            .map_err(|_| Error::new("Relocation symbol table index overflow"))?;
        let target_section_index = usize::try_from(section.info)
            .map_err(|_| Error::new("Relocation target section index overflow"))?;
        let symbol_table = sections
            .get(symbol_table_index)
            .ok_or_else(|| Error::new("ELF relocation references an invalid symbol table"))?;
        if symbol_table.kind != SHT_SYMTAB && symbol_table.kind != SHT_DYNSYM {
            return Err(Error::new(
                "ELF relocation does not reference a symbol table",
            ));
        }
        if sections.get(target_section_index).is_none() {
            return Err(Error::new(
                "ELF relocation references an invalid target section",
            ));
        }
        if symbol_table.entry_size < SYMBOL_SIZE as u32
            || symbol_table.size % symbol_table.entry_size != 0
        {
            return Err(Error::new(
                "ELF relocation references a malformed symbol table",
            ));
        }
        let symbol_count = symbol_table.size / symbol_table.entry_size;
        let count = usize::try_from(section.size / section.entry_size)
            .map_err(|_| Error::new("ELF relocation count does not fit host size"))?;
        if count > limits.max_relocations.saturating_sub(relocations.len()) {
            return Err(Error::new(
                "ELF relocation count exceeds configured parser limit",
            ));
        }
        let table = section_data(bytes, section, "ELF relocation table")?;
        for index in 0..count {
            let base = index
                .checked_mul(section.entry_size as usize)
                .ok_or_else(|| Error::new("ELF relocation entry offset overflow"))?;
            let offset = read_u32(table, base)?;
            let info = read_u32(table, base + 4)?;
            let symbol_index = info >> 8;
            if symbol_index >= symbol_count {
                return Err(Error::new(
                    "ELF relocation references an invalid symbol index",
                ));
            }
            relocations.push(Relocation {
                offset,
                symbol_index,
                relocation_type: (info & 0xFF) as u8,
                addend: if rela {
                    Some(read_i32(table, base + 8)?)
                } else {
                    None
                },
                relocation_section_index: section_index,
                target_section_index,
                symbol_table_section_index: symbol_table_index,
            });
        }
    }
    Ok(relocations)
}

/// Parse ELF32 little-endian metadata with bounded table processing.
pub fn parse(bytes: &[u8], limits: ParseLimits) -> Result<Image, Error> {
    if bytes.len() < ELF_HEADER_SIZE {
        return Err(Error::new("Input is too small to contain an ELF32 header"));
    }
    if bytes.get(0..4) != Some(&b"\x7FELF"[..]) {
        return Err(Error::new("Input does not have the ELF magic signature"));
    }
    if read_u8(bytes, 4)? != 1 {
        return Err(Error::new("Only ELF32 files are supported"));
    }
    if read_u8(bytes, 5)? != 1 {
        return Err(Error::new("Only little-endian ELF files are supported"));
    }
    if read_u8(bytes, 6)? != 1 {
        return Err(Error::new("Unsupported ELF identification version"));
    }
    let os_abi = read_u8(bytes, 7)?;
    let abi_version = read_u8(bytes, 8)?;

    let file_type = read_u16(bytes, 16)?;
    let machine = read_u16(bytes, 18)?;
    if read_u32(bytes, 20)? != 1 {
        return Err(Error::new("Unsupported ELF header version"));
    }
    let entry_point = read_u32(bytes, 24)?;
    let program_offset = read_u32(bytes, 28)?;
    let section_offset = read_u32(bytes, 32)?;
    let flags = read_u32(bytes, 36)?;
    let header_size = read_u16(bytes, 40)?;
    let program_entry_size = read_u16(bytes, 42)?;
    let raw_program_count = read_u16(bytes, 44)?;
    let section_entry_size = read_u16(bytes, 46)?;
    let raw_section_count = read_u16(bytes, 48)?;
    let raw_section_name_table_index = read_u16(bytes, 50)?;

    if usize::from(header_size) < ELF_HEADER_SIZE || usize::from(header_size) > bytes.len() {
        return Err(Error::new("Invalid ELF header size"));
    }
    // Resolve ELF extended section and program-header numbering through
    // section-header entry zero. An absent section table cannot carry either
    // extended count.
    let (
        section_count,
        section_name_table_index,
        extended_section_count,
        extended_name_index,
        program_count,
        extended_program_count,
    ) = if section_offset == 0 {
        if raw_section_count != 0 || raw_section_name_table_index != 0 {
            return Err(Error::new(
                "ELF section numbering references a missing section table",
            ));
        }
        if raw_program_count == PN_XNUM {
            return Err(Error::new("ELF PN_XNUM requires section-header entry zero"));
        }
        (
            0u32,
            0u32,
            false,
            false,
            u32::from(raw_program_count),
            false,
        )
    } else {
        if section_offset < u32::from(header_size) {
            return Err(Error::new(
                "ELF section-header table overlaps the ELF header",
            ));
        }
        validate_table(
            bytes,
            section_offset,
            section_entry_size,
            1,
            SECTION_HEADER_SIZE,
            "section-header zero",
        )?;
        let section_zero_offset = usize::try_from(section_offset)
            .map_err(|_| Error::new("Section-header offset overflow"))?;
        let section_zero = read_section_header(bytes, section_zero_offset)?;
        if section_zero.kind != SHT_NULL {
            return Err(Error::new(
                "ELF section header zero is not the required null entry",
            ));
        }

        let extended_count = raw_section_count == 0;
        let resolved_count = if extended_count {
            if section_zero.size < u32::from(SHN_LORESERVE) {
                return Err(Error::new(
                    "ELF extended section count is below SHN_LORESERVE",
                ));
            }
            section_zero.size
        } else {
            if raw_section_count >= SHN_LORESERVE {
                return Err(Error::new(
                    "ELF section count at or above SHN_LORESERVE must use extended numbering",
                ));
            }
            u32::from(raw_section_count)
        };
        if resolved_count == 0 {
            return Err(Error::new("ELF section table has a zero section count"));
        }

        let extended_programs = raw_program_count == PN_XNUM;
        let resolved_program_count = if extended_programs {
            if section_zero.info < u32::from(PN_XNUM) {
                return Err(Error::new(
                    "ELF PN_XNUM program-header count is below PN_XNUM",
                ));
            }
            section_zero.info
        } else {
            if section_zero.info != 0 {
                return Err(Error::new(
                    "ELF section header zero sh_info must be zero without PN_XNUM",
                ));
            }
            u32::from(raw_program_count)
        };

        let extended_names = raw_section_name_table_index == SHN_XINDEX;
        let resolved_name_index = if extended_names {
            if section_zero.link < u32::from(SHN_LORESERVE) {
                return Err(Error::new(
                    "ELF extended section-name table index is below SHN_LORESERVE",
                ));
            }
            section_zero.link
        } else {
            if raw_section_name_table_index >= SHN_LORESERVE {
                return Err(Error::new(
                    "ELF section-name table index must use SHN_XINDEX when reserved",
                ));
            }
            u32::from(raw_section_name_table_index)
        };
        (
            resolved_count,
            resolved_name_index,
            extended_count,
            extended_names,
            resolved_program_count,
            extended_programs,
        )
    };

    let section_count_host = usize::try_from(section_count)
        .map_err(|_| Error::new("ELF section count does not fit host size"))?;
    let program_count_host = usize::try_from(program_count)
        .map_err(|_| Error::new("ELF program-header count does not fit host size"))?;
    if program_count_host > limits.max_program_headers {
        return Err(Error::new(
            "ELF program-header count exceeds configured parser limit",
        ));
    }
    if section_count_host > limits.max_sections {
        return Err(Error::new(
            "ELF section count exceeds configured parser limit",
        ));
    }
    if section_count == 0 && section_name_table_index != 0 {
        return Err(Error::new(
            "ELF has a section-name table index but no section table",
        ));
    }
    if section_count != 0
        && section_name_table_index != 0
        && section_name_table_index >= section_count
    {
        return Err(Error::new("ELF section-name table index is out of range"));
    }
    if program_count != 0 && program_offset < u32::from(header_size) {
        return Err(Error::new(
            "ELF program-header table overlaps the ELF header",
        ));
    }
    validate_table(
        bytes,
        program_offset,
        program_entry_size,
        program_count,
        PROGRAM_HEADER_SIZE,
        "program-header",
    )?;
    validate_table(
        bytes,
        section_offset,
        section_entry_size,
        section_count,
        SECTION_HEADER_SIZE,
        "section-header",
    )?;

    if program_count != 0 && section_count != 0 {
        let program_end =
            u64::from(program_offset) + u64::from(program_entry_size) * u64::from(program_count);
        let section_end =
            u64::from(section_offset) + u64::from(section_entry_size) * u64::from(section_count);
        if u64::from(program_offset) < section_end && u64::from(section_offset) < program_end {
            return Err(Error::new(
                "ELF program-header and section-header tables overlap",
            ));
        }
    }

    let mut program_headers = Vec::with_capacity(program_count_host);
    for index in 0..program_count_host {
        let base = usize::try_from(program_offset)
            .map_err(|_| Error::new("Program-header offset overflow"))?
            .checked_add(
                index
                    .checked_mul(usize::from(program_entry_size))
                    .ok_or_else(|| Error::new("Program-header index overflow"))?,
            )
            .ok_or_else(|| Error::new("Program-header offset overflow"))?;
        let header = ProgramHeader {
            kind: read_u32(bytes, base)?,
            offset: read_u32(bytes, base + 4)?,
            virtual_address: read_u32(bytes, base + 8)?,
            physical_address: read_u32(bytes, base + 12)?,
            file_size: read_u32(bytes, base + 16)?,
            memory_size: read_u32(bytes, base + 20)?,
            flags: read_u32(bytes, base + 24)?,
            alignment: read_u32(bytes, base + 28)?,
        };
        checked_range(
            bytes,
            u64::from(header.offset),
            u64::from(header.file_size),
            "ELF segment data",
        )?;
        if header.kind == PT_LOAD {
            if header.file_size > header.memory_size {
                return Err(Error::new("ELF PT_LOAD file size exceeds memory size"));
            }
            if header.alignment > 1 && !header.alignment.is_power_of_two() {
                return Err(Error::new("ELF PT_LOAD alignment is not a power of two"));
            }
            if header.alignment > 1
                && header.virtual_address % header.alignment != header.offset % header.alignment
            {
                return Err(Error::new(
                    "ELF PT_LOAD address and offset violate alignment",
                ));
            }
            if u64::from(header.virtual_address) + u64::from(header.memory_size)
                > (u64::from(u32::MAX) + 1)
            {
                return Err(Error::new(
                    "ELF PT_LOAD virtual range overflows 32-bit address space",
                ));
            }
        }
        program_headers.push(header);
    }

    let mips_reginfo_segments = parse_mips_reginfo_segments(bytes, machine, &program_headers)?;
    let mips_abi_flags_segments = parse_mips_abi_flags_segments(bytes, machine, &program_headers)?;

    let mut section_headers = Vec::with_capacity(section_count_host);
    for index in 0..section_count_host {
        let base = usize::try_from(section_offset)
            .map_err(|_| Error::new("Section-header offset overflow"))?
            .checked_add(
                index
                    .checked_mul(usize::from(section_entry_size))
                    .ok_or_else(|| Error::new("Section-header index overflow"))?,
            )
            .ok_or_else(|| Error::new("Section-header offset overflow"))?;
        let section = read_section_header(bytes, base)?;
        if section.kind != SHT_NOBITS && section.kind != SHT_NULL {
            checked_range(
                bytes,
                u64::from(section.offset),
                u64::from(section.size),
                "ELF section data",
            )?;
        }
        section_headers.push(section);
    }

    let expected_null_size = if extended_section_count {
        section_count
    } else {
        0
    };
    let expected_null_link = if extended_name_index {
        section_name_table_index
    } else {
        0
    };
    let expected_null_info = if extended_program_count {
        program_count
    } else {
        0
    };
    let null_section_is_invalid = section_headers.first().is_some_and(|null_section| {
        null_section.kind != SHT_NULL
            || null_section.name_offset != 0
            || null_section.flags != 0
            || null_section.virtual_address != 0
            || null_section.offset != 0
            || null_section.size != expected_null_size
            || null_section.link != expected_null_link
            || null_section.info != expected_null_info
            || null_section.alignment != 0
            || null_section.entry_size != 0
    });
    if null_section_is_invalid {
        return Err(Error::new(
            "ELF section header zero is not the required null entry",
        ));
    }

    let string_table_bytes = section_headers
        .iter()
        .filter(|section| section.kind == SHT_STRTAB)
        .try_fold(0usize, |sum, section| {
            sum.checked_add(section.size as usize)
                .ok_or_else(|| Error::new("ELF string-table size overflow"))
        })?;
    if string_table_bytes > limits.max_string_table_bytes {
        return Err(Error::new(
            "ELF string tables exceed configured parser limit",
        ));
    }
    for section in section_headers
        .iter()
        .filter(|section| section.kind == SHT_STRTAB)
    {
        let table = section_data(bytes, section, "ELF string table")?;
        validate_string_table(table, "ELF string table")?;
    }

    if section_name_table_index != 0 {
        let names_index = usize::try_from(section_name_table_index)
            .map_err(|_| Error::new("ELF section-name table index does not fit host size"))?;
        let names = section_headers
            .get(names_index)
            .ok_or_else(|| Error::new("ELF section-name table index is invalid"))?;
        if names.kind != SHT_STRTAB {
            return Err(Error::new(
                "ELF section-name table does not reference SHT_STRTAB",
            ));
        }
        let name_data = section_data(bytes, names, "ELF section-name string table")?;
        validate_string_table(name_data, "ELF section-name string table")?;
        for section in &mut section_headers {
            section.name = if section.name_offset == 0 {
                String::new()
            } else {
                read_string(name_data, section.name_offset, "ELF section name")?
            };
        }
    } else if section_headers
        .iter()
        .any(|section| section.name_offset != 0)
    {
        return Err(Error::new(
            "ELF section names are present but no section-name string table is declared",
        ));
    }

    let symbols = parse_symbols(bytes, &section_headers, limits)?;
    let relocations = parse_relocations(bytes, &section_headers, limits)?;
    let (mips_reginfo, mips_abi_flags) = parse_mips_abi_sections(bytes, machine, &section_headers)?;
    let mips_options = parse_mips_options_sections(bytes, machine, &section_headers, limits)?;
    Ok(Image {
        file_type,
        os_abi,
        abi_version,
        machine,
        entry_point,
        flags,
        section_name_table_index,
        program_headers,
        section_headers,
        symbols,
        relocations,
        mips_reginfo,
        mips_reginfo_segments,
        mips_abi_flags_segments,
        mips_abi_flags,
        mips_options,
    })
}

/// Materializes PT_LOAD data and zero-fills each memory-only tail. Overlapping
/// guest virtual ranges are rejected rather than silently mapped in an order.
/// Build a strict default-EE main-RAM image from a static MIPS ET_EXEC ELF.
///
/// All loadable segments must fit in the 32 MiB main-RAM aliases. Distinct
/// virtual aliases that collide on the same physical bytes are rejected.
/// Allocated REL/RELA relocations are rejected until a relocation engine exists.
pub fn build_main_ram_image(bytes: &[u8], image: &Image) -> Result<MainRamImage, Error> {
    if !image.is_mips() {
        return Err(Error::new("EE main-RAM image requires EM_MIPS"));
    }
    if !image.is_executable() {
        return Err(Error::new(
            "EE main-RAM image currently requires ET_EXEC; relocatable/shared ELF loading is not implemented",
        ));
    }

    if image.entry_point & 3 != 0 {
        return Err(Error::new("ELF EE entry point is not word-aligned"));
    }

    for relocation in &image.relocations {
        let target = image
            .section_headers
            .get(relocation.target_section_index)
            .ok_or_else(|| Error::new("ELF relocation target section index is invalid"))?;
        if target.flags & SHF_ALLOC != 0 {
            return Err(Error::new(format!(
                "allocated ELF relocation type {} at 0x{:08X} is not supported yet",
                relocation.relocation_type, relocation.offset
            )));
        }
    }

    let loaded = load_segments(bytes, image, EE_MAIN_RAM_SIZE)?;
    let mut ram = vec![0u8; EE_MAIN_RAM_SIZE];
    let mut physical_ranges: Vec<Range<u64>> = Vec::new();
    let mut segments = Vec::with_capacity(loaded.len());

    for segment in loaded {
        let memory_size = segment.bytes.len();
        let physical_address =
            ee_main_ram_physical_address(segment.virtual_address).ok_or_else(|| {
                Error::new(format!(
                    "PT_LOAD at 0x{:08X} is outside the supported EE main-RAM aliases",
                    segment.virtual_address
                ))
            })?;

        let start = u64::from(physical_address);
        let end = start
            .checked_add(memory_size as u64)
            .ok_or_else(|| Error::new("PT_LOAD physical main-RAM range overflows"))?;
        if end > EE_MAIN_RAM_SIZE as u64 {
            return Err(Error::new(format!(
                "PT_LOAD at 0x{:08X} extends beyond the 32 MiB EE main RAM",
                segment.virtual_address
            )));
        }

        if memory_size > 0 {
            let last_virtual = segment
                .virtual_address
                .checked_add(u32::try_from(memory_size - 1).map_err(|_| {
                    Error::new("PT_LOAD memory size does not fit guest address width")
                })?)
                .ok_or_else(|| Error::new("PT_LOAD guest virtual range overflows"))?;
            let last_physical = ee_main_ram_physical_address(last_virtual).ok_or_else(|| {
                Error::new("PT_LOAD crosses out of the supported EE main-RAM aliases")
            })?;
            if u64::from(last_physical) != end - 1 {
                return Err(Error::new(
                    "PT_LOAD crosses a non-contiguous EE main-RAM alias boundary",
                ));
            }

            if physical_ranges
                .iter()
                .any(|range| start < range.end && range.start < end)
            {
                return Err(Error::new(format!(
                    "PT_LOAD at 0x{:08X} overlaps another segment after EE RAM alias translation",
                    segment.virtual_address
                )));
            }
            physical_ranges.push(start..end);
            let offset = usize::try_from(physical_address)
                .map_err(|_| Error::new("PT_LOAD physical address does not fit host size"))?;
            let copy_end = offset
                .checked_add(memory_size)
                .ok_or_else(|| Error::new("PT_LOAD host memory range overflows"))?;
            ram[offset..copy_end].copy_from_slice(&segment.bytes);
        }

        segments.push(MainRamSegment {
            virtual_address: segment.virtual_address,
            physical_address,
            flags: segment.flags,
            file_size: segment.file_size,
            memory_size,
        });
    }

    let entry_physical = ee_main_ram_physical_address(image.entry_point).ok_or_else(|| {
        Error::new("ELF entry point is outside the supported EE main-RAM aliases")
    })?;
    let entry_is_file_backed_code = segments.iter().any(|segment| {
        segment.flags & PF_X != 0
            && image.entry_point >= segment.virtual_address
            && u64::from(image.entry_point)
                < u64::from(segment.virtual_address) + segment.file_size as u64
    });
    if !entry_is_file_backed_code {
        return Err(Error::new(
            "ELF entry point is not inside file-backed executable PT_LOAD data",
        ));
    }
    let entry_offset = usize::try_from(entry_physical)
        .map_err(|_| Error::new("ELF entry physical address does not fit host size"))?;
    if entry_offset
        .checked_add(4)
        .is_none_or(|end| end > ram.len())
    {
        return Err(Error::new(
            "ELF entry instruction extends beyond EE main RAM",
        ));
    }

    Ok(MainRamImage {
        entry_point: image.entry_point,
        bytes: ram,
        segments,
    })
}

pub fn load_segments(
    bytes: &[u8],
    image: &Image,
    maximum_total_memory: usize,
) -> Result<Vec<LoadedSegment>, Error> {
    let mut total = 0usize;
    let mut ranges: Vec<Range<u64>> = Vec::new();
    let mut segments: Vec<LoadedSegment> = Vec::new();

    for header in image
        .program_headers
        .iter()
        .filter(|header| header.kind == PT_LOAD)
    {
        if header.memory_size == 0 {
            // An empty segment allocates no memory and carries no file bytes.
            continue;
        }
        if header.file_size > header.memory_size {
            return Err(Error::new("ELF PT_LOAD file size exceeds memory size"));
        }
        if u64::from(header.virtual_address) + u64::from(header.memory_size)
            > (u64::from(u32::MAX) + 1)
        {
            return Err(Error::new(
                "ELF PT_LOAD virtual range overflows 32-bit address space",
            ));
        }
        let memory_size = usize::try_from(header.memory_size)
            .map_err(|_| Error::new("ELF segment memory size does not fit host size"))?;

        if header.file_size == 0 {
            // Pure BSS zero-fill segment. PS2 linkers such as SN Systems ProDG emit
            // multiple PT_LOAD entries with file_size == 0 sharing the same base
            // address to describe multiple BSS sections or overlays. Coalesce them
            // into the maximal BSS bounding range.
            if let Some(existing) = segments
                .iter_mut()
                .find(|s| s.virtual_address == header.virtual_address && s.file_size == 0)
            {
                if memory_size > existing.bytes.len() {
                    let additional = memory_size - existing.bytes.len();
                    total = total
                        .checked_add(additional)
                        .ok_or_else(|| Error::new("ELF loaded memory size overflow"))?;
                    if total > maximum_total_memory {
                        return Err(Error::new(
                            "ELF PT_LOAD memory exceeds configured load limit",
                        ));
                    }
                    existing.bytes.resize(memory_size, 0);
                    let start = u64::from(header.virtual_address);
                    let new_end = start + u64::from(header.memory_size);
                    for r in &mut ranges {
                        if r.start == start {
                            if new_end > r.end {
                                r.end = new_end;
                            }
                            break;
                        }
                    }
                }
                existing.flags |= header.flags;
                continue;
            }
        }

        total = total
            .checked_add(memory_size)
            .ok_or_else(|| Error::new("ELF loaded memory size overflow"))?;
        if total > maximum_total_memory {
            return Err(Error::new(
                "ELF PT_LOAD memory exceeds configured load limit",
            ));
        }
        let start = u64::from(header.virtual_address);
        let end = start + u64::from(header.memory_size);
        if ranges
            .iter()
            .any(|range| start < range.end && range.start < end)
        {
            return Err(Error::new("ELF PT_LOAD virtual memory ranges overlap"));
        }
        ranges.push(start..end);

        let file_range = checked_range(
            bytes,
            u64::from(header.offset),
            u64::from(header.file_size),
            "ELF PT_LOAD data",
        )?;
        let file_size = usize::try_from(header.file_size)
            .map_err(|_| Error::new("ELF segment file size does not fit host size"))?;
        let mut memory = vec![0; memory_size];
        memory[..file_size].copy_from_slice(&bytes[file_range]);
        segments.push(LoadedSegment {
            virtual_address: header.virtual_address,
            flags: header.flags,
            file_size,
            bytes: memory,
        });
    }

    segments.sort_by_key(|segment| segment.virtual_address);
    Ok(segments)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn put_u16(bytes: &mut [u8], offset: usize, value: u16) {
        bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
    }

    fn put_u32(bytes: &mut [u8], offset: usize, value: u32) {
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }

    fn minimal_elf() -> Vec<u8> {
        let mut bytes = vec![0; 112];
        bytes[0..4].copy_from_slice(b"\x7FELF");
        bytes[4] = 1;
        bytes[5] = 1;
        bytes[6] = 1;
        put_u16(&mut bytes, 16, ET_EXEC);
        put_u16(&mut bytes, 18, EM_MIPS);
        put_u32(&mut bytes, 20, 1);
        put_u32(&mut bytes, 24, 0x0010_0000);
        put_u32(&mut bytes, 28, 52);
        put_u16(&mut bytes, 40, 52);
        put_u16(&mut bytes, 42, 32);
        put_u16(&mut bytes, 44, 1);
        put_u16(&mut bytes, 46, 40);
        put_u32(&mut bytes, 52, PT_LOAD);
        put_u32(&mut bytes, 56, 96);
        put_u32(&mut bytes, 60, 0x0010_0000);
        put_u32(&mut bytes, 64, 0x0010_0000);
        put_u32(&mut bytes, 68, 8);
        put_u32(&mut bytes, 72, 16);
        put_u32(&mut bytes, 76, 5);
        put_u32(&mut bytes, 80, 1);
        bytes[96..104].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
        bytes
    }

    struct SectionSpec {
        name: u32,
        kind: u32,
        address: u32,
        offset: u32,
        size: u32,
        link: u32,
        info: u32,
        alignment: u32,
        entry_size: u32,
    }

    fn write_section(bytes: &mut [u8], base: usize, section: SectionSpec) {
        put_u32(bytes, base, section.name);
        put_u32(bytes, base + 4, section.kind);
        put_u32(bytes, base + 8, 0);
        put_u32(bytes, base + 12, section.address);
        put_u32(bytes, base + 16, section.offset);
        put_u32(bytes, base + 20, section.size);
        put_u32(bytes, base + 24, section.link);
        put_u32(bytes, base + 28, section.info);
        put_u32(bytes, base + 32, section.alignment);
        put_u32(bytes, base + 36, section.entry_size);
    }

    fn elf_with_mips_abi_metadata() -> Vec<u8> {
        let shstr = b"\0.shstrtab\0.reginfo\0.MIPS.abiflags\0";
        let section_table_end = ELF_HEADER_SIZE + (4 * SECTION_HEADER_SIZE);
        let shstr_offset = section_table_end;
        let reginfo_offset = shstr_offset + shstr.len();
        let abiflags_offset = reginfo_offset + MIPS_REGINFO_SIZE;
        let mut bytes = vec![0; abiflags_offset + MIPS_ABIFLAGS_V0_SIZE];
        let find_name = |name: &[u8]| -> u32 {
            shstr
                .windows(name.len())
                .position(|window| window == name)
                .expect("MIPS metadata section name exists") as u32
        };

        bytes[0..4].copy_from_slice(b"\x7FELF");
        bytes[4] = 1;
        bytes[5] = 1;
        bytes[6] = 1;
        put_u16(&mut bytes, 16, ET_EXEC);
        put_u16(&mut bytes, 18, EM_MIPS);
        put_u32(&mut bytes, 20, 1);
        put_u32(&mut bytes, 24, 0x0010_0000);
        put_u32(&mut bytes, 32, ELF_HEADER_SIZE as u32);
        put_u16(&mut bytes, 40, ELF_HEADER_SIZE as u16);
        put_u16(&mut bytes, 46, SECTION_HEADER_SIZE as u16);
        put_u16(&mut bytes, 48, 4);
        put_u16(&mut bytes, 50, 1);

        bytes[shstr_offset..shstr_offset + shstr.len()].copy_from_slice(shstr);
        put_u32(&mut bytes, reginfo_offset, 0x8000_0002);
        put_u32(&mut bytes, reginfo_offset + 4, 0x0000_0000);
        put_u32(&mut bytes, reginfo_offset + 8, 0x1234_5678);
        put_u32(&mut bytes, reginfo_offset + 12, 0x0000_0000);
        put_u32(&mut bytes, reginfo_offset + 16, 0x8000_0000);
        put_u32(&mut bytes, reginfo_offset + 20, 0x8012_3400);

        put_u16(&mut bytes, abiflags_offset, 0); // ABI flags version 0
        bytes[abiflags_offset + 2] = 3; // MIPS III
        bytes[abiflags_offset + 3] = 0; // revision 0
        bytes[abiflags_offset + 4] = 1; // 32-bit GPRs
        bytes[abiflags_offset + 5] = 1; // 32-bit CPR1
        bytes[abiflags_offset + 6] = 0; // no CPR2
        bytes[abiflags_offset + 7] = 3; // soft-float ABI
        put_u32(&mut bytes, abiflags_offset + 8, 6); // AFL_EXT_5900
        put_u32(&mut bytes, abiflags_offset + 12, 0x0002_0001); // DSP + GINV ASEs
        put_u32(&mut bytes, abiflags_offset + 16, 0xAABB_CCDD);
        put_u32(&mut bytes, abiflags_offset + 20, 0x1122_3344);

        write_section(
            &mut bytes,
            ELF_HEADER_SIZE + SECTION_HEADER_SIZE,
            SectionSpec {
                name: find_name(b".shstrtab"),
                kind: SHT_STRTAB,
                address: 0,
                offset: shstr_offset as u32,
                size: shstr.len() as u32,
                link: 0,
                info: 0,
                alignment: 1,
                entry_size: 0,
            },
        );
        write_section(
            &mut bytes,
            ELF_HEADER_SIZE + 2 * SECTION_HEADER_SIZE,
            SectionSpec {
                name: find_name(b".reginfo"),
                kind: SHT_MIPS_REGINFO,
                address: 0,
                offset: reginfo_offset as u32,
                size: MIPS_REGINFO_SIZE as u32,
                link: 0,
                info: 0,
                alignment: 4,
                entry_size: 0,
            },
        );
        write_section(
            &mut bytes,
            ELF_HEADER_SIZE + 3 * SECTION_HEADER_SIZE,
            SectionSpec {
                name: find_name(b".MIPS.abiflags"),
                kind: SHT_MIPS_ABIFLAGS,
                address: 0,
                offset: abiflags_offset as u32,
                size: MIPS_ABIFLAGS_V0_SIZE as u32,
                link: 0,
                info: 0,
                alignment: 4,
                entry_size: 0,
            },
        );
        bytes
    }

    fn elf_with_mips_abi_flags_program_header(
        version: u16,
        payload_size: usize,
        duplicate: bool,
    ) -> Vec<u8> {
        let program_header_count = if duplicate { 3usize } else { 2usize };
        let program_header_table_end = ELF_HEADER_SIZE + program_header_count * PROGRAM_HEADER_SIZE;
        let flags_offset = program_header_table_end;
        let load_offset = flags_offset + payload_size;
        let mut bytes = vec![0; load_offset + 4];

        bytes[0..4].copy_from_slice(&[0x7F, b'E', b'L', b'F']);
        bytes[4] = 1;
        bytes[5] = 1;
        bytes[6] = 1;
        put_u16(&mut bytes, 16, ET_EXEC);
        put_u16(&mut bytes, 18, EM_MIPS);
        put_u32(&mut bytes, 20, 1);
        put_u32(&mut bytes, 24, 0x0010_0000);
        put_u32(&mut bytes, 28, ELF_HEADER_SIZE as u32);
        put_u16(&mut bytes, 40, ELF_HEADER_SIZE as u16);
        put_u16(&mut bytes, 42, PROGRAM_HEADER_SIZE as u16);
        put_u16(&mut bytes, 44, program_header_count as u16);

        put_u16(&mut bytes, flags_offset, version);
        if version == 0 && payload_size >= MIPS_ABIFLAGS_V0_SIZE {
            bytes[flags_offset + 2] = 3; // MIPS III
            bytes[flags_offset + 3] = 0;
            bytes[flags_offset + 4] = 1; // GPR32
            bytes[flags_offset + 5] = 1; // CPR1 32-bit
            bytes[flags_offset + 6] = 0; // CPR2 unused
            bytes[flags_offset + 7] = 3; // soft-float
            put_u32(&mut bytes, flags_offset + 8, 6); // AFL_EXT_5900
            put_u32(&mut bytes, flags_offset + 12, 1); // DSP ASE
            put_u32(&mut bytes, flags_offset + 16, 0xAABB_CCDD);
            put_u32(&mut bytes, flags_offset + 20, 0x1122_3344);
        }

        let write_abiflags_header = |bytes: &mut [u8], index: usize| {
            let base = ELF_HEADER_SIZE + index * PROGRAM_HEADER_SIZE;
            put_u32(bytes, base, PT_MIPS_ABIFLAGS);
            put_u32(bytes, base + 4, flags_offset as u32);
            put_u32(bytes, base + 8, 0x0010_0000);
            put_u32(bytes, base + 12, 0x0010_0000);
            put_u32(bytes, base + 16, payload_size as u32);
            put_u32(bytes, base + 20, payload_size as u32);
            put_u32(bytes, base + 24, PF_R);
            put_u32(bytes, base + 28, 4);
        };
        let write_load_header = |bytes: &mut [u8], index: usize| {
            let base = ELF_HEADER_SIZE + index * PROGRAM_HEADER_SIZE;
            put_u32(bytes, base, PT_LOAD);
            put_u32(bytes, base + 4, load_offset as u32);
            put_u32(bytes, base + 8, 0x0020_0000);
            put_u32(bytes, base + 12, 0x0020_0000);
            put_u32(bytes, base + 16, 4);
            put_u32(bytes, base + 20, 4);
            put_u32(bytes, base + 24, PF_R | PF_X);
            put_u32(bytes, base + 28, 4);
        };

        if duplicate {
            write_abiflags_header(&mut bytes, 0);
            write_abiflags_header(&mut bytes, 1);
            write_load_header(&mut bytes, 2);
        } else {
            write_abiflags_header(&mut bytes, 0);
            write_load_header(&mut bytes, 1);
        }
        bytes
    }

    fn elf_with_mips_reginfo_program_headers(
        reginfo_after_load: bool,
        duplicate_reginfo: bool,
    ) -> Vec<u8> {
        let program_header_count = if duplicate_reginfo { 3usize } else { 2usize };
        let program_header_table_end = ELF_HEADER_SIZE + program_header_count * PROGRAM_HEADER_SIZE;
        let reginfo_offset = program_header_table_end;
        let load_offset = reginfo_offset + MIPS_REGINFO_SIZE;
        let mut bytes = vec![0; load_offset + 4];

        bytes[0..4].copy_from_slice(&[0x7F, b'E', b'L', b'F']);
        bytes[4] = 1;
        bytes[5] = 1;
        bytes[6] = 1;
        put_u16(&mut bytes, 16, ET_EXEC);
        put_u16(&mut bytes, 18, EM_MIPS);
        put_u32(&mut bytes, 20, 1);
        put_u32(&mut bytes, 24, 0x0010_0000);
        put_u32(&mut bytes, 28, ELF_HEADER_SIZE as u32);
        put_u16(&mut bytes, 40, ELF_HEADER_SIZE as u16);
        put_u16(&mut bytes, 42, PROGRAM_HEADER_SIZE as u16);
        put_u16(&mut bytes, 44, program_header_count as u16);

        let mut reginfo = vec![0; MIPS_REGINFO_SIZE];
        put_u32(&mut reginfo, 0, 0x8000_0002);
        put_u32(&mut reginfo, 4, 0x0000_0001);
        put_u32(&mut reginfo, 8, 0x1234_5678);
        put_u32(&mut reginfo, 12, 0x0000_0004);
        put_u32(&mut reginfo, 16, 0x8000_0000);
        put_u32(&mut reginfo, 20, 0x8012_3400);
        bytes[reginfo_offset..load_offset].copy_from_slice(&reginfo);
        bytes[load_offset..load_offset + 4].copy_from_slice(&[0, 0, 0, 0]);

        let write_reginfo_header = |bytes: &mut [u8], index: usize| {
            let base = ELF_HEADER_SIZE + index * PROGRAM_HEADER_SIZE;
            put_u32(bytes, base, PT_MIPS_REGINFO);
            put_u32(bytes, base + 4, reginfo_offset as u32);
            put_u32(bytes, base + 16, MIPS_REGINFO_SIZE as u32);
            put_u32(bytes, base + 20, MIPS_REGINFO_SIZE as u32);
            put_u32(bytes, base + 28, 4);
        };
        let write_load_header = |bytes: &mut [u8], index: usize| {
            let base = ELF_HEADER_SIZE + index * PROGRAM_HEADER_SIZE;
            put_u32(bytes, base, PT_LOAD);
            put_u32(bytes, base + 4, load_offset as u32);
            put_u32(bytes, base + 8, 0x0010_0000);
            put_u32(bytes, base + 12, 0x0010_0000);
            put_u32(bytes, base + 16, 4);
            put_u32(bytes, base + 20, 4);
            put_u32(bytes, base + 24, PF_R | PF_X);
            put_u32(bytes, base + 28, 4);
        };

        if reginfo_after_load {
            write_load_header(&mut bytes, 0);
            write_reginfo_header(&mut bytes, 1);
        } else {
            write_reginfo_header(&mut bytes, 0);
            if duplicate_reginfo {
                write_reginfo_header(&mut bytes, 1);
                write_load_header(&mut bytes, 2);
            } else {
                write_load_header(&mut bytes, 1);
            }
        }
        bytes
    }

    fn elf_with_mips_options() -> Vec<u8> {
        let shstr = b"\0.shstrtab\0.MIPS.options\0";
        const REGINFO_DESCRIPTOR_SIZE: usize = MIPS_OPTIONS_HEADER_SIZE + MIPS_OPTION_REGINFO_SIZE;
        const EXCEPTIONS_DESCRIPTOR_SIZE: usize = MIPS_OPTIONS_HEADER_SIZE;
        const UNKNOWN_DESCRIPTOR_SIZE: usize = MIPS_OPTIONS_HEADER_SIZE + 2;
        let options_size =
            REGINFO_DESCRIPTOR_SIZE + EXCEPTIONS_DESCRIPTOR_SIZE + UNKNOWN_DESCRIPTOR_SIZE;
        let section_table_end = ELF_HEADER_SIZE + 3 * SECTION_HEADER_SIZE;
        let shstr_offset = section_table_end;
        let options_offset = shstr_offset + shstr.len();
        let mut bytes = vec![0; options_offset + options_size];
        let find_name = |name: &[u8]| -> u32 {
            shstr
                .windows(name.len())
                .position(|window| window == name)
                .expect("MIPS options section name exists") as u32
        };

        bytes[0..4].copy_from_slice(b"\x7FELF");
        bytes[4] = 1;
        bytes[5] = 1;
        bytes[6] = 1;
        put_u16(&mut bytes, 16, ET_EXEC);
        put_u16(&mut bytes, 18, EM_MIPS);
        put_u32(&mut bytes, 20, 1);
        put_u32(&mut bytes, 24, 0x0010_0000);
        put_u32(&mut bytes, 32, ELF_HEADER_SIZE as u32);
        put_u16(&mut bytes, 40, ELF_HEADER_SIZE as u16);
        put_u16(&mut bytes, 46, SECTION_HEADER_SIZE as u16);
        put_u16(&mut bytes, 48, 3);
        put_u16(&mut bytes, 50, 1);

        bytes[shstr_offset..shstr_offset + shstr.len()].copy_from_slice(shstr);

        // ODK_REGINFO consists of the fixed eight-byte Elf_Options header
        // followed by the 24-byte ELF32_RegInfo payload.
        bytes[options_offset] = ODK_REGINFO;
        bytes[options_offset + 1] = REGINFO_DESCRIPTOR_SIZE as u8;
        put_u16(&mut bytes, options_offset + 2, 0); // global options
        put_u32(&mut bytes, options_offset + 4, 0xAABB_CCDD);
        put_u32(&mut bytes, options_offset + 8, 0x8000_0002);
        put_u32(&mut bytes, options_offset + 12, 0x0000_0000);
        put_u32(&mut bytes, options_offset + 16, 0x1234_5678);
        put_u32(&mut bytes, options_offset + 20, 0x0000_0000);
        put_u32(&mut bytes, options_offset + 24, 0x8000_0000);
        put_u32(&mut bytes, options_offset + 28, 0x8012_3400);

        let exceptions_offset = options_offset + REGINFO_DESCRIPTOR_SIZE;
        bytes[exceptions_offset] = ODK_EXCEPTIONS;
        bytes[exceptions_offset + 1] = EXCEPTIONS_DESCRIPTOR_SIZE as u8;
        put_u16(&mut bytes, exceptions_offset + 2, 2);
        put_u32(&mut bytes, exceptions_offset + 4, 0x0001_001F);

        let unknown_offset = exceptions_offset + EXCEPTIONS_DESCRIPTOR_SIZE;
        bytes[unknown_offset] = 0xFE;
        bytes[unknown_offset + 1] = UNKNOWN_DESCRIPTOR_SIZE as u8;
        put_u16(&mut bytes, unknown_offset + 2, 0);
        put_u32(&mut bytes, unknown_offset + 4, 0x1122_3344);
        bytes[unknown_offset + 8..unknown_offset + 10].copy_from_slice(&[0xDE, 0xAD]);

        write_section(
            &mut bytes,
            ELF_HEADER_SIZE + SECTION_HEADER_SIZE,
            SectionSpec {
                name: find_name(b".shstrtab"),
                kind: SHT_STRTAB,
                address: 0,
                offset: shstr_offset as u32,
                size: shstr.len() as u32,
                link: 0,
                info: 0,
                alignment: 1,
                entry_size: 0,
            },
        );
        write_section(
            &mut bytes,
            ELF_HEADER_SIZE + 2 * SECTION_HEADER_SIZE,
            SectionSpec {
                name: find_name(b".MIPS.options"),
                kind: SHT_MIPS_OPTIONS,
                address: 0,
                offset: options_offset as u32,
                size: options_size as u32,
                link: 0,
                info: 0,
                alignment: 4,
                entry_size: 0,
            },
        );
        bytes
    }

    fn elf_with_symbol_and_rel() -> Vec<u8> {
        let shstr = b"\0.shstrtab\0.strtab\0.symtab\0.text\0.rel.text\0";
        let strtab = b"\0entry\0";
        let find_name = |name: &[u8]| -> u32 {
            shstr
                .windows(name.len())
                .position(|window| window == name)
                .unwrap() as u32
        };
        let section_table_end = 52 + (6 * 40);
        let shstr_offset = section_table_end;
        let strtab_offset = shstr_offset + shstr.len();
        let symtab_offset = strtab_offset + strtab.len();
        let text_offset = symtab_offset + 32;
        let rel_offset = text_offset + 4;
        let mut bytes = vec![0; rel_offset + 8];

        bytes[0..4].copy_from_slice(b"\x7FELF");
        bytes[4] = 1;
        bytes[5] = 1;
        bytes[6] = 1;
        put_u16(&mut bytes, 16, ET_EXEC);
        put_u16(&mut bytes, 18, EM_MIPS);
        put_u32(&mut bytes, 20, 1);
        put_u32(&mut bytes, 24, 0x1000);
        put_u32(&mut bytes, 32, 52);
        put_u16(&mut bytes, 40, 52);
        put_u16(&mut bytes, 46, 40);
        put_u16(&mut bytes, 48, 6);
        put_u16(&mut bytes, 50, 1);

        bytes[shstr_offset..shstr_offset + shstr.len()].copy_from_slice(shstr);
        bytes[strtab_offset..strtab_offset + strtab.len()].copy_from_slice(strtab);
        bytes[text_offset..text_offset + 4].copy_from_slice(&[0, 0, 0, 0]);
        put_u32(&mut bytes, symtab_offset + 16, 1);
        put_u32(&mut bytes, symtab_offset + 20, 0x1000);
        put_u32(&mut bytes, symtab_offset + 24, 4);
        bytes[symtab_offset + 28] = 0x12;
        put_u16(&mut bytes, symtab_offset + 30, 4);
        put_u32(&mut bytes, rel_offset, 0x1000);
        put_u32(&mut bytes, rel_offset + 4, (1 << 8) | 2);

        write_section(
            &mut bytes,
            52 + 40,
            SectionSpec {
                name: find_name(b".shstrtab"),
                kind: SHT_STRTAB,
                address: 0,
                offset: shstr_offset as u32,
                size: shstr.len() as u32,
                link: 0,
                info: 0,
                alignment: 1,
                entry_size: 0,
            },
        );
        write_section(
            &mut bytes,
            52 + 80,
            SectionSpec {
                name: find_name(b".strtab"),
                kind: SHT_STRTAB,
                address: 0,
                offset: strtab_offset as u32,
                size: strtab.len() as u32,
                link: 0,
                info: 0,
                alignment: 1,
                entry_size: 0,
            },
        );
        write_section(
            &mut bytes,
            52 + 120,
            SectionSpec {
                name: find_name(b".symtab"),
                kind: SHT_SYMTAB,
                address: 0,
                offset: symtab_offset as u32,
                size: 32,
                link: 2,
                info: 1,
                alignment: 4,
                entry_size: 16,
            },
        );
        write_section(
            &mut bytes,
            52 + 160,
            SectionSpec {
                name: find_name(b".text"),
                kind: 1,
                address: 0x1000,
                offset: text_offset as u32,
                size: 4,
                link: 0,
                info: 0,
                alignment: 4,
                entry_size: 0,
            },
        );
        write_section(
            &mut bytes,
            52 + 200,
            SectionSpec {
                name: find_name(b".rel.text"),
                kind: SHT_REL,
                address: 0,
                offset: rel_offset as u32,
                size: 8,
                link: 3,
                info: 4,
                alignment: 4,
                entry_size: 8,
            },
        );
        bytes
    }

    fn elf_with_extended_symbol_indices() -> Vec<u8> {
        const SECTION_COUNT: usize = 0xFF02;
        const EXTENDED_TARGET_INDEX: usize = 0xFF00;
        const EXTENDED_SHSTRTAB_INDEX: usize = 0xFF01;
        const SECTION_TABLE_OFFSET: usize = ELF_HEADER_SIZE;
        let shstr =
            b"\0.strtab\0.symtab\0.text\0.rel.text\0.symtab_shndx\0.extended_target\0.shstrtab\0";
        let strtab = b"\0extended\0";
        let find_name = |name: &[u8]| -> u32 {
            shstr
                .windows(name.len())
                .position(|window| window == name)
                .expect("section name exists in fixture table") as u32
        };

        let section_table_end = SECTION_TABLE_OFFSET + SECTION_COUNT * SECTION_HEADER_SIZE;
        let strtab_offset = section_table_end;
        let symtab_offset = strtab_offset + strtab.len();
        let text_offset = symtab_offset + 2 * SYMBOL_SIZE;
        let rel_offset = text_offset + 4;
        let shndx_offset = rel_offset + REL_SIZE;
        let shstr_offset = shndx_offset + 2 * 4;
        let mut bytes = vec![0; shstr_offset + shstr.len()];

        bytes[0..4].copy_from_slice(b"\x7FELF");
        bytes[4] = 1;
        bytes[5] = 1;
        bytes[6] = 1;
        put_u16(&mut bytes, 16, ET_EXEC);
        put_u16(&mut bytes, 18, EM_MIPS);
        put_u32(&mut bytes, 20, 1);
        put_u32(&mut bytes, 24, 0x1000);
        put_u32(&mut bytes, 32, SECTION_TABLE_OFFSET as u32);
        put_u16(&mut bytes, 40, ELF_HEADER_SIZE as u16);
        put_u16(&mut bytes, 46, SECTION_HEADER_SIZE as u16);
        put_u16(&mut bytes, 48, 0); // e_shnum: use section zero's sh_size
        put_u16(&mut bytes, 50, SHN_XINDEX); // e_shstrndx: use section zero's sh_link

        bytes[strtab_offset..strtab_offset + strtab.len()].copy_from_slice(strtab);
        put_u32(&mut bytes, symtab_offset + SYMBOL_SIZE, 1);
        put_u32(&mut bytes, symtab_offset + SYMBOL_SIZE + 4, 0x1000);
        put_u32(&mut bytes, symtab_offset + SYMBOL_SIZE + 8, 4);
        bytes[symtab_offset + SYMBOL_SIZE + 12] = 0x12; // STB_GLOBAL | STT_FUNC
        put_u16(&mut bytes, symtab_offset + SYMBOL_SIZE + 14, SHN_XINDEX);
        bytes[text_offset..text_offset + 4].copy_from_slice(&[0, 0, 0, 0]);
        put_u32(&mut bytes, rel_offset, 0x1000);
        put_u32(&mut bytes, rel_offset + 4, (1 << 8) | 2);
        put_u32(&mut bytes, shndx_offset, 0);
        put_u32(&mut bytes, shndx_offset + 4, EXTENDED_TARGET_INDEX as u32);
        bytes[shstr_offset..shstr_offset + shstr.len()].copy_from_slice(shstr);

        // The null section header carries both extended-numbering values.
        put_u32(&mut bytes, SECTION_TABLE_OFFSET + 20, SECTION_COUNT as u32);
        put_u32(
            &mut bytes,
            SECTION_TABLE_OFFSET + 24,
            EXTENDED_SHSTRTAB_INDEX as u32,
        );

        let write_at = |bytes: &mut [u8], index: usize, section: SectionSpec| {
            write_section(
                bytes,
                SECTION_TABLE_OFFSET + index * SECTION_HEADER_SIZE,
                section,
            );
        };
        write_at(
            &mut bytes,
            1,
            SectionSpec {
                name: find_name(b".strtab"),
                kind: SHT_STRTAB,
                address: 0,
                offset: strtab_offset as u32,
                size: strtab.len() as u32,
                link: 0,
                info: 0,
                alignment: 1,
                entry_size: 0,
            },
        );
        write_at(
            &mut bytes,
            2,
            SectionSpec {
                name: find_name(b".symtab"),
                kind: SHT_SYMTAB,
                address: 0,
                offset: symtab_offset as u32,
                size: (2 * SYMBOL_SIZE) as u32,
                link: 1,
                info: 1,
                alignment: 4,
                entry_size: SYMBOL_SIZE as u32,
            },
        );
        write_at(
            &mut bytes,
            3,
            SectionSpec {
                name: find_name(b".text"),
                kind: 1,
                address: 0x1000,
                offset: text_offset as u32,
                size: 4,
                link: 0,
                info: 0,
                alignment: 4,
                entry_size: 0,
            },
        );
        write_at(
            &mut bytes,
            4,
            SectionSpec {
                name: find_name(b".rel.text"),
                kind: SHT_REL,
                address: 0,
                offset: rel_offset as u32,
                size: REL_SIZE as u32,
                link: 2,
                info: 3,
                alignment: 4,
                entry_size: REL_SIZE as u32,
            },
        );
        write_at(
            &mut bytes,
            5,
            SectionSpec {
                name: find_name(b".symtab_shndx"),
                kind: SHT_SYMTAB_SHNDX,
                address: 0,
                offset: shndx_offset as u32,
                size: 8,
                link: 2,
                info: 0,
                alignment: 4,
                entry_size: 4,
            },
        );
        for index in 6..EXTENDED_TARGET_INDEX {
            let base = SECTION_TABLE_OFFSET + index * SECTION_HEADER_SIZE;
            put_u32(&mut bytes, base + 4, SHT_NOBITS);
            put_u32(&mut bytes, base + 32, 1);
        }
        write_at(
            &mut bytes,
            EXTENDED_TARGET_INDEX,
            SectionSpec {
                name: find_name(b".extended_target"),
                kind: SHT_NOBITS,
                address: 0,
                offset: 0,
                size: 0,
                link: 0,
                info: 0,
                alignment: 1,
                entry_size: 0,
            },
        );
        write_at(
            &mut bytes,
            EXTENDED_SHSTRTAB_INDEX,
            SectionSpec {
                name: find_name(b".shstrtab"),
                kind: SHT_STRTAB,
                address: 0,
                offset: shstr_offset as u32,
                size: shstr.len() as u32,
                link: 0,
                info: 0,
                alignment: 1,
                entry_size: 0,
            },
        );
        bytes
    }

    fn elf_with_all_extended_numbering_fields() -> Vec<u8> {
        let original = elf_with_extended_symbol_indices();
        let original_section_offset =
            usize::try_from(read_u32(&original, 32).expect("section-header offset"))
                .expect("section-header offset fits host size");
        let section_count = usize::try_from(
            read_u32(&original, original_section_offset + 20).expect("extended section count"),
        )
        .expect("extended section count fits host size");
        let program_table_bytes = PN_XNUM as usize * PROGRAM_HEADER_SIZE;
        let program_table_shift =
            u32::try_from(program_table_bytes).expect("program-header table shift fits ELF32");
        let shifted_section_offset = original_section_offset + program_table_bytes;
        let mut bytes = vec![0; original.len() + program_table_bytes];
        bytes[..ELF_HEADER_SIZE].copy_from_slice(&original[..ELF_HEADER_SIZE]);
        bytes[ELF_HEADER_SIZE + program_table_bytes..]
            .copy_from_slice(&original[ELF_HEADER_SIZE..]);

        put_u32(&mut bytes, 28, ELF_HEADER_SIZE as u32);
        put_u32(
            &mut bytes,
            32,
            u32::try_from(shifted_section_offset).expect("section table fits ELF32"),
        );
        put_u16(&mut bytes, 42, PROGRAM_HEADER_SIZE as u16);
        put_u16(&mut bytes, 44, PN_XNUM);

        for index in 0..section_count {
            let original_header = original_section_offset + index * SECTION_HEADER_SIZE;
            let section_kind = read_u32(&original, original_header + 4).expect("section kind");
            if section_kind == SHT_NULL || section_kind == SHT_NOBITS {
                continue;
            }
            let old_data_offset =
                read_u32(&original, original_header + 16).expect("section data offset");
            if old_data_offset != 0 {
                let new_header = shifted_section_offset + index * SECTION_HEADER_SIZE;
                put_u32(
                    &mut bytes,
                    new_header + 16,
                    old_data_offset
                        .checked_add(program_table_shift)
                        .expect("shifted section offset fits ELF32"),
                );
            }
        }

        // The null section header now carries all three independent extended
        // values: sh_size (e_shnum), sh_link (e_shstrndx), and sh_info (e_phnum).
        put_u32(&mut bytes, shifted_section_offset + 28, u32::from(PN_XNUM));
        bytes
    }

    fn elf_with_extended_program_header_count() -> Vec<u8> {
        const PROGRAM_COUNT: usize = PN_XNUM as usize;
        const PROGRAM_TABLE_OFFSET: usize = ELF_HEADER_SIZE;
        const SECTION_TABLE_OFFSET: usize =
            PROGRAM_TABLE_OFFSET + PROGRAM_COUNT * PROGRAM_HEADER_SIZE;
        let mut bytes = vec![0; SECTION_TABLE_OFFSET + SECTION_HEADER_SIZE];

        bytes[0..4].copy_from_slice(&[0x7F, b'E', b'L', b'F']);
        bytes[4] = 1;
        bytes[5] = 1;
        bytes[6] = 1;
        put_u16(&mut bytes, 16, ET_EXEC);
        put_u16(&mut bytes, 18, EM_MIPS);
        put_u32(&mut bytes, 20, 1);
        put_u32(&mut bytes, 28, PROGRAM_TABLE_OFFSET as u32);
        put_u32(&mut bytes, 32, SECTION_TABLE_OFFSET as u32);
        put_u16(&mut bytes, 40, ELF_HEADER_SIZE as u16);
        put_u16(&mut bytes, 42, PROGRAM_HEADER_SIZE as u16);
        put_u16(&mut bytes, 44, PN_XNUM);
        put_u16(&mut bytes, 46, SECTION_HEADER_SIZE as u16);
        put_u16(&mut bytes, 48, 1);
        put_u16(&mut bytes, 50, 0);
        put_u32(&mut bytes, SECTION_TABLE_OFFSET + 28, PROGRAM_COUNT as u32);
        bytes
    }

    #[test]
    fn resolves_all_extended_numbering_fields_together() {
        let bytes = elf_with_all_extended_numbering_fields();
        let image = parse(&bytes, ParseLimits::default()).expect("combined extended-numbering ELF");

        assert_eq!(image.program_headers.len(), PN_XNUM as usize);
        assert_eq!(image.section_headers.len(), 0xFF02);
        assert_eq!(image.section_name_table_index, 0xFF01);
        assert_eq!(image.section_headers[0].size, 0xFF02);
        assert_eq!(image.section_headers[0].link, 0xFF01);
        assert_eq!(image.section_headers[0].info, u32::from(PN_XNUM));
        assert_eq!(image.symbols[1].section_index, 0xFF00);
        assert_eq!(image.section_headers[0xFF00].name, ".extended_target");
    }

    #[test]
    fn resolves_pn_xnum_through_section_header_zero() {
        let bytes = elf_with_extended_program_header_count();
        let image = parse(&bytes, ParseLimits::default()).expect("PN_XNUM fixture");
        assert_eq!(image.program_headers.len(), PN_XNUM as usize);
        assert_eq!(image.section_headers.len(), 1);
        assert_eq!(image.section_headers[0].info, u32::from(PN_XNUM));
    }

    #[test]
    fn rejects_invalid_pn_xnum_metadata_and_resource_limits() {
        let seed = elf_with_extended_program_header_count();

        let mut below_threshold = seed.clone();
        let section_zero_info_offset =
            ELF_HEADER_SIZE + PN_XNUM as usize * PROGRAM_HEADER_SIZE + 28;
        put_u32(
            &mut below_threshold,
            section_zero_info_offset,
            u32::from(PN_XNUM) - 1,
        );
        assert!(parse(&below_threshold, ParseLimits::default()).is_err());

        let mut non_extended_with_sh_info = seed.clone();
        put_u16(&mut non_extended_with_sh_info, 44, 1);
        assert!(parse(&non_extended_with_sh_info, ParseLimits::default()).is_err());

        let mut no_section_table = seed.clone();
        put_u32(&mut no_section_table, 32, 0);
        put_u16(&mut no_section_table, 48, 0);
        put_u16(&mut no_section_table, 50, 0);
        assert!(parse(&no_section_table, ParseLimits::default()).is_err());

        let limited = ParseLimits {
            max_program_headers: 100,
            ..ParseLimits::default()
        };
        assert!(parse(&seed, limited).is_err());
    }

    #[test]
    fn parses_pt_mips_abiflags_v0_and_identifies_the_r5900_extension() {
        let bytes = elf_with_mips_abi_flags_program_header(0, MIPS_ABIFLAGS_V0_SIZE, false);
        let image = parse(&bytes, ParseLimits::default()).expect("PT_MIPS_ABIFLAGS ELF");
        assert_eq!(image.mips_abi_flags_segments.len(), 1);

        let flags = &image.mips_abi_flags_segments[0];
        assert_eq!(flags.program_header_index, 0);
        assert_eq!(
            flags.file_offset as usize,
            ELF_HEADER_SIZE + 2 * PROGRAM_HEADER_SIZE
        );
        assert_eq!(flags.virtual_address, 0x0010_0000);
        assert_eq!(flags.segment_size, MIPS_ABIFLAGS_V0_SIZE as u32);
        assert_eq!(flags.version, 0);
        let decoded = flags.version_zero.expect("version zero is decoded");
        assert_eq!(decoded.isa_level_name(), "MIPS III");
        assert_eq!(decoded.isa_extension, 6);
        assert_eq!(decoded.isa_extension_name(), "R5900");
        assert_eq!(decoded.fp_abi_name(), "soft-float");
        assert_eq!(decoded.ase_names(), vec!["DSP"]);
        assert_eq!(decoded.flags1, 0xAABB_CCDD);
        assert_eq!(decoded.flags2, 0x1122_3344);
    }

    #[test]
    fn preserves_unknown_pt_mips_abiflags_versions_and_rejects_malformed_records() {
        let unknown = elf_with_mips_abi_flags_program_header(2, 4, false);
        let image = parse(&unknown, ParseLimits::default()).expect("unknown version is retained");
        assert_eq!(image.mips_abi_flags_segments.len(), 1);
        assert_eq!(image.mips_abi_flags_segments[0].version, 2);
        assert_eq!(image.mips_abi_flags_segments[0].segment_size, 4);
        assert!(image.mips_abi_flags_segments[0].version_zero.is_none());

        let mut short = elf_with_mips_abi_flags_program_header(0, 20, false);
        let flags_header = ELF_HEADER_SIZE;
        let short_error =
            parse(&short, ParseLimits::default()).expect_err("v0 is exactly 24 bytes");
        assert!(short_error.to_string().contains("exactly 24 bytes"));
        put_u32(&mut short, flags_header + 16, 1);
        let too_small = parse(&short, ParseLimits::default())
            .expect_err("payload cannot omit the version-zero record");
        assert!(too_small.to_string().contains("too small"));
    }

    #[test]
    fn rejects_duplicate_pt_mips_abiflags_and_ignores_non_mips_payloads() {
        let duplicate = elf_with_mips_abi_flags_program_header(0, MIPS_ABIFLAGS_V0_SIZE, true);
        let duplicate_error =
            parse(&duplicate, ParseLimits::default()).expect_err("PT_MIPS_ABIFLAGS is unique");
        assert!(
            duplicate_error
                .to_string()
                .contains("more than one PT_MIPS_ABIFLAGS")
        );

        let mut non_mips = elf_with_mips_abi_flags_program_header(0, MIPS_ABIFLAGS_V0_SIZE, false);
        put_u16(&mut non_mips, 18, 3);
        let flags_header = ELF_HEADER_SIZE;
        put_u32(&mut non_mips, flags_header + 16, 1);
        let image = parse(&non_mips, ParseLimits::default())
            .expect("non-MIPS ELF leaves the MIPS flags segment opaque");
        assert!(image.mips_abi_flags_segments.is_empty());
    }

    #[test]
    fn parses_pt_mips_reginfo_payload_before_loadable_segments() {
        let bytes = elf_with_mips_reginfo_program_headers(false, false);
        let image = parse(&bytes, ParseLimits::default()).expect("PT_MIPS_REGINFO ELF");
        assert_eq!(image.mips_reginfo_segments.len(), 1);

        let reginfo = &image.mips_reginfo_segments[0];
        assert_eq!(reginfo.program_header_index, 0);
        assert_eq!(
            reginfo.file_offset as usize,
            ELF_HEADER_SIZE + 2 * PROGRAM_HEADER_SIZE
        );
        assert_eq!(reginfo.virtual_address, 0);
        assert_eq!(reginfo.gpr_mask, 0x8000_0002);
        assert_eq!(
            reginfo.coprocessor_masks,
            [0x0000_0001, 0x1234_5678, 0x0000_0004, 0x8000_0000]
        );
        assert_eq!(reginfo.gp_value, 0x8012_3400u32 as i32);
    }

    #[test]
    fn rejects_duplicate_late_and_malformed_pt_mips_reginfo_segments() {
        let duplicate = elf_with_mips_reginfo_program_headers(false, true);
        let duplicate_error =
            parse(&duplicate, ParseLimits::default()).expect_err("PT_MIPS_REGINFO is unique");
        assert!(
            duplicate_error
                .to_string()
                .contains("more than one PT_MIPS_REGINFO")
        );

        let late = elf_with_mips_reginfo_program_headers(true, false);
        let late_error =
            parse(&late, ParseLimits::default()).expect_err("REGINFO precedes PT_LOAD");
        assert!(
            late_error
                .to_string()
                .contains("must precede every PT_LOAD")
        );

        let mut short = elf_with_mips_reginfo_program_headers(false, false);
        let first_header = ELF_HEADER_SIZE;
        put_u32(
            &mut short,
            first_header + 16,
            (MIPS_REGINFO_SIZE - 1) as u32,
        );
        let short_error =
            parse(&short, ParseLimits::default()).expect_err("REGINFO is exactly 24 bytes");
        assert!(
            short_error
                .to_string()
                .contains("one 24-byte ELF32_RegInfo")
        );
    }

    #[test]
    fn ignores_mips_reginfo_program_header_payload_for_non_mips_objects() {
        let mut bytes = elf_with_mips_reginfo_program_headers(false, false);
        put_u16(&mut bytes, 18, 3); // EM_386
        put_u32(
            &mut bytes,
            ELF_HEADER_SIZE + 16,
            1, // Deliberately not an ELF32_RegInfo-sized payload.
        );
        let image = parse(&bytes, ParseLimits::default())
            .expect("non-MIPS ELF leaves PT_MIPS_REGINFO opaque");
        assert!(image.mips_reginfo_segments.is_empty());
    }

    #[test]
    fn parses_mips_options_descriptors_and_elf32_reginfo_payload() {
        let bytes = elf_with_mips_options();
        let image = parse(&bytes, ParseLimits::default()).expect("MIPS options fixture");
        assert_eq!(image.mips_options.len(), 1);

        let section = &image.mips_options[0];
        assert_eq!(section.section_index, 2);
        assert_eq!(section.descriptors.len(), 3);

        let reginfo = &section.descriptors[0];
        assert_eq!(reginfo.kind, ODK_REGINFO);
        assert_eq!(reginfo.kind_name(), "REGINFO");
        assert_eq!(
            reginfo.size as usize,
            MIPS_OPTIONS_HEADER_SIZE + MIPS_OPTION_REGINFO_SIZE
        );
        assert_eq!(reginfo.section_index, 0);
        assert_eq!(reginfo.info, 0xAABB_CCDD);
        assert_eq!(reginfo.payload_size as usize, MIPS_OPTION_REGINFO_SIZE);
        let registers = reginfo.register_info.as_ref().expect("ODK_REGINFO payload");
        assert_eq!(registers.gpr_mask, 0x8000_0002);
        assert_eq!(
            registers.coprocessor_masks,
            [0, 0x1234_5678, 0, 0x8000_0000]
        );
        assert_eq!(registers.gp_value, 0x8012_3400u32 as i32);

        let exceptions = &section.descriptors[1];
        assert_eq!(exceptions.kind, ODK_EXCEPTIONS);
        assert_eq!(exceptions.kind_name(), "EXCEPTIONS");
        assert_eq!(exceptions.section_index, 2);
        assert_eq!(exceptions.info, 0x0001_001F);
        assert_eq!(exceptions.payload_size, 0);
        assert!(exceptions.register_info.is_none());

        let unknown = &section.descriptors[2];
        assert_eq!(unknown.kind, 0xFE);
        assert_eq!(unknown.kind_name(), "unknown");
        assert_eq!(unknown.size, 10);
        assert_eq!(unknown.section_index, 0);
        assert_eq!(unknown.info, 0x1122_3344);
        assert_eq!(unknown.payload_size, 2);
        assert!(unknown.register_info.is_none());
    }

    #[test]
    fn rejects_malformed_mips_options_descriptor_boundaries_and_links() {
        let seed = elf_with_mips_options();
        let options_header = ELF_HEADER_SIZE + 2 * SECTION_HEADER_SIZE;
        let options_offset =
            read_u32(&seed, options_header + 16).expect("MIPS options file offset") as usize;

        let mut too_small = seed.clone();
        too_small[options_offset + 1] = (MIPS_OPTIONS_HEADER_SIZE - 1) as u8;
        assert!(parse(&too_small, ParseLimits::default()).is_err());

        let mut extends_past_section = seed.clone();
        extends_past_section[options_offset + 1] = u8::MAX;
        assert!(parse(&extends_past_section, ParseLimits::default()).is_err());

        let mut short_reginfo = seed.clone();
        short_reginfo[options_offset + 1] =
            (MIPS_OPTIONS_HEADER_SIZE + MIPS_OPTION_REGINFO_SIZE - 1) as u8;
        assert!(parse(&short_reginfo, ParseLimits::default()).is_err());

        let mut invalid_target_section = seed.clone();
        put_u16(&mut invalid_target_section, options_offset + 2, 3);
        assert!(parse(&invalid_target_section, ParseLimits::default()).is_err());

        let mut truncated_header = seed;
        put_u32(
            &mut truncated_header,
            options_header + 20,
            (MIPS_OPTIONS_HEADER_SIZE + MIPS_OPTION_REGINFO_SIZE + MIPS_OPTIONS_HEADER_SIZE + 5)
                as u32,
        );
        assert!(parse(&truncated_header, ParseLimits::default()).is_err());
    }

    #[test]
    fn applies_mips_options_descriptor_limit_and_ignores_non_mips_payloads() {
        let seed = elf_with_mips_options();
        let limited = ParseLimits {
            max_mips_option_descriptors: 2,
            ..ParseLimits::default()
        };
        let error = parse(&seed, limited).expect_err("descriptor limit");
        assert!(
            error
                .to_string()
                .contains("MIPS option descriptors exceed configured parser limit")
        );

        let mut non_mips = seed;
        put_u16(&mut non_mips, 18, 3);
        let options_header = ELF_HEADER_SIZE + 2 * SECTION_HEADER_SIZE;
        let options_offset =
            read_u32(&non_mips, options_header + 16).expect("MIPS options offset") as usize;
        non_mips[options_offset + 1] = 0;
        let image = parse(&non_mips, ParseLimits::default())
            .expect("non-MIPS file does not interpret MIPS option descriptors");
        assert!(image.mips_options.is_empty());
    }

    #[test]
    fn parses_mips_reginfo_register_masks_and_global_pointer() {
        let bytes = elf_with_mips_abi_metadata();
        let image = parse(&bytes, ParseLimits::default()).expect("MIPS .reginfo fixture");
        assert_eq!(image.mips_reginfo.len(), 1);
        let reginfo = &image.mips_reginfo[0];
        assert_eq!(reginfo.section_index, 2);
        assert_eq!(reginfo.gpr_mask, 0x8000_0002);
        assert_eq!(
            reginfo.coprocessor_masks,
            [0x0000_0000, 0x1234_5678, 0x0000_0000, 0x8000_0000]
        );
        assert_eq!(
            reginfo.gp_value,
            i32::from_ne_bytes(0x8012_3400u32.to_ne_bytes())
        );
    }

    #[test]
    fn parses_mips_abiflags_v0_and_identifies_the_r5900_extension() {
        let bytes = elf_with_mips_abi_metadata();
        let image = parse(&bytes, ParseLimits::default()).expect("MIPS ABI flags fixture");
        assert_eq!(image.mips_abi_flags.len(), 1);
        let flags = &image.mips_abi_flags[0];
        assert_eq!(flags.section_index, 3);
        assert_eq!(flags.section_size, MIPS_ABIFLAGS_V0_SIZE as u32);
        assert_eq!(flags.version, 0);
        let decoded = flags.version_zero.expect("version zero is decoded");
        assert_eq!(decoded.isa_level, 3);
        assert_eq!(decoded.isa_level_name(), "MIPS III");
        assert_eq!(decoded.isa_revision, 0);
        assert_eq!(
            MipsAbiFlagsV0::register_size_name(decoded.gpr_size),
            "32-bit"
        );
        assert_eq!(MipsAbiFlagsV0::register_size_name(0), "none");
        assert_eq!(decoded.fp_abi_name(), "soft-float");
        assert_eq!(decoded.isa_extension, 6);
        assert_eq!(decoded.isa_extension_name(), "R5900");
        assert_eq!(decoded.ase_names(), vec!["DSP", "GINV"]);
        assert_eq!(decoded.unknown_ases(), 0);
        assert_eq!(decoded.flags1, 0xAABB_CCDD);
        assert!(decoded.has_odd_single_precision_registers());
        assert_eq!(decoded.unknown_flags1(), 0xAABB_CCDC);
        assert_eq!(decoded.flags2, 0x1122_3344);

        let mut no_odd_single_registers = decoded;
        no_odd_single_registers.flags1 = 0;
        assert!(!no_odd_single_registers.has_odd_single_precision_registers());
        assert_eq!(no_odd_single_registers.unknown_flags1(), 0);

        let mut unknown_ase = decoded;
        unknown_ase.ases |= 0x8000_0000;
        assert_eq!(unknown_ase.unknown_ases(), 0x8000_0000);
    }

    #[test]
    fn retains_unknown_mips_abiflags_versions_without_interpreting_them_as_v0() {
        let mut bytes = elf_with_mips_abi_metadata();
        let abiflags_header = ELF_HEADER_SIZE + 3 * SECTION_HEADER_SIZE;
        let abiflags_offset = read_u32(&bytes, abiflags_header + 16).expect("ABI flags offset");
        put_u16(&mut bytes, abiflags_offset as usize, 7);
        let image = parse(&bytes, ParseLimits::default()).expect("unknown ABI flags version");
        assert_eq!(image.mips_abi_flags.len(), 1);
        assert_eq!(image.mips_abi_flags[0].version, 7);
        assert_eq!(image.mips_abi_flags[0].section_size, 24);
        assert_eq!(image.mips_abi_flags[0].version_zero, None);
    }

    #[test]
    fn rejects_malformed_mips_reginfo_and_version_zero_abiflags_sizes() {
        let seed = elf_with_mips_abi_metadata();
        let reginfo_header = ELF_HEADER_SIZE + 2 * SECTION_HEADER_SIZE;
        let abiflags_header = ELF_HEADER_SIZE + 3 * SECTION_HEADER_SIZE;

        let mut short_reginfo = seed.clone();
        put_u32(&mut short_reginfo, reginfo_header + 20, 20);
        assert!(parse(&short_reginfo, ParseLimits::default()).is_err());

        let mut short_v0 = seed.clone();
        put_u32(&mut short_v0, abiflags_header + 20, 20);
        assert!(parse(&short_v0, ParseLimits::default()).is_err());

        let mut duplicate_abi_flags = seed.clone();
        let reginfo_header = ELF_HEADER_SIZE + 2 * SECTION_HEADER_SIZE;
        put_u32(
            &mut duplicate_abi_flags,
            reginfo_header + 4,
            SHT_MIPS_ABIFLAGS,
        );
        let duplicate_error = parse(&duplicate_abi_flags, ParseLimits::default())
            .expect_err("duplicate ABI flags sections are ambiguous");
        assert!(
            duplicate_error
                .to_string()
                .contains("more than one SHT_MIPS_ABIFLAGS")
        );

        let mut truncated_reginfo = seed;
        let reginfo_offset =
            read_u32(&truncated_reginfo, reginfo_header + 16).expect("reginfo file offset");
        put_u32(&mut truncated_reginfo, reginfo_header + 16, u32::MAX);
        assert!(parse(&truncated_reginfo, ParseLimits::default()).is_err());
        assert_ne!(reginfo_offset, u32::MAX);
    }

    #[test]
    fn ignores_mips_specific_section_payloads_for_non_mips_machine_ids() {
        let mut bytes = elf_with_mips_abi_metadata();
        put_u16(&mut bytes, 18, 3);
        let image =
            parse(&bytes, ParseLimits::default()).expect("non-MIPS ELF retains opaque sections");
        assert!(image.mips_reginfo.is_empty());
        assert!(image.mips_abi_flags.is_empty());
    }

    #[test]
    fn resolves_section_names_symbols_and_rel_entries() {
        let bytes = elf_with_symbol_and_rel();
        let image = parse(&bytes, ParseLimits::default()).expect("synthetic symbol ELF");
        assert_eq!(image.section_headers[4].name, ".text");
        assert_eq!(image.symbols.len(), 2);
        assert_eq!(image.symbols[1].name, "entry");
        assert_eq!(image.symbols[1].value, 0x1000);
        assert_eq!(image.relocations.len(), 1);
        assert_eq!(image.relocations[0].symbol_index, 1);
        assert_eq!(image.relocations[0].relocation_type, 2);
        assert_eq!(image.relocations[0].addend, None);
    }

    #[test]
    fn resolves_extended_symbol_indices_and_extended_section_names() {
        let bytes = elf_with_extended_symbol_indices();
        let image = parse(&bytes, ParseLimits::default()).expect("extended-numbering ELF");
        assert_eq!(image.section_headers.len(), 0xFF02);
        assert_eq!(image.section_name_table_index, 0xFF01);
        assert_eq!(image.section_headers[0xFF00].name, ".extended_target");
        assert_eq!(image.section_headers[0xFF01].name, ".shstrtab");
        assert_eq!(image.symbols.len(), 2);
        assert_eq!(image.symbols[1].section_index, 0xFF00);
        assert_eq!(image.relocations.len(), 1);
        assert_eq!(image.relocations[0].target_section_index, 3);
    }

    #[test]
    fn accepts_zero_extended_slots_for_symbols_with_regular_section_indices() {
        let mut bytes = elf_with_extended_symbol_indices();
        let image = parse(&bytes, ParseLimits::default()).expect("extended fixture");
        let shndx_data = image.section_headers[5].offset as usize;
        let symbol_data = image.section_headers[2].offset as usize;
        put_u16(&mut bytes, symbol_data + SYMBOL_SIZE + 14, 3);
        put_u32(&mut bytes, shndx_data + 4, 0);

        let parsed = parse(&bytes, ParseLimits::default())
            .expect("an unused extended-index table with zero slots is valid");
        assert_eq!(parsed.symbols[1].section_index, 3);
    }

    #[test]
    fn rejects_shn_xindex_without_its_linked_table() {
        let mut bytes = elf_with_extended_symbol_indices();
        let section_header_base = ELF_HEADER_SIZE + 5 * SECTION_HEADER_SIZE;
        put_u32(&mut bytes, section_header_base + 4, SHT_NOBITS);
        assert!(parse(&bytes, ParseLimits::default()).is_err());
    }

    #[test]
    fn rejects_malformed_symtab_shndx_links_and_entry_shapes() {
        let seed = elf_with_extended_symbol_indices();
        let shndx_header = ELF_HEADER_SIZE + 5 * SECTION_HEADER_SIZE;

        let mut wrong_link = seed.clone();
        put_u32(&mut wrong_link, shndx_header + 24, 1); // .strtab, not .symtab
        assert!(parse(&wrong_link, ParseLimits::default()).is_err());

        let mut wrong_entry_size = seed.clone();
        put_u32(&mut wrong_entry_size, shndx_header + 36, 8);
        assert!(parse(&wrong_entry_size, ParseLimits::default()).is_err());

        let mut wrong_info = seed.clone();
        put_u32(&mut wrong_info, shndx_header + 28, 1);
        assert!(parse(&wrong_info, ParseLimits::default()).is_err());

        let mut wrong_count = seed;
        put_u32(&mut wrong_count, shndx_header + 20, 4);
        assert!(parse(&wrong_count, ParseLimits::default()).is_err());
    }

    #[test]
    fn rejects_invalid_or_inconsistent_extended_symbol_index_entries() {
        let seed = elf_with_extended_symbol_indices();
        let image = parse(&seed, ParseLimits::default()).expect("extended fixture");
        let shndx_data = image.section_headers[5].offset as usize;
        let symbol_data = image.section_headers[2].offset as usize;

        let mut out_of_range = seed.clone();
        put_u32(&mut out_of_range, shndx_data + 4, 0xFF02);
        assert!(parse(&out_of_range, ParseLimits::default()).is_err());

        let mut below_reserved_range = seed.clone();
        put_u32(&mut below_reserved_range, shndx_data + 4, 3);
        assert!(parse(&below_reserved_range, ParseLimits::default()).is_err());

        let mut non_extended_symbol_has_nonzero_slot = seed;
        put_u16(
            &mut non_extended_symbol_has_nonzero_slot,
            symbol_data + SYMBOL_SIZE + 14,
            3,
        );
        assert!(
            parse(
                &non_extended_symbol_has_nonzero_slot,
                ParseLimits::default()
            )
            .is_err()
        );
    }

    #[test]
    fn rejects_duplicate_symtab_shndx_tables_and_extended_section_count_over_limit() {
        let seed = elf_with_extended_symbol_indices();
        let mut duplicate = seed.clone();
        let duplicate_header = ELF_HEADER_SIZE + 7 * SECTION_HEADER_SIZE;
        put_u32(&mut duplicate, duplicate_header + 4, SHT_SYMTAB_SHNDX);
        put_u32(&mut duplicate, duplicate_header + 24, 2);
        assert!(parse(&duplicate, ParseLimits::default()).is_err());

        let limited = ParseLimits {
            max_sections: 100,
            ..ParseLimits::default()
        };
        assert!(parse(&seed, limited).is_err());
    }

    #[test]
    fn rejects_invalid_extended_section_numbering_sentinels() {
        let seed = elf_with_extended_symbol_indices();
        let mut small_count = seed.clone();
        put_u32(&mut small_count, ELF_HEADER_SIZE + 20, 6);
        assert!(parse(&small_count, ParseLimits::default()).is_err());

        let mut small_name_index = seed.clone();
        put_u32(&mut small_name_index, ELF_HEADER_SIZE + 24, 1);
        assert!(parse(&small_name_index, ParseLimits::default()).is_err());

        let mut nonextended_reserved_count = seed;
        put_u16(&mut nonextended_reserved_count, 48, 0xFF02);
        assert!(parse(&nonextended_reserved_count, ParseLimits::default()).is_err());
    }

    #[test]
    fn rejects_a_nonzero_undefined_symbol_entry() {
        let mut malformed = elf_with_symbol_and_rel();
        let image = parse(&malformed, ParseLimits::default()).expect("symbol fixture");
        let table_offset = image.section_headers[3].offset as usize;
        put_u32(&mut malformed, table_offset + 4, 1);
        assert!(parse(&malformed, ParseLimits::default()).is_err());
    }

    #[test]
    fn validates_string_table_leading_and_trailing_null_bytes() {
        let seed = elf_with_symbol_and_rel();
        let image = parse(&seed, ParseLimits::default()).expect("well-formed table fixture");

        for section_index in [1usize, 2usize] {
            let section = &image.section_headers[section_index];
            assert_eq!(section.kind, SHT_STRTAB);
            assert!(section.size > 1);
            for relative_offset in [0usize, section.size as usize - 1] {
                let mut malformed = seed.clone();
                let offset = section.offset as usize + relative_offset;
                malformed[offset] = b'X';
                assert!(
                    parse(&malformed, ParseLimits::default()).is_err(),
                    "string table {} accepted a non-null sentinel at {}",
                    section_index,
                    relative_offset
                );
            }
        }
    }

    #[test]
    fn validates_the_null_section_header_entry() {
        let seed = elf_with_symbol_and_rel();
        let mut malformed = seed.clone();
        put_u32(&mut malformed, 52 + 4, 1);
        assert!(parse(&malformed, ParseLimits::default()).is_err());

        let mut malformed = seed;
        put_u32(&mut malformed, 52 + 8, 0x2);
        assert!(parse(&malformed, ParseLimits::default()).is_err());
    }

    #[test]
    fn validates_symbol_section_indices_and_local_symbol_boundary() {
        let seed = elf_with_symbol_and_rel();
        let image = parse(&seed, ParseLimits::default()).expect("well-formed symbol fixture");
        let symbol_table = &image.section_headers[3];
        let symbol_table_offset = symbol_table.offset as usize;
        let symbol_table_header = 52 + 3 * SECTION_HEADER_SIZE;

        let mut bad_section_index = seed.clone();
        put_u16(
            &mut bad_section_index,
            symbol_table_offset + SYMBOL_SIZE + 14,
            6,
        );
        assert!(parse(&bad_section_index, ParseLimits::default()).is_err());

        let mut special_section_index = seed.clone();
        put_u16(
            &mut special_section_index,
            symbol_table_offset + SYMBOL_SIZE + 14,
            0xFFF1,
        );
        let parsed = parse(&special_section_index, ParseLimits::default())
            .expect("SHN_ABS is a valid reserved symbol section index");
        assert_eq!(parsed.symbols[1].section_index, 0xFFF1);

        let mut boundary_out_of_range = seed.clone();
        put_u32(&mut boundary_out_of_range, symbol_table_header + 28, 3);
        assert!(parse(&boundary_out_of_range, ParseLimits::default()).is_err());

        let mut boundary_misclassifies_global = seed;
        put_u32(
            &mut boundary_misclassifies_global,
            symbol_table_header + 28,
            2,
        );
        assert!(parse(&boundary_misclassifies_global, ParseLimits::default()).is_err());
    }

    #[test]
    fn accepts_an_empty_symbol_table_with_zero_local_boundary() {
        // Stripped retail MIPS images can carry a SHT_SYMTAB whose sh_size is
        // zero. ELF requires sh_info to be zero for such a table and there is
        // no null symbol entry to validate.
        let seed = elf_with_symbol_and_rel();
        let mut empty = seed;
        let symbol_table_header = 52 + 3 * SECTION_HEADER_SIZE;
        put_u32(&mut empty, symbol_table_header + 20, 0);
        put_u32(&mut empty, symbol_table_header + 28, 0);
        let relocation_header = 52 + 5 * SECTION_HEADER_SIZE;
        put_u32(&mut empty, relocation_header + 20, 0);

        let image = parse(&empty, ParseLimits::default())
            .expect("an empty SHT_SYMTAB with sh_info zero is well formed");
        assert!(image.symbols.is_empty());
        assert!(image.relocations.is_empty());
        assert_eq!(image.section_headers[3].size, 0);
    }

    #[test]
    fn rejects_an_empty_symbol_table_with_nonzero_local_boundary() {
        let seed = elf_with_symbol_and_rel();
        let mut malformed = seed;
        let symbol_table_header = 52 + 3 * SECTION_HEADER_SIZE;
        put_u32(&mut malformed, symbol_table_header + 20, 0);
        put_u32(&mut malformed, symbol_table_header + 28, 1);
        let relocation_header = 52 + 5 * SECTION_HEADER_SIZE;
        put_u32(&mut malformed, relocation_header + 20, 0);

        assert!(parse(&malformed, ParseLimits::default()).is_err());
    }

    #[test]
    fn decodes_the_r5900_ee_header_flag_example() {
        // PCSX2's ELF_HEADER comment records 0x20924001 as a common example.
        // This is an e_flags decode fixture, not a retail-game compatibility claim.
        let decoded = decode_mips_elf_flags(0x2092_4001);
        assert_eq!(decoded.cpu_code, 0x92);
        assert_eq!(decoded.cpu_name(), "R5900");
        assert_eq!(decoded.abi_code, 0x4000);
        assert_eq!(decoded.abi_name(), "EABI64");
        assert_eq!(decoded.architecture_code, 2);
        assert_eq!(decoded.architecture_name(), "MIPS III");
        assert_eq!(decoded.feature_names(), vec!["noreorder"]);
        assert_eq!(decoded.unknown_bits, 0);
    }

    #[test]
    fn preserves_unrecognized_mips_flag_fields_and_bits() {
        let raw = 0x9000_0000 | (0xAA << 16) | 0xD000 | 0x0000_0801;
        let decoded = decode_mips_elf_flags(raw);
        assert_eq!(decoded.cpu_code, 0xAA);
        assert_eq!(decoded.cpu_name(), "unrecognized");
        assert_eq!(decoded.abi_code, 0xD000);
        assert_eq!(decoded.abi_name(), "unrecognized");
        assert_eq!(decoded.architecture_code, 9);
        assert_eq!(decoded.architecture_name(), "unrecognized");
        assert_eq!(decoded.feature_names(), vec!["noreorder"]);
        assert_eq!(decoded.unknown_bits, 0x0000_0800);
        assert_eq!(decoded.raw, raw);
    }

    #[test]
    fn does_not_decode_mips_flags_for_non_mips_machines() {
        let image = Image {
            file_type: ET_EXEC,
            os_abi: 0,
            abi_version: 0,
            machine: 3,
            entry_point: 0,
            flags: 0x2092_4001,
            section_name_table_index: 0,
            program_headers: Vec::new(),
            section_headers: Vec::new(),
            symbols: Vec::new(),
            relocations: Vec::new(),
            mips_reginfo: Vec::new(),
            mips_reginfo_segments: Vec::new(),
            mips_abi_flags_segments: Vec::new(),
            mips_abi_flags: Vec::new(),
            mips_options: Vec::new(),
        };
        assert_eq!(image.mips_elf_flags(), None);
    }

    #[test]
    fn preserves_osabi_and_abi_version_bytes() {
        let mut bytes = minimal_elf();
        bytes[7] = 9; // FreeBSD
        bytes[8] = 3; // raw ABI version, interpreted under OSABI 9
        let image = parse(&bytes, ParseLimits::default()).expect("ELF with OS ABI metadata");
        assert_eq!(image.os_abi, 9);
        assert_eq!(image.abi_version, 3);
        assert_eq!(image.os_abi_name(), "FreeBSD");
        assert_eq!(image.file_type_name(), "executable");
    }

    #[test]
    fn labels_architecture_specific_and_unrecognized_osabi_values_without_rejection() {
        let mut architecture_specific = minimal_elf();
        architecture_specific[7] = 97;
        architecture_specific[8] = 0xA5;
        let image = parse(&architecture_specific, ParseLimits::default())
            .expect("architecture-specific OS ABI remains parseable");
        assert_eq!(image.os_abi, 97);
        assert_eq!(image.abi_version, 0xA5);
        assert_eq!(image.os_abi_name(), "architecture-specific");

        let mut unrecognized = minimal_elf();
        unrecognized[7] = 63;
        unrecognized[8] = 0xFE;
        let image =
            parse(&unrecognized, ParseLimits::default()).expect("unknown OS ABI remains parseable");
        assert_eq!(image.os_abi, 63);
        assert_eq!(image.abi_version, 0xFE);
        assert_eq!(image.os_abi_name(), "reserved/unrecognized");
    }

    #[test]
    fn classifies_standard_and_reserved_elf_object_type_ranges() {
        let cases = [
            (ET_NONE, "no file type"),
            (ET_REL, "relocatable object"),
            (ET_EXEC, "executable"),
            (ET_DYN, "shared object"),
            (ET_CORE, "core file"),
            (0xFE00, "OS-specific type"),
            (0xFEFF, "OS-specific type"),
            (0xFF00, "processor-specific type"),
            (0xFFFF, "processor-specific type"),
            (0x1234, "reserved/unknown type"),
        ];
        for (file_type, expected) in cases {
            let mut bytes = minimal_elf();
            put_u16(&mut bytes, 16, file_type);
            let image = parse(&bytes, ParseLimits::default())
                .expect("unrecognized object type remains parseable");
            assert_eq!(image.file_type, file_type);
            assert_eq!(image.file_type_name(), expected);
            assert_eq!(image.is_executable(), file_type == ET_EXEC);
        }
    }

    #[test]
    fn loads_zero_filled_memory_tail() {
        let bytes = minimal_elf();
        let image = parse(&bytes, ParseLimits::default()).expect("valid fixture");
        let loaded = load_segments(&bytes, &image, 1024).expect("PT_LOAD materializes");
        assert_eq!(&loaded[0].bytes[..8], &[1, 2, 3, 4, 5, 6, 7, 8]);
        assert_eq!(loaded[0].file_size, 8);
        assert_eq!(&loaded[0].bytes[8..], &[0; 8]);

        let mut malformed_image = image.clone();
        malformed_image.program_headers[0].file_size = 17;
        assert!(load_segments(&bytes, &malformed_image, 1024).is_err());
    }

    #[test]
    fn builds_a_32_mib_ee_ram_image_and_zero_fills_bss() {
        let bytes = minimal_elf();
        let image = parse(&bytes, ParseLimits::default()).expect("valid fixture");
        let ram = build_main_ram_image(&bytes, &image).expect("default EE main-RAM image");

        assert_eq!(ram.entry_point, 0x0010_0000);
        assert_eq!(ram.bytes.len(), EE_MAIN_RAM_SIZE);
        assert_eq!(
            &ram.bytes[0x0010_0000..0x0010_0008],
            &[1, 2, 3, 4, 5, 6, 7, 8]
        );
        assert_eq!(&ram.bytes[0x0010_0008..0x0010_0010], &[0; 8]);
        assert_eq!(ram.segments.len(), 1);
        assert_eq!(ram.segments[0].physical_address, 0x0010_0000);
        assert_eq!(ram.segments[0].file_size, 8);
        assert_eq!(ram.segments[0].memory_size, 16);
        assert_eq!(ram.segments[0].flags & PF_X, PF_X);
    }

    #[test]
    fn resolves_default_main_ram_aliases_to_one_physical_backing() {
        let cases = [
            (0x0000_1000, Some(0x0000_1000)),
            (0x2000_1000, Some(0x0000_1000)),
            (0x3010_1000, Some(0x0010_1000)),
            (0x8000_1000, Some(0x0000_1000)),
            (0xA000_1000, Some(0x0000_1000)),
            (0x0200_0000, None),
            (0x1000_0000, None),
            (0x8200_0000, None),
        ];
        for (virtual_address, expected) in cases {
            assert_eq!(
                ee_main_ram_physical_address(virtual_address),
                expected,
                "address 0x{virtual_address:08X}"
            );
        }
    }

    #[test]
    fn builds_ram_image_from_a_kseg_alias() {
        let mut bytes = minimal_elf();
        put_u32(&mut bytes, 24, 0x8010_0000);
        put_u32(&mut bytes, 60, 0x8010_0000);
        put_u32(&mut bytes, 64, 0x8010_0000);
        let image = parse(&bytes, ParseLimits::default()).expect("KSEG ELF parses");
        let ram = build_main_ram_image(&bytes, &image).expect("KSEG0 maps to EE RAM");
        assert_eq!(ram.segments[0].physical_address, 0x0010_0000);
        assert_eq!(
            &ram.bytes[0x0010_0000..0x0010_0008],
            &[1, 2, 3, 4, 5, 6, 7, 8]
        );
    }

    #[test]
    fn rejects_non_exec_unsupported_ranges_and_alias_collisions() {
        let mut dynamic = minimal_elf();
        put_u16(&mut dynamic, 16, 3);
        let dynamic_image = parse(&dynamic, ParseLimits::default()).expect("ET_DYN parses");
        assert!(build_main_ram_image(&dynamic, &dynamic_image).is_err());

        let mut unaligned_entry = minimal_elf();
        put_u32(&mut unaligned_entry, 24, 0x0010_0002);
        let unaligned_image = parse(&unaligned_entry, ParseLimits::default())
            .expect("unaligned entry metadata parses");
        assert!(build_main_ram_image(&unaligned_entry, &unaligned_image).is_err());

        let mut mmio = minimal_elf();
        put_u32(&mut mmio, 24, 0x1000_0000);
        put_u32(&mut mmio, 60, 0x1000_0000);
        put_u32(&mut mmio, 64, 0x1000_0000);
        let mmio_image = parse(&mmio, ParseLimits::default()).expect("MMIO ELF metadata parses");
        assert!(build_main_ram_image(&mmio, &mmio_image).is_err());

        let mut aliased = minimal_elf();
        aliased.resize(160, 0);
        put_u16(&mut aliased, 44, 2);
        put_u32(&mut aliased, 84, PT_LOAD);
        put_u32(&mut aliased, 88, 112);
        put_u32(&mut aliased, 92, 0x2010_0000);
        put_u32(&mut aliased, 96, 0x2010_0000);
        put_u32(&mut aliased, 100, 0);
        put_u32(&mut aliased, 104, 4);
        put_u32(&mut aliased, 108, 6);
        let aliased_image =
            parse(&aliased, ParseLimits::default()).expect("non-overlapping virtual aliases parse");
        assert!(build_main_ram_image(&aliased, &aliased_image).is_err());
    }

    #[test]
    fn rejects_allocated_relocations_until_relocation_application_exists() {
        let mut bytes = elf_with_symbol_and_rel();
        put_u32(&mut bytes, 52 + 160 + 8, SHF_ALLOC);
        let image = parse(&bytes, ParseLimits::default()).expect("allocated REL fixture parses");
        assert!(
            build_main_ram_image(&bytes, &image)
                .expect_err("allocated relocation cannot be ignored")
                .to_string()
                .contains("relocation")
        );
    }

    #[test]
    fn rejects_malformed_elf_tables_and_segment_sizes() {
        let mut bad = minimal_elf();
        bad[0] = 0;
        assert!(parse(&bad, ParseLimits::default()).is_err());
        let mut bad = minimal_elf();
        bad[5] = 2;
        assert!(parse(&bad, ParseLimits::default()).is_err());
        let mut bad = minimal_elf();
        put_u32(&mut bad, 28, 0xFFFF_FFF0);
        assert!(parse(&bad, ParseLimits::default()).is_err());
        let mut bad = minimal_elf();
        put_u32(&mut bad, 68, 20);
        put_u32(&mut bad, 72, 16);
        assert!(parse(&bad, ParseLimits::default()).is_err());
        let mut bad = minimal_elf();
        put_u32(&mut bad, 28, 16);
        assert!(parse(&bad, ParseLimits::default()).is_err());
    }

    #[test]
    fn rejects_overlapping_load_ranges() {
        let mut bytes = minimal_elf();
        bytes.resize(160, 0);
        put_u16(&mut bytes, 44, 2);
        put_u32(&mut bytes, 84, PT_LOAD);
        put_u32(&mut bytes, 88, 128);
        put_u32(&mut bytes, 92, 0x0010_0008);
        put_u32(&mut bytes, 96, 0x0010_0008);
        put_u32(&mut bytes, 100, 4);
        put_u32(&mut bytes, 104, 4);
        put_u32(&mut bytes, 108, 4);
        put_u32(&mut bytes, 128, 1);
        let image = parse(&bytes, ParseLimits::default()).expect("headers parse");
        assert!(load_segments(&bytes, &image, 1024).is_err());
    }

    #[test]
    fn coalesces_overlapping_pure_bss_segments_and_ignores_empty_segments() {
        let mut bytes = minimal_elf();
        // 4 program headers:
        // ph0: minimal_elf() at 0x0010_0000, filesz=8, memsz=16 (ends at 0x0010_0010)
        // ph1: pure BSS at 0x0010_0010, filesz=0, memsz=16
        // ph2: pure BSS at 0x0010_0010, filesz=0, memsz=32 (coalesces ph1)
        // ph3: empty at 0x0010_0030, filesz=0, memsz=0 (ignored)
        bytes.resize(52 + 32 * 4 + 4, 0);
        put_u16(&mut bytes, 44, 4);

        // ph1: 52 + 32
        put_u32(&mut bytes, 52 + 32, PT_LOAD);
        put_u32(&mut bytes, 52 + 32 + 4, 0);
        put_u32(&mut bytes, 52 + 32 + 8, 0x0010_0010);
        put_u32(&mut bytes, 52 + 32 + 12, 0x0010_0010);
        put_u32(&mut bytes, 52 + 32 + 16, 0);
        put_u32(&mut bytes, 52 + 32 + 20, 16);
        put_u32(&mut bytes, 52 + 32 + 24, PF_R | PF_W);

        // ph2: 52 + 64
        put_u32(&mut bytes, 52 + 64, PT_LOAD);
        put_u32(&mut bytes, 52 + 64 + 4, 0);
        put_u32(&mut bytes, 52 + 64 + 8, 0x0010_0010);
        put_u32(&mut bytes, 52 + 64 + 12, 0x0010_0010);
        put_u32(&mut bytes, 52 + 64 + 16, 0);
        put_u32(&mut bytes, 52 + 64 + 20, 32);
        put_u32(&mut bytes, 52 + 64 + 24, PF_R | PF_W);

        // ph3: 52 + 96
        put_u32(&mut bytes, 52 + 96, PT_LOAD);
        put_u32(&mut bytes, 52 + 96 + 4, 0);
        put_u32(&mut bytes, 52 + 96 + 8, 0x0010_0030);
        put_u32(&mut bytes, 52 + 96 + 12, 0x0010_0030);
        put_u32(&mut bytes, 52 + 96 + 16, 0);
        put_u32(&mut bytes, 52 + 96 + 20, 0);
        put_u32(&mut bytes, 52 + 96 + 24, PF_R);

        let image = parse(&bytes, ParseLimits::default()).expect("4 headers parse");
        let loaded = load_segments(&bytes, &image, 1024).expect("BSS segments coalesce");
        assert_eq!(loaded.len(), 2);
        assert_eq!(loaded[0].virtual_address, 0x0010_0000);
        assert_eq!(loaded[0].file_size, 8);
        assert_eq!(loaded[0].bytes.len(), 16);
        assert_eq!(loaded[1].virtual_address, 0x0010_0010);
        assert_eq!(loaded[1].file_size, 0);
        assert_eq!(loaded[1].bytes.len(), 32);
    }

    /// Deterministic xorshift64 so mutation tests are reproducible without
    /// pulling in a random-number crate.
    fn next_random(state: &mut u64) -> u64 {
        *state ^= *state << 13;
        *state ^= *state >> 7;
        *state ^= *state << 17;
        *state
    }

    fn mutate(seed: &[u8], rng: &mut u64) -> Vec<u8> {
        let mut bytes = seed.to_vec();
        for _ in 0..=(next_random(rng) % 8) {
            // Half of the mutations target the headers, where the interesting
            // offsets, counts and sizes live.
            let span = if next_random(rng) & 1 == 0 {
                bytes.len().min(128)
            } else {
                bytes.len()
            };
            let index = (next_random(rng) as usize) % span;
            bytes[index] = next_random(rng) as u8;
        }
        bytes
    }

    #[test]
    fn parser_never_panics_on_every_truncation_or_random_mutation() {
        let limits = ParseLimits {
            max_string_table_bytes: 1 << 16,
            max_symbols: 1 << 10,
            max_relocations: 1 << 10,
            max_sections: 1 << 10,
            max_program_headers: 1 << 10,
            max_mips_option_descriptors: 1 << 10,
        };
        let mut rng = 0x9E37_79B9_7F4A_7C15_u64;
        let mut ram_images_built = 0;
        for seed in [minimal_elf(), elf_with_symbol_and_rel()] {
            for length in 0..=seed.len() {
                let _ = parse(&seed[..length], limits);
            }
            for _ in 0..20_000 {
                let bytes = mutate(&seed, &mut rng);
                if let Ok(image) = parse(&bytes, limits) {
                    let _ = load_segments(&bytes, &image, 1 << 20);
                    // Each RAM image is 32 MiB, so only sample a few.
                    if ram_images_built < 32 {
                        ram_images_built += 1;
                        let _ = build_main_ram_image(&bytes, &image);
                    }
                }
            }
        }
    }

    #[test]
    fn analysis_and_coverage_never_panic_on_mutated_executables() {
        use crate::analysis::{AnalysisLimits, discover_elf, discover_elf_with_discovery};

        let limits = ParseLimits {
            max_string_table_bytes: 1 << 16,
            max_symbols: 1 << 10,
            max_relocations: 1 << 10,
            max_sections: 1 << 10,
            max_program_headers: 1 << 10,
            max_mips_option_descriptors: 1 << 10,
        };
        let analysis_limits = AnalysisLimits {
            max_loaded_memory: 1 << 20,
            max_instructions: 10_000,
            max_blocks: 1_000,
            max_aliased_roots: 4,
            max_function_roots: 1_000,
        };
        let mut rng = 0xD1B5_4A32_D192_ED03_u64;
        let seed = minimal_elf();
        let mut analyzed = 0;
        for _ in 0..5_000 {
            let bytes = mutate(&seed, &mut rng);
            if let Ok(image) = parse(&bytes, limits) {
                analyzed += 1;
                let _ = crate::coverage::profile(&image, &bytes);
                let _ = discover_elf(&image, &bytes, analysis_limits);
                let _ = discover_elf_with_discovery(&image, &bytes, analysis_limits);
            }
        }
        assert!(
            analyzed > 100,
            "mutations should still often parse: {analyzed}"
        );
    }
}
