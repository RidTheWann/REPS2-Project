//! EE virtual-address interpretation shared by analysis, code generation, and
//! the CLI.
//!
//! Real PS2 executables routinely describe their `PT_LOAD` program headers in
//! the direct physical mapping while well over a third of their control
//! transfers run through a cached (KSEG0) or uncached (KSEG1) alias of the same
//! bytes. A static decoder therefore sees both views of one instruction stream.
//!
//! Mapping is derived from ELF segment permissions and the documented EE
//! address layout, never from a game identity:
//!
//! - An address aliases main RAM when its payload (the offset inside its
//!   512 MiB region) is below 32 MiB.
//! - A fetch at an address is file-backed only when an executable segment
//!   covers that payload with its file bytes. Zero-filled tails are data.
//!
//! Nothing outside a loaded segment is assumed to be code.

use crate::elf::{Image, LoadedSegment, PF_X};

/// Size of EE main RAM in bytes. `elf` owns the constant; this module exposes a
/// `u32` view because every alias decision is expressed in 32-bit guest terms.
const EE_MAIN_RAM_BYTES: u32 = crate::elf::EE_MAIN_RAM_SIZE as u32;

/// Payload mask that selects the low 512 MiB alias payload of an address.
const REGION_PAYLOAD_MASK: u32 = 0x1FFF_FFFF;

/// A KSEG segment selected by a virtual address.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub enum EeSegment {
    /// `0x00000000..=0x1FFFFFFF`: direct physical (uncached) view.
    Kuseg,
    /// `0x20000000..=0x3FFFFFFF`: uncached/accelerated physical aliases.
    PhysicalAlias,
    /// `0x80000000..=0x9FFFFFFF`: cached (KSEG0) view.
    Kseg0,
    /// `0xA0000000..=0xBFFFFFFF`: uncached (KSEG1) view.
    Kseg1,
    /// Any other region, including the user `0x40000000..=0x7FFFFFFF` space.
    Other,
}

impl EeSegment {
    /// Classifies a 32-bit EE virtual address.
    #[must_use]
    pub const fn classify(address: u32) -> Self {
        match address {
            0x0000_0000..=0x1FFF_FFFF => Self::Kuseg,
            0x2000_0000..=0x3FFF_FFFF => Self::PhysicalAlias,
            0x8000_0000..=0x9FFF_FFFF => Self::Kseg0,
            0xA000_0000..=0xBFFF_FFFF => Self::Kseg1,
            _ => Self::Other,
        }
    }

    /// Returns a stable label for diagnostics.
    #[must_use]
    pub const fn label(self) -> &'static str {
        match self {
            Self::Kuseg => "kuseg",
            Self::PhysicalAlias => "physical-alias",
            Self::Kseg0 => "kseg0",
            Self::Kseg1 => "kseg1",
            Self::Other => "other",
        }
    }
}

/// One alias region of physical main RAM.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub enum AliasRegion {
    /// `0x00000000`: cached direct view of main RAM.
    Kuseg,
    /// `0x20000000`: uncached ("accelerated") direct view.
    PhysicalAlias,
    /// `0x80000000`: cached KSEG0 view.
    Kseg0,
    /// `0xA0000000`: uncached KSEG1 view.
    Kseg1,
}

impl AliasRegion {
    /// Every alias region in deterministic order.
    pub const ALL: [Self; 4] = [Self::Kuseg, Self::PhysicalAlias, Self::Kseg0, Self::Kseg1];

    /// Returns the region's base virtual address.
    #[must_use]
    pub const fn base(self) -> u32 {
        match self {
            Self::Kuseg => 0x0000_0000,
            Self::PhysicalAlias => 0x2000_0000,
            Self::Kseg0 => 0x8000_0000,
            Self::Kseg1 => 0xA000_0000,
        }
    }

