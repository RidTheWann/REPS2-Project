//! Defensive ELF32 little-endian parser for PS2 executable research.

use std::{fmt, ops::Range};

pub const EM_MIPS: u16 = 8;
pub const ET_EXEC: u16 = 2;
pub const PT_LOAD: u32 = 1;
pub const SHF_ALLOC: u32 = 0x2;
pub const PF_R: u32 = 0x4;
pub const PF_W: u32 = 0x2;
pub const PF_X: u32 = 0x1;
pub const EE_MAIN_RAM_SIZE: usize = 32 * 1024 * 1024;
pub const SHT_NULL: u32 = 0;
pub const SHT_SYMTAB: u32 = 2;
pub const SHT_STRTAB: u32 = 3;
pub const SHT_RELA: u32 = 4;
pub const SHT_NOBITS: u32 = 8;
pub const SHT_REL: u32 = 9;
pub const SHT_DYNSYM: u32 = 11;

const ELF_HEADER_SIZE: usize = 52;
const PROGRAM_HEADER_SIZE: usize = 32;
const SECTION_HEADER_SIZE: usize = 40;
const SYMBOL_SIZE: usize = 16;
const REL_SIZE: usize = 8;
const STB_LOCAL: u8 = 0;
const SHN_LORESERVE: u16 = 0xFF00;
const SHN_XINDEX: u16 = 0xFFFF;
const RELA_SIZE: usize = 12;

/// Limits untrusted ELF-derived allocations and table processing.
#[derive(Clone, Copy, Debug)]
pub struct ParseLimits {
    pub max_string_table_bytes: usize,
    pub max_symbols: usize,
    pub max_relocations: usize,
}

impl Default for ParseLimits {
    fn default() -> Self {
        Self {
            max_string_table_bytes: 64 * 1024 * 1024,
            max_symbols: 1_000_000,
            max_relocations: 4_000_000,
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
    pub section_index: u16,
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

/// Parsed ELF metadata. This is not yet a complete PS2 ABI loader.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Image {
    pub file_type: u16,
    pub machine: u16,
    pub entry_point: u32,
    pub flags: u32,
    pub section_name_table_index: u16,
    pub program_headers: Vec<ProgramHeader>,
    pub section_headers: Vec<SectionHeader>,
    pub symbols: Vec<Symbol>,
    pub relocations: Vec<Relocation>,
}

impl Image {
    #[must_use]
    pub fn is_mips(&self) -> bool {
        self.machine == EM_MIPS
    }

    #[must_use]
    pub fn is_executable(&self) -> bool {
        self.file_type == ET_EXEC
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
    count: u16,
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
        if count == 0 || local_symbol_end == 0 || local_symbol_end > count {
            return Err(Error::new(
                "ELF symbol table has an invalid local-symbol boundary",
            ));
        }
        if count > limits.max_symbols.saturating_sub(symbols.len()) {
            return Err(Error::new(
                "ELF symbol count exceeds configured parser limit",
            ));
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
            let symbol_section = read_u16(table, base + 14)?;
            if symbol_section == SHN_XINDEX {
                return Err(Error::new(
                    "Extended ELF symbol section indices are not supported yet",
                ));
            }
            if symbol_section < SHN_LORESERVE && usize::from(symbol_section) >= sections.len() {
                return Err(Error::new("ELF symbol references an invalid section index"));
            }
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
    let program_count = read_u16(bytes, 44)?;
    let section_entry_size = read_u16(bytes, 46)?;
    let section_count = read_u16(bytes, 48)?;
    let section_name_table_index = read_u16(bytes, 50)?;

    if usize::from(header_size) < ELF_HEADER_SIZE || usize::from(header_size) > bytes.len() {
        return Err(Error::new("Invalid ELF header size"));
    }
    if program_count == u16::MAX
        || section_name_table_index == u16::MAX
        || (section_count == 0 && section_offset != 0)
    {
        return Err(Error::new("Extended ELF numbering is not supported yet"));
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
    if section_count != 0 && section_offset < u32::from(header_size) {
        return Err(Error::new(
            "ELF section-header table overlaps the ELF header",
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

    let mut program_headers = Vec::with_capacity(usize::from(program_count));
    for index in 0..usize::from(program_count) {
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

    let mut section_headers = Vec::with_capacity(usize::from(section_count));
    for index in 0..usize::from(section_count) {
        let base = usize::try_from(section_offset)
            .map_err(|_| Error::new("Section-header offset overflow"))?
            .checked_add(
                index
                    .checked_mul(usize::from(section_entry_size))
                    .ok_or_else(|| Error::new("Section-header index overflow"))?,
            )
            .ok_or_else(|| Error::new("Section-header offset overflow"))?;
        let section = read_section_header(bytes, base)?;
        if section.kind != SHT_NOBITS {
            checked_range(
                bytes,
                u64::from(section.offset),
                u64::from(section.size),
                "ELF section data",
            )?;
        }
        section_headers.push(section);
    }

    let null_section_is_invalid = section_headers.first().is_some_and(|null_section| {
        null_section.kind != SHT_NULL
            || null_section.name_offset != 0
            || null_section.flags != 0
            || null_section.virtual_address != 0
            || null_section.offset != 0
            || null_section.size != 0
            || null_section.link != 0
            || null_section.info != 0
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
        let names = section_headers
            .get(usize::from(section_name_table_index))
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
    Ok(Image {
        file_type,
        machine,
        entry_point,
        flags,
        section_name_table_index,
        program_headers,
        section_headers,
        symbols,
        relocations,
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
