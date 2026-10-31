//! Deterministic decoder coverage summaries over ELF executable bytes.
//!
//! A profile scans file-backed bytes in executable `PT_LOAD` segments. It is an
//! address-space census, not a reachability analysis: embedded data and bytes
//! that never execute are included. The separate entry scan is a bounded linear
//! diagnostic and likewise makes no control-flow claim.

use std::collections::BTreeMap;
use std::fmt;

use crate::elf::{Image, PF_X, PT_LOAD};
use crate::mips::{self, EncodingFamily, Opcode};

/// Maximum number of words in the linear entry-point diagnostic scan.
pub const LINEAR_ENTRY_SCAN_LIMIT: u64 = 4096;

/// A file-backed executable segment whose bytes are not fully present.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProfileError {
    segment_index: usize,
    offset: u64,
    size: u64,
    file_size: u64,
}

impl ProfileError {
    fn segment_outside_file(segment_index: usize, offset: u64, size: u64, file_size: u64) -> Self {
        Self {
            segment_index,
            offset,
            size,
            file_size,
        }
    }
}

impl fmt::Display for ProfileError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "executable PT_LOAD segment {} range 0x{:X}..0x{:X} exceeds the {}-byte input",
            self.segment_index,
            self.offset,
            self.offset.saturating_add(self.size),
            self.file_size
        )
    }
}

impl std::error::Error for ProfileError {}

/// Why the bounded linear scan from the ELF entry point stopped.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EntryScanStop {
    Unsupported(EncodingFamily),
    Trap(&'static str),
    SegmentEnd,
    WordLimit,
}

/// A bounded linear scan beginning at the ELF entry point.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct EntryScan {
    pub decoded_words: u64,
    pub stop_pc: u32,
    pub stop_word: Option<u32>,
    pub stop: EntryScanStop,
}

/// Aggregate decoder census for one ELF image.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct DecoderProfile {
    pub file_backed_executable_bytes: u64,
    pub scanned_words: u64,
    pub supported_words: u64,
    pub trap_words: u64,
    pub unsupported_words: u64,
    pub supported_opcodes: BTreeMap<&'static str, u64>,
    pub unsupported_families: BTreeMap<EncodingFamily, u64>,
    pub unsupported_encodings: BTreeMap<u32, u64>,
    pub entry_scan: Option<EntryScan>,
}

impl DecoderProfile {
    fn record_word(&mut self, word: u32) {
        self.scanned_words += 1;
        let instruction = mips::decode(word, 0);
        if !instruction.is_supported() {
            self.unsupported_words += 1;
            *self
                .unsupported_families
                .entry(mips::encoding_family(word))
                .or_default() += 1;
            *self.unsupported_encodings.entry(word).or_default() += 1;
        } else if matches!(instruction.opcode, Opcode::Syscall | Opcode::Break) {
            self.trap_words += 1;
        } else {
            self.supported_words += 1;
            *self
                .supported_opcodes
                .entry(instruction.opcode.mnemonic())
                .or_default() += 1;
        }
    }
}

/// Profiles complete 32-bit words at naturally aligned virtual addresses in
/// every file-backed executable `PT_LOAD` segment.
pub fn profile(image: &Image, file_bytes: &[u8]) -> Result<DecoderProfile, ProfileError> {
    let mut profile = DecoderProfile::default();

    for (index, header) in image.program_headers.iter().enumerate() {
        if header.kind != PT_LOAD || header.flags & PF_X == 0 {
            continue;
        }

        let file_offset = u64::from(header.offset);
        let file_size = u64::from(header.file_size);
        if file_offset
            .checked_add(file_size)
            .is_none_or(|end| end > file_bytes.len() as u64)
        {
            return Err(ProfileError::segment_outside_file(
                index,
                file_offset,
                file_size,
                file_bytes.len() as u64,
            ));
        }
        profile.file_backed_executable_bytes += file_size;

        let skip = ((4 - (u64::from(header.virtual_address) % 4)) % 4) as usize;
        let file_offset = file_offset as usize;
        let file_size = file_size as usize;
        if skip >= file_size {
            continue;
        }
        for word_offset in (skip..=file_size.saturating_sub(4)).step_by(4) {
            let word = u32::from_le_bytes(
                file_bytes[file_offset + word_offset..file_offset + word_offset + 4]
                    .try_into()
                    .expect("four-byte coverage window"),
            );
            profile.record_word(word);
        }
    }

    profile.entry_scan = scan_entry(image, file_bytes, image.entry_point);
    Ok(profile)
}