    /// Returns a stable label for diagnostics.
    #[must_use]
    pub const fn label(self) -> &'static str {
        match self {
            Self::Kuseg => "kuseg",
            Self::PhysicalAlias => "physical-alias",
            Self::Kseg0 => "kseg0",
            Self::Kseg1 => "kseg1",
        }
    }

    /// Returns the region an address belongs to, if it is an EE alias region.
    #[must_use]
    pub const fn of(address: u32) -> Option<Self> {
        match EeSegment::classify(address) {
            EeSegment::Kuseg => Some(Self::Kuseg),
            EeSegment::PhysicalAlias => Some(Self::PhysicalAlias),
            EeSegment::Kseg0 => Some(Self::Kseg0),
            EeSegment::Kseg1 => Some(Self::Kseg1),
            EeSegment::Other => None,
        }
    }
}

/// Translates an EE virtual address into physical main-RAM space.
///
/// Only the documented 32 MiB direct-mapped main-RAM window is translated;
/// the EE register area, reserved space, `0x30100000` scratchpad alias,
/// `0x70000000` scratchpad and the user space return `None`.
#[must_use]
pub const fn physical_main_ram_address(virtual_address: u32) -> Option<u32> {
    let physical = match EeSegment::classify(virtual_address) {
        EeSegment::Kuseg => virtual_address,
        EeSegment::PhysicalAlias | EeSegment::Kseg0 | EeSegment::Kseg1 => {
            virtual_address & REGION_PAYLOAD_MASK
        }
        EeSegment::Other => return None,
    };
    if physical < EE_MAIN_RAM_BYTES {
        Some(physical)
    } else {
        None
    }
}

/// Returns the offset of an address inside its alias region.
///
/// `None` for addresses in regions that never alias main RAM.
#[must_use]
pub const fn region_payload(virtual_address: u32) -> Option<u32> {
    match AliasRegion::of(virtual_address) {
        Some(region) => Some(virtual_address - region.base()),
        None => None,
    }
}

/// Returns every virtual region that can address `payload` bytes of main RAM.
///
/// A payload range that crosses the 32 MiB boundary cannot be addressed from
/// any other region while preserving the selected region, because `J`/`JAL`
/// take their region from the delay-slot address and an alias view has a
/// different region. Such a range is only addressable from its original
/// region.
#[must_use]
pub fn regions_for_payload(payload: u32, length: u32) -> Vec<AliasRegion> {
    let Some(end) = payload.checked_add(length) else {
        return Vec::new();
    };
    if payload >= EE_MAIN_RAM_BYTES {
        return Vec::new();
    }
    if end > EE_MAIN_RAM_BYTES {
        return Vec::new();
    }
    AliasRegion::ALL.to_vec()
}

/// Computes the virtual range of `length` bytes of main RAM inside one alias
/// region, or `None` when the range would leave main RAM.
#[must_use]
pub fn alias_range(region: Option<AliasRegion>, payload: u32, length: u32) -> Option<(u32, u32)> {
    let region = region?;
    let end = payload.checked_add(length)?;
    if end > EE_MAIN_RAM_BYTES {
        return None;
    }
    Some((region.base() + payload, region.base() + end))
}

/// Returns the executable virtual-address ranges described by an ELF image.
///
/// Each `PT_LOAD` with executable permission contributes the view it declares
/// plus every further EE alias of the same file-backed bytes. A range that
/// would cross the 32 MiB boundary keeps only its declared view. Overlapping
/// or adjacent ranges are merged so callers can perform a deterministic
/// binary search.
#[must_use]
pub fn executable_ranges(image: &Image) -> Vec<(u32, u32)> {
    let mut ranges = Vec::new();
    for header in &image.program_headers {
        if header.kind != crate::elf::PT_LOAD || header.flags & PF_X == 0 {
            continue;
        }
        if header.file_size == 0 {
            continue;
        }
        let Some(payload) = region_payload(header.virtual_address) else {
            continue;
        };
        let Some(declared) = alias_range(
            AliasRegion::of(header.virtual_address),
            payload,
            header.file_size,
        ) else {
            continue;
        };
        let crossable = payload
            .checked_add(header.file_size)
            .is_some_and(|end| end <= EE_MAIN_RAM_BYTES);
        if crossable {
            for region in AliasRegion::ALL {
                if let Some(range) = alias_range(Some(region), payload, header.file_size) {
                    ranges.push(range);
                }
            }
        } else {
            ranges.push(declared);
        }
    }
    normalize_ranges(ranges)
}

/// Sorts and merges overlapping or touching half-open ranges.
fn normalize_ranges(mut ranges: Vec<(u32, u32)>) -> Vec<(u32, u32)> {
    ranges.sort_unstable();
    let mut merged: Vec<(u32, u32)> = Vec::with_capacity(ranges.len());
    for (start, end) in ranges {
        match merged.last_mut() {
            Some((_, last_end)) if start <= *last_end => {
                *last_end = (*last_end).max(end);
            }
            _ => merged.push((start, end)),
        }
    }
    merged
}

/// Returns whether the virtual address lies inside one of the ranges.
#[must_use]
pub fn contains(ranges: &[(u32, u32)], address: u32) -> bool {
    let index = ranges.partition_point(|(start, _)| *start <= address);
    index > 0 && address < ranges[index - 1].1
}

/// Returns the file-backed executable materialization that backs an
/// instruction fetch at `address`, if any.
///
/// The segment must be executable and cover `address` with file bytes under
/// the same alias region as `address`; a segment declared at the direct
/// physical mapping does not make its KSEG0 alias readable, because that view
/// was never materialized. Zero-filled PT_LOAD tails are never code.
#[must_use]
pub fn file_backed_executable_segment(
    segments: &[LoadedSegment],
    address: u32,
) -> Option<&LoadedSegment> {
    let region = AliasRegion::of(address)?;
    let payload = address - region.base();
    segments.iter().find(|segment| {
        if segment.flags & PF_X == 0 || AliasRegion::of(segment.virtual_address) != Some(region) {
            return false;
        }
        let Some(start) = region_payload(segment.virtual_address) else {
            return false;
        };
        let file_size = segment.file_size.min(segment.bytes.len()) as u32;
        payload >= start && payload - start < file_size
    })
}

/// Returns whether an executable, file-backed segment backs a fetch at the
/// virtual address under the same alias region.
#[must_use]
pub fn is_file_backed_executable_address(segments: &[LoadedSegment], address: u32) -> bool {
    file_backed_executable_segment(segments, address).is_some()
}

/// Copies file-backed executable bytes for `[payload, payload + length)` of
/// the first matching executable segment.
///
/// Returns `None` when fewer than `length` bytes are contiguous file-backed
/// data in one executable segment.
#[must_use]
pub fn executable_file_bytes(
    segments: &[LoadedSegment],
    payload: u32,
    length: u32,
) -> Option<&[u8]> {
    let end = payload.checked_add(length)?;
    if end > EE_MAIN_RAM_BYTES {
        return None;
    }
    segments.iter().find_map(|segment| {
        if segment.flags & PF_X == 0 {
            return None;
        }
        let start = region_payload(segment.virtual_address)?;
        let file_size = segment.file_size.min(segment.bytes.len()) as u32;
        let segment_end = start.checked_add(file_size)?;
        if payload < start || end > segment_end {
            return None;
        }
        let begin = usize::try_from(payload - start).ok()?;
        let finish = usize::try_from(end - start).ok()?;
        segment.bytes.get(begin..finish)
    })
}