fn scan_entry(image: &Image, file_bytes: &[u8], entry: u32) -> Option<EntryScan> {
    for header in image
        .program_headers
        .iter()
        .filter(|header| header.kind == PT_LOAD && header.flags & PF_X != 0)
    {
        if entry & 3 != 0 {
            continue;
        }
        let Some(relative) = entry.checked_sub(header.virtual_address) else {
            continue;
        };
        if u64::from(relative) + 4 > u64::from(header.file_size) {
            continue;
        }

        let mut pc = entry;
        let mut decoded_words = 0u64;
        for _ in 0..LINEAR_ENTRY_SCAN_LIMIT {
            let Some(relative) = pc.checked_sub(header.virtual_address) else {
                return Some(EntryScan {
                    decoded_words,
                    stop_pc: pc,
                    stop_word: None,
                    stop: EntryScanStop::SegmentEnd,
                });
            };
            if u64::from(relative) + 4 > u64::from(header.file_size) {
                return Some(EntryScan {
                    decoded_words,
                    stop_pc: pc,
                    stop_word: None,
                    stop: EntryScanStop::SegmentEnd,
                });
            }
            let offset = u64::from(header.offset) + u64::from(relative);
            let file_size = file_bytes.len() as u64;
            if offset + 4 > file_size {
                return Some(EntryScan {
                    decoded_words,
                    stop_pc: pc,
                    stop_word: None,
                    stop: EntryScanStop::SegmentEnd,
                });
            }
            let offset = offset as usize;
            let word = u32::from_le_bytes(
                file_bytes[offset..offset + 4]
                    .try_into()
                    .expect("four-byte entry scan window"),
            );
            let instruction = mips::decode(word, pc);
            if !instruction.is_supported() {
                return Some(EntryScan {
                    decoded_words,
                    stop_pc: pc,
                    stop_word: Some(word),
                    stop: EntryScanStop::Unsupported(mips::encoding_family(word)),
                });
            }
            if matches!(instruction.opcode, Opcode::Syscall | Opcode::Break) {
                return Some(EntryScan {
                    decoded_words,
                    stop_pc: pc,
                    stop_word: Some(word),
                    stop: EntryScanStop::Trap(instruction.opcode.mnemonic()),
                });
            }
            decoded_words += 1;
            let Some(next_pc) = pc.checked_add(4) else {
                return Some(EntryScan {
                    decoded_words,
                    stop_pc: pc,
                    stop_word: None,
                    stop: EntryScanStop::SegmentEnd,
                });
            };
            pc = next_pc;
        }
        return Some(EntryScan {
            decoded_words,
            stop_pc: pc,
            stop_word: None,
            stop: EntryScanStop::WordLimit,
        });
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::elf::{EM_MIPS, ET_EXEC, Image, ProgramHeader};

    fn image_with_segment(
        entry: u32,
        virtual_address: u32,
        file_size: u32,
        memory_size: u32,
        flags: u32,
    ) -> Image {
        Image {
            file_type: ET_EXEC,
            machine: EM_MIPS,
            entry_point: entry,
            flags: 0,
            section_name_table_index: 0,
            program_headers: vec![ProgramHeader {
                kind: PT_LOAD,
                offset: 0,
                virtual_address,
                physical_address: virtual_address,
                file_size,
                memory_size,
                flags,
                alignment: 4,
            }],
            section_headers: Vec::new(),
            symbols: Vec::new(),
            relocations: Vec::new(),
        }
    }

    fn words_to_bytes(words: &[u32]) -> Vec<u8> {
        words.iter().flat_map(|word| word.to_le_bytes()).collect()
    }

    #[test]
    fn counts_supported_trap_and_unsupported_words() {
        let words = [0x0000_0000, 0x2408_0005, 0x7000_0C28, 0x0000_000C];
        let bytes = words_to_bytes(&words);
        let image = image_with_segment(0x1000, 0x1000, 16, 16, PF_X);

        let profile = profile(&image, &bytes).expect("valid executable segment");
        assert_eq!(profile.file_backed_executable_bytes, 16);
        assert_eq!(profile.scanned_words, 4);
        assert_eq!(profile.supported_words, 2);
        assert_eq!(profile.trap_words, 1);
        assert_eq!(profile.unsupported_words, 1);
        assert_eq!(profile.supported_opcodes.get("nop"), Some(&1));
        assert_eq!(profile.supported_opcodes.get("addiu"), Some(&1));
        assert_eq!(
            profile.unsupported_families.get(&EncodingFamily::Mmi1),
            Some(&1)
        );
        assert_eq!(profile.unsupported_encodings.get(&0x7000_0C28), Some(&1));

        let scan = profile.entry_scan.expect("entry is executable");
        assert_eq!(scan.decoded_words, 2);
        assert_eq!(scan.stop_pc, 0x1008);
        assert_eq!(scan.stop_word, Some(0x7000_0C28));
        assert_eq!(scan.stop, EntryScanStop::Unsupported(EncodingFamily::Mmi1));
    }

    #[test]
    fn ignores_non_executable_and_zero_file_size_segments() {
        let words = [0x2408_0005];
        let bytes = words_to_bytes(&words);
        let image = image_with_segment(0x1000, 0x1000, 4, 4, 0x2);
        let non_executable_profile = profile(&image, &bytes).expect("valid segment");
        assert_eq!(non_executable_profile.file_backed_executable_bytes, 0);
        assert_eq!(non_executable_profile.scanned_words, 0);
        assert_eq!(non_executable_profile.entry_scan, None);

        let image = image_with_segment(0x1000, 0x1000, 0, 64, PF_X);
        let bss_profile = profile(&image, &bytes).expect("valid BSS-only segment");
        assert_eq!(bss_profile.file_backed_executable_bytes, 0);
        assert_eq!(bss_profile.scanned_words, 0);
        assert_eq!(bss_profile.entry_scan, None);
    }

    #[test]
    fn scans_only_complete_aligned_words() {
        let bytes = [0xAA, 0xBB, 0x05, 0x00, 0x08, 0x24, 0x28, 0x0C, 0x00, 0x70];
        let image = image_with_segment(0x1004, 0x1002, 10, 10, PF_X);

        let profile = profile(&image, &bytes).expect("valid segment");
        assert_eq!(profile.file_backed_executable_bytes, 10);
        assert_eq!(profile.scanned_words, 2);
        assert_eq!(profile.supported_words, 1);
        assert_eq!(profile.unsupported_words, 1);

        let scan = profile.entry_scan.expect("entry is executable");
        assert_eq!(scan.decoded_words, 1);
        assert_eq!(scan.stop_pc, 0x1008);
        assert_eq!(scan.stop_word, Some(0x7000_0C28));
    }

    #[test]
    fn rejects_segments_outside_the_input() {
        let image = image_with_segment(0x1000, 0x1000, 8, 8, PF_X);
        let error = profile(&image, &[0; 4]).expect_err("truncated segment");
        assert!(error.to_string().contains("exceeds the 4-byte input"));
    }
}