/// Calls `f` with the payload of every alias region containing `address`.
///
/// The payload is the offset of `address` inside the region. Iteration order
/// is fixed (`kuseg`, physical alias, KSEG0, KSEG1) so callers can produce a
/// deterministic block set. Traversal stops when `f` returns `false`.
/// Returns `true` when at least one region was offered.
pub fn for_each_alias_payload(address: u32, mut f: impl FnMut(AliasRegion, u32) -> bool) -> bool {
    let Some(payload) = region_payload(address) else {
        return false;
    };
    if payload >= EE_MAIN_RAM_BYTES {
        return false;
    }
    let mut offered = false;
    for region in AliasRegion::ALL {
        if !f(region, payload) {
            break;
        }
        offered = true;
    }
    offered
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::elf::{LoadedSegment, PF_R, PF_W, ProgramHeader};

    fn load_header(virtual_address: u32, file_size: u32, flags: u32) -> ProgramHeader {
        ProgramHeader {
            kind: crate::elf::PT_LOAD,
            offset: 0,
            virtual_address,
            physical_address: virtual_address,
            file_size,
            memory_size: file_size,
            flags,
            alignment: 4,
        }
    }

    fn image(headers: Vec<ProgramHeader>) -> Image {
        Image {
            file_type: crate::elf::ET_EXEC,
            os_abi: 0,
            abi_version: 0,
            machine: crate::elf::EM_MIPS,
            entry_point: 0x0010_0000,
            flags: 0,
            section_name_table_index: 0,
            program_headers: headers,
            section_headers: Vec::new(),
            symbols: Vec::new(),
            relocations: Vec::new(),
            mips_abi_flags: None,
        }
    }

    #[test]
    fn classifies_and_translates_ee_segments() {
        assert_eq!(EeSegment::classify(0x0010_0000), EeSegment::Kuseg);
        assert_eq!(EeSegment::classify(0x2010_0000), EeSegment::PhysicalAlias);
        assert_eq!(EeSegment::classify(0x8010_0000), EeSegment::Kseg0);
        assert_eq!(EeSegment::classify(0xA010_0000), EeSegment::Kseg1);
        assert_eq!(EeSegment::classify(0x4000_0000), EeSegment::Other);
        assert_eq!(physical_main_ram_address(0x0010_0000), Some(0x0010_0000));
        assert_eq!(physical_main_ram_address(0x2010_0000), Some(0x0010_0000));
        assert_eq!(physical_main_ram_address(0x8010_0000), Some(0x0010_0000));
        assert_eq!(physical_main_ram_address(0xA010_0000), Some(0x0010_0000));
        // EE registers, IOP memory, scratchpad and user space must not alias
        // main RAM, including under the direct physical view.
        assert_eq!(physical_main_ram_address(0x1000_0000), None);
        assert_eq!(physical_main_ram_address(0x0410_0000), None);
        assert_eq!(physical_main_ram_address(0x7000_0000), None);
        assert_eq!(physical_main_ram_address(0x4000_0000), None);
        assert_eq!(physical_main_ram_address(0x8010_0000), Some(0x0010_0000));
        assert_eq!(physical_main_ram_address(0x21FF_FFFF), Some(0x01FF_FFFF));
        assert_eq!(physical_main_ram_address(0x8200_0000), None);
    }

    #[test]
    fn executable_ranges_cover_every_alias_of_executable_segments_only() {
        let image = image(vec![
            load_header(0x0001_0000, 0x1000, PF_X),
            load_header(0x0100_0000, 0x2000, PF_R | PF_W),
        ]);
        let ranges = executable_ranges(&image);
        assert_eq!(
            ranges,
            vec![
                (0x0001_0000, 0x0001_1000),
                (0x2001_0000, 0x2001_1000),
                (0x8001_0000, 0x8001_1000),
                (0xA001_0000, 0xA001_1000),
            ]
        );
        assert!(contains(&ranges, 0x0001_0FFC));
        assert!(contains(&ranges, 0x8001_0FFC));
        assert!(!contains(&ranges, 0x0001_1000));
        // Data-only segments never become executable.
        assert!(!contains(&ranges, 0x0100_0000));
    }

    #[test]
    fn merges_overlapping_program_header_aliases() {
        let image = image(vec![
            load_header(0x0010_0000, 0x1000, PF_X),
            load_header(0x8010_0800, 0x1000, PF_X),
        ]);
        assert_eq!(
            executable_ranges(&image),
            vec![
                (0x0010_0000, 0x0010_1800),
                (0x2010_0000, 0x2010_1800),
                (0x8010_0000, 0x8010_1800),
                (0xA010_0000, 0xA010_1800),
            ]
        );
    }

    #[test]
    fn kseg0_described_segment_covers_every_alias_of_the_declared_region() {
        // The real-world shape: the whole image is one KSEG0 PT_LOAD.
        let image = image(vec![load_header(0x8010_0000, 0x1000, PF_X)]);
        assert_eq!(
            executable_ranges(&image),
            vec![
                (0x0010_0000, 0x0010_1000),
                (0x2010_0000, 0x2010_1000),
                (0x8010_0000, 0x8010_1000),
                (0xA010_0000, 0xA010_1000),
            ]
        );
    }

    #[test]
    fn alias_ranges_never_cross_main_ram() {
        assert_eq!(
            alias_range(Some(AliasRegion::Kseg0), 0x0010_0000, 0x10),
            Some((0x8010_0000, 0x8010_0010))
        );
        assert_eq!(
            alias_range(Some(AliasRegion::Kseg0), 0x01FF_FFF0, 0x20),
            None
        );
        assert_eq!(alias_range(None, 0, 4), None);
        assert_eq!(alias_range(Some(AliasRegion::Kseg1), u32::MAX, 4), None);
        assert!(regions_for_payload(0x0010_0000, 0x1000).contains(&AliasRegion::Kseg1));
        assert!(regions_for_payload(0x01FF_FFF0, 0x20).is_empty());
    }

    #[test]
    fn materialized_segment_lookup_requires_executable_file_bytes() {
        let segments = [
            LoadedSegment {
                virtual_address: 0x0010_0000,
                flags: PF_X,
                file_size: 0x10,
                bytes: vec![0; 0x20],
            },
            LoadedSegment {
                virtual_address: 0x0020_0000,
                flags: PF_R | PF_W,
                file_size: 0x10,
                bytes: vec![0; 0x10],
            },
            LoadedSegment {
                virtual_address: 0x8030_0000,
                flags: PF_X,
                file_size: 0x08,
                bytes: vec![0; 0x08],
            },
        ];
        assert!(is_file_backed_executable_address(&segments, 0x0010_0000));
        // The KSEG0 descriptor does not make the direct view executable.
        assert!(!is_file_backed_executable_address(&segments, 0x0030_0000));
        assert!(is_file_backed_executable_address(&segments, 0x8030_0004));
        assert!(!is_file_backed_executable_address(&segments, 0x8030_0008));
        // The zero-filled tail beyond file_size is not file-backed.
        assert!(!is_file_backed_executable_address(&segments, 0x0010_0010));
        // A writable data segment is not code even when addresses line up.
        assert!(!is_file_backed_executable_address(&segments, 0x0020_0000));
        assert_eq!(
            executable_file_bytes(&segments, 0x0010_0004, 4),
            Some(&[0u8; 4][..])
        );
        assert_eq!(executable_file_bytes(&segments, 0x0010_000E, 4), None);
        assert_eq!(executable_file_bytes(&segments, 0x0020_0000, 4), None);
    }

    #[test]
    fn alias_iteration_is_ordered() {
        let mut seen = Vec::new();
        assert!(for_each_alias_payload(0x8010_0020, |region, payload| {
            seen.push((region.base(), payload));
            true
        }));
        assert_eq!(
            seen,
            vec![
                (0x0000_0000, 0x0010_0020),
                (0x2000_0000, 0x0010_0020),
                (0x8000_0000, 0x0010_0020),
                (0xA000_0000, 0x0010_0020),
            ]
        );
        assert!(!for_each_alias_payload(0x4000_0020, |_, _| true));
    }
}
