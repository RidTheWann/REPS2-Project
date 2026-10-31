// SPDX-License-Identifier: GPL-3.0-or-later
//! Bounded, read-only ISO 9660 reader for 2048-byte logical-sector images.
//!
//! The initial PS2 disc path intentionally supports the Primary Volume
//! Descriptor and ordinary non-interleaved, single-extent files. Raw 2352-byte
//! BIN/CUE tracks, CHD, UDF, Joliet names, Rock Ridge, and multi-extent files
//! are detected or rejected explicitly rather than guessed.

use std::io::{Read, Seek, SeekFrom};

pub const LOGICAL_BLOCK_SIZE: u64 = 2048;
const VOLUME_DESCRIPTOR_START_LBA: u64 = 16;
const MAX_VOLUME_DESCRIPTORS: u64 = 64;
const MAX_DIRECTORY_BYTES: u64 = 16 * 1024 * 1024;
const MAX_DIRECTORY_ENTRIES: usize = 100_000;
const MAX_PATH_DEPTH: usize = 32;
pub const MAX_SYSTEM_CNF_BYTES: u64 = 64 * 1024;
pub const MAX_BOOT_EXECUTABLE_BYTES: u64 = 512 * 1024 * 1024;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Error {
    message: String,
}

impl Error {
    fn new(message: impl Into<String>) -> Self {
        Self { message: message.into() }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl std::error::Error for Error {}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct VolumeInfo {
    pub volume_identifier: String,
    pub volume_space_blocks: u32,
    pub logical_block_size: u16,
    pub volume_bytes: u64,
    pub image_bytes: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DirectoryEntry {
    pub name: String,
    pub extent_lba: u32,
    pub size_bytes: u32,
    pub flags: u8,
    file_unit_size: u8,
    interleave_gap_size: u8,
}

impl DirectoryEntry {
    #[must_use]
    pub const fn is_directory(&self) -> bool {
        self.flags & 0x02 != 0
    }
}

pub struct Iso9660<R> {
    reader: R,
    info: VolumeInfo,
    root: DirectoryEntry,
}

impl<R: Read + Seek> Iso9660<R> {
    /// Opens a raw 2048-byte-sector ISO image and validates its primary volume
    /// descriptor, volume bounds, logical block size, and root directory.
    pub fn open(mut reader: R) -> Result<Self, Error> {
        let image_bytes = reader.seek(SeekFrom::End(0))
            .map_err(|error| Error::new(format!("cannot determine ISO image size: {error}")))?;

        if image_bytes < (VOLUME_DESCRIPTOR_START_LBA + 1) * LOGICAL_BLOCK_SIZE {
            return Err(Error::new("image is too small to contain an ISO 9660 volume descriptor"));
        }
        if image_bytes % LOGICAL_BLOCK_SIZE != 0 {
            return Err(Error::new(
                "image is not a raw 2048-byte-sector ISO; 2352-byte BIN/CUE and CHD images are not supported",
            ));
        }

        let mut primary_descriptor: Option<[u8; LOGICAL_BLOCK_SIZE as usize]> = None;
        let mut found_terminator = false;

        for descriptor_index in 0..MAX_VOLUME_DESCRIPTORS {
            let lba = VOLUME_DESCRIPTOR_START_LBA + descriptor_index;
            let offset = lba.checked_mul(LOGICAL_BLOCK_SIZE)
                .ok_or_else(|| Error::new("volume descriptor offset overflow"))?;
            let mut sector = [0u8; LOGICAL_BLOCK_SIZE as usize];
            read_image_at(&mut reader, image_bytes, offset, &mut sector)?;

            if &sector[1..6] != b"CD001" {
                return Err(Error::new(format!(
                    "invalid ISO 9660 descriptor identifier at sector {lba}"
                )));
            }
            if sector[6] != 1 {
                return Err(Error::new(format!(
                    "unsupported ISO 9660 descriptor version {} at sector {lba}",
                    sector[6]
                )));
            }

            match sector[0] {
                1 => {
                    if primary_descriptor.is_some() {
                        return Err(Error::new("ISO contains more than one Primary Volume Descriptor"));
                    }
                    primary_descriptor = Some(sector);
                }
                255 => {
                    found_terminator = true;
                    break;
                }
                _ => {
                    // Boot records and supplementary descriptors are valid
                    // members of a descriptor set. This reader only consumes
                    // the Primary Volume Descriptor.
                }
            }
        }

        if !found_terminator {
            return Err(Error::new("ISO volume descriptor sequence has no terminator within the safety limit"));
        }

        let descriptor = primary_descriptor
            .ok_or_else(|| Error::new("ISO has no Primary Volume Descriptor"))?;
        let volume_space_blocks = read_both_u32(&descriptor, 80, "volume space size")?;
        if volume_space_blocks <= VOLUME_DESCRIPTOR_START_LBA as u32 {
            return Err(Error::new("ISO volume space size is too small"));
        }
        let logical_block_size = read_both_u16(&descriptor, 128, "logical block size")?;
        if u64::from(logical_block_size) != LOGICAL_BLOCK_SIZE {
            return Err(Error::new(format!(
                "unsupported ISO logical block size {logical_block_size}; expected 2048"
            )));
        }

        let volume_bytes = u64::from(volume_space_blocks)
            .checked_mul(LOGICAL_BLOCK_SIZE)
            .ok_or_else(|| Error::new("ISO volume byte size overflows"))?;
        if volume_bytes > image_bytes {
            return Err(Error::new(format!(
                "ISO declares {volume_bytes} bytes but the image contains only {image_bytes}"
            )));
        }

        let volume_identifier = decode_volume_identifier(&descriptor[40..72])?;
        let info = VolumeInfo {
            volume_identifier,
            volume_space_blocks,
            logical_block_size,
            volume_bytes,
            image_bytes,
        };

        let root_record_length = usize::from(descriptor[156]);
        if root_record_length < 34 || root_record_length > descriptor.len() - 156 {
            return Err(Error::new("Primary Volume Descriptor root directory record is malformed"));
        }
        let root = parse_directory_record(
            &descriptor[156..156 + root_record_length],
            volume_bytes,
        )?;
        if !root.is_directory() {
            return Err(Error::new("Primary Volume Descriptor root record is not a directory"));
        }
        if root.name != "." {
            return Err(Error::new("Primary Volume Descriptor root record is not the current-directory entry"));
        }

        Ok(Self { reader, info, root })
    }

    #[must_use]
    pub fn volume_info(&self) -> &VolumeInfo {
        &self.info
    }

    /// Reads one directory, enforcing the record, extent, and entry-count
    /// limits. ISO directory records are never allowed to cross a logical
    /// block boundary; zero-length bytes pad to the next block.
    pub fn read_directory(&mut self, directory: &DirectoryEntry) -> Result<Vec<DirectoryEntry>, Error> {
        if !directory.is_directory() {
            return Err(Error::new(format!("{} is not a directory", directory.name)));
        }
        if directory.flags & 0x80 != 0 || directory.file_unit_size != 0 || directory.interleave_gap_size != 0 {
            return Err(Error::new(format!("directory {} uses unsupported multi-extent/interleaved storage", directory.name)));
        }
        let directory_size = u64::from(directory.size_bytes);
        if directory_size > MAX_DIRECTORY_BYTES {
            return Err(Error::new(format!(
                "directory {} exceeds the {} MiB safety limit",
                directory.name,
                MAX_DIRECTORY_BYTES / (1024 * 1024)
            )));
        }

        let mut bytes = vec![0u8; usize::try_from(directory_size)
            .map_err(|_| Error::new("directory size does not fit host memory"))?];
        let offset = u64::from(directory.extent_lba)
            .checked_mul(LOGICAL_BLOCK_SIZE)
            .ok_or_else(|| Error::new("directory extent offset overflow"))?;
        self.read_at(offset, &mut bytes)?;

        let mut entries = Vec::new();
        let mut cursor = 0usize;
        while cursor < bytes.len() {
            let record_length = usize::from(bytes[cursor]);
            if record_length == 0 {
                let next_block = (cursor / LOGICAL_BLOCK_SIZE as usize)
                    .checked_add(1)
                    .and_then(|block| block.checked_mul(LOGICAL_BLOCK_SIZE as usize))
                    .ok_or_else(|| Error::new("directory padding offset overflow"))?;
                if next_block <= cursor {
                    return Err(Error::new("directory padding did not advance the parser"));
                }
                cursor = next_block.min(bytes.len());
                continue;
            }

            if record_length < 34 {
                return Err(Error::new(format!(
                    "directory record at byte {cursor} is shorter than the minimum ISO record"
                )));
            }
            let block_offset = cursor % LOGICAL_BLOCK_SIZE as usize;
            if block_offset + record_length > LOGICAL_BLOCK_SIZE as usize {
                return Err(Error::new(format!(
                    "directory record at byte {cursor} crosses a logical block boundary"
                )));
            }
            let record_end = cursor.checked_add(record_length)
                .ok_or_else(|| Error::new("directory record end overflows"))?;
            if record_end > bytes.len() {
                return Err(Error::new(format!(
                    "directory record at byte {cursor} extends beyond its directory extent"
                )));
            }

            let record = parse_directory_record(&bytes[cursor..record_end], self.info.volume_bytes)?;
            if record.name != "." && record.name != ".." {
                entries.push(record);
                if entries.len() > MAX_DIRECTORY_ENTRIES {
                    return Err(Error::new("directory exceeds the 100000-entry safety limit"));
                }
            }
            cursor = record_end;
        }

        Ok(entries)
    }

    pub fn root_entries(&mut self) -> Result<Vec<DirectoryEntry>, Error> {
        let root = self.root.clone();
        self.read_directory(&root)
    }

    /// Resolves an ISO path using case-insensitive primary-volume identifiers.
    /// A device prefix such as cdrom0: and either slash style are accepted.
    pub fn find_entry(&mut self, path: &str) -> Result<Option<DirectoryEntry>, Error> {
        let components = normalize_path(path)?;
        if components.is_empty() {
            return Ok(Some(self.root.clone()));
        }

        let mut current = self.root.clone();
        for (index, component) in components.iter().enumerate() {
            if !current.is_directory() {
                return Ok(None);
            }
            let children = self.read_directory(&current)?;
            let Some(found) = children.into_iter().find(|entry| {
                normalize_identifier(&entry.name) == *component
            }) else {
                return Ok(None);
            };

            if index + 1 < components.len() && !found.is_directory() {
                return Ok(None);
            }
            current = found;
        }
        Ok(Some(current))
    }

    /// Reads one ordinary file into bounded memory. Multi-extent and
    /// interleaved files are rejected by the directory-record parser.
    pub fn read_file(&mut self, path: &str, max_bytes: u64) -> Result<Vec<u8>, Error> {
        let entry = self.find_entry(path)?
            .ok_or_else(|| Error::new(format!("ISO path was not found: {path}")))?;
        self.read_entry(&entry, max_bytes)
    }

    pub fn read_entry(&mut self, entry: &DirectoryEntry, max_bytes: u64) -> Result<Vec<u8>, Error> {
        if entry.is_directory() {
            return Err(Error::new(format!("{} is a directory, not a file", entry.name)));
        }
        if entry.flags & 0x80 != 0 || entry.file_unit_size != 0 || entry.interleave_gap_size != 0 {
            return Err(Error::new(format!("{} uses unsupported multi-extent/interleaved storage", entry.name)));
        }
        let size = u64::from(entry.size_bytes);
        if size > max_bytes {
            return Err(Error::new(format!(
                "ISO file {} is {size} bytes, exceeding the {max_bytes}-byte extraction limit",
                entry.name
            )));
        }
        let mut bytes = vec![0u8; usize::try_from(size)
            .map_err(|_| Error::new("ISO file size does not fit host memory"))?];
        let offset = u64::from(entry.extent_lba)
            .checked_mul(LOGICAL_BLOCK_SIZE)
            .ok_or_else(|| Error::new("file extent offset overflow"))?;
        self.read_at(offset, &mut bytes)?;
        Ok(bytes)
    }

    /// Resolves SYSTEM.CNF and returns its BOOT2 target (preferred) or BOOT
    /// target. A missing SYSTEM.CNF is represented by Ok(None).
    pub fn boot_executable_path(&mut self) -> Result<Option<String>, Error> {
        let Some(system_cnf) = self.find_entry("SYSTEM.CNF")? else {
            return Ok(None);
        };
        if system_cnf.is_directory() {
            return Err(Error::new("SYSTEM.CNF resolves to a directory"));
        }
        let bytes = self.read_entry(&system_cnf, MAX_SYSTEM_CNF_BYTES)?;
        let text = String::from_utf8_lossy(&bytes);
        parse_boot_path(&text).map(Some)
    }

    fn read_at(&mut self, offset: u64, destination: &mut [u8]) -> Result<(), Error> {
        let end = offset.checked_add(destination.len() as u64)
            .ok_or_else(|| Error::new("ISO read range overflows"))?;
        if end > self.info.volume_bytes || end > self.info.image_bytes {
            return Err(Error::new("ISO read extends beyond the validated volume or image"));
        }
        read_image_at(&mut self.reader, self.info.image_bytes, offset, destination)
    }
}

fn read_image_at<R: Read + Seek>(
    reader: &mut R,
    image_bytes: u64,
    offset: u64,
    destination: &mut [u8],
) -> Result<(), Error> {
    let end = offset.checked_add(destination.len() as u64)
        .ok_or_else(|| Error::new("ISO image read range overflows"))?;
    if end > image_bytes {
        return Err(Error::new("ISO image read extends beyond end of file"));
    }
    reader.seek(SeekFrom::Start(offset))
        .map_err(|error| Error::new(format!("cannot seek within ISO image: {error}")))?;
    reader.read_exact(destination)
        .map_err(|error| Error::new(format!("cannot read ISO image data: {error}")))
}

fn read_both_u16(bytes: &[u8], offset: usize, label: &str) -> Result<u16, Error> {
    let little_end = bytes.get(offset..offset + 2)
        .ok_or_else(|| Error::new(format!("ISO {label} little-endian field is truncated")))?;
    let big_end = bytes.get(offset + 2..offset + 4)
        .ok_or_else(|| Error::new(format!("ISO {label} big-endian field is truncated")))?;
    let little = u16::from_le_bytes([little_end[0], little_end[1]]);
    let big = u16::from_be_bytes([big_end[0], big_end[1]]);
    if little != big {
        return Err(Error::new(format!("ISO {label} both-endian values disagree")));
    }
    Ok(little)
}

fn read_both_u32(bytes: &[u8], offset: usize, label: &str) -> Result<u32, Error> {
    let little_end = bytes.get(offset..offset + 4)
        .ok_or_else(|| Error::new(format!("ISO {label} little-endian field is truncated")))?;
    let big_end = bytes.get(offset + 4..offset + 8)
        .ok_or_else(|| Error::new(format!("ISO {label} big-endian field is truncated")))?;
    let little = u32::from_le_bytes([little_end[0], little_end[1], little_end[2], little_end[3]]);
    let big = u32::from_be_bytes([big_end[0], big_end[1], big_end[2], big_end[3]]);
    if little != big {
        return Err(Error::new(format!("ISO {label} both-endian values disagree")));
    }
    Ok(little)
}

fn decode_volume_identifier(bytes: &[u8]) -> Result<String, Error> {
    let trimmed = bytes.iter()
        .copied()
        .take_while(|byte| *byte != 0)
        .collect::<Vec<_>>();
    let value = std::str::from_utf8(&trimmed)
        .map_err(|_| Error::new("ISO volume identifier is not ASCII/UTF-8"))?;
    Ok(value.trim_end_matches(' ').to_owned())
}

fn parse_directory_record(bytes: &[u8], volume_bytes: u64) -> Result<DirectoryEntry, Error> {
    if bytes.len() < 34 {
        return Err(Error::new("ISO directory record is too short"));
    }
    let record_length = usize::from(bytes[0]);
    if record_length < 34 || record_length > bytes.len() {
        return Err(Error::new("ISO directory record length is invalid"));
    }

    let extended_attribute_blocks = u32::from(bytes[1]);
    let extent = read_both_u32(bytes, 2, "directory extent")?;
    let size_bytes = read_both_u32(bytes, 10, "directory/file size")?;
    let flags = bytes[25];
    let file_unit_size = bytes[26];
    let interleave_gap_size = bytes[27];
    let _volume_sequence = read_both_u16(bytes, 28, "volume sequence number")?;
    let identifier_length = usize::from(bytes[32]);
    let identifier_end = 33usize.checked_add(identifier_length)
        .ok_or_else(|| Error::new("ISO file identifier length overflows"))?;
    if identifier_length == 0 || identifier_end > record_length {
        return Err(Error::new("ISO file identifier is missing or truncated"));
    }
    let identifier = &bytes[33..identifier_end];
    let name = match identifier {
        [0] => ".".to_owned(),
        [1] => "..".to_owned(),
        _ => decode_identifier(identifier)?,
    };

    let data_lba = extent.checked_add(extended_attribute_blocks)
        .ok_or_else(|| Error::new("ISO extent plus extended-attribute length overflows"))?;
    let data_offset = u64::from(data_lba).checked_mul(LOGICAL_BLOCK_SIZE)
        .ok_or_else(|| Error::new("ISO data extent byte offset overflows"))?;
    let data_end = data_offset.checked_add(u64::from(size_bytes))
        .ok_or_else(|| Error::new("ISO data extent end overflows"))?;
    if data_offset > volume_bytes || data_end > volume_bytes {
        return Err(Error::new(format!("ISO entry {name} extends beyond the declared volume")));
    }

    Ok(DirectoryEntry {
        name,
        extent_lba: data_lba,
        size_bytes,
        flags,
        file_unit_size,
        interleave_gap_size,
    })
}

fn decode_identifier(bytes: &[u8]) -> Result<String, Error> {
    let value = std::str::from_utf8(bytes)
        .map_err(|_| Error::new("ISO primary-volume filename is not ASCII/UTF-8"))?;
    if value.bytes().any(|byte| byte < 0x20 || byte > 0x7E)
        || value.contains('/') || value.contains('\\') || value.contains(':')
    {
        return Err(Error::new("ISO primary-volume filename contains unsupported characters"));
    }
    Ok(value.to_owned())
}

fn normalize_identifier(value: &str) -> String {
    let without_version = value.split(';').next().unwrap_or(value);
    without_version.trim_end_matches('.').to_ascii_uppercase()
}

fn normalize_path(path: &str) -> Result<Vec<String>, Error> {
    let mut value = path.trim().trim_matches('"').to_owned();
    if let Some((device, remainder)) = value.split_once(':') {
        if !device.eq_ignore_ascii_case("cdrom0") && !device.eq_ignore_ascii_case("cdrom") {
            return Err(Error::new(format!("unsupported SYSTEM.CNF boot device: {device}")));
        }
        value = remainder.to_owned();
    }
    let value = value.replace('\\', "/");
    let mut components = Vec::new();
    for component in value.split('/') {
        let component = component.trim();
        if component.is_empty() || component == "." {
            continue;
        }
        if component == ".." {
            return Err(Error::new("ISO paths may not contain parent-directory traversal"));
        }
        let normalized = normalize_identifier(component);
        if normalized.is_empty() {
            return Err(Error::new("ISO path contains an empty filename component"));
        }
        components.push(normalized);
    }
    if components.len() > MAX_PATH_DEPTH {
        return Err(Error::new("ISO path exceeds the 32-component safety limit"));
    }
    Ok(components)
}

fn parse_boot_path(system_cnf: &str) -> Result<String, Error> {
    let mut boot2: Option<String> = None;
    let mut boot: Option<String> = None;

    for line in system_cnf.lines() {
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let key = key.trim();
        let value = value.trim();
        if key.eq_ignore_ascii_case("BOOT2") && boot2.is_none() {
            boot2 = Some(value.to_owned());
        } else if key.eq_ignore_ascii_case("BOOT") && boot.is_none() {
            boot = Some(value.to_owned());
        }
    }

    let value = boot2.or(boot)
        .ok_or_else(|| Error::new("SYSTEM.CNF contains neither BOOT2 nor BOOT"))?;
    let value = value.split_whitespace().next().unwrap_or_default().trim_matches('"');
    let components = normalize_path(value)?;
    if components.is_empty() {
        return Err(Error::new("SYSTEM.CNF boot path is empty"));
    }
    Ok(components.join("/"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    const TEST_BLOCKS: usize = 32;
    const ROOT_LBA: usize = 20;
    const SYSTEM_LBA: usize = 21;
    const EXECUTABLE_LBA: usize = 22;

    fn put_both_u16(bytes: &mut [u8], offset: usize, value: u16) {
        bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
        bytes[offset + 2..offset + 4].copy_from_slice(&value.to_be_bytes());
    }

    fn put_both_u32(bytes: &mut [u8], offset: usize, value: u32) {
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        bytes[offset + 4..offset + 8].copy_from_slice(&value.to_be_bytes());
    }

    fn directory_record(extent: u32, size: u32, flags: u8, identifier: &[u8]) -> Vec<u8> {
        let padding = if identifier.len() % 2 == 0 { 1 } else { 0 };
        let record_length = 33 + identifier.len() + padding;
        let mut record = vec![0u8; record_length];
        record[0] = record_length as u8;
        record[2..6].copy_from_slice(&extent.to_le_bytes());
        record[6..10].copy_from_slice(&extent.to_be_bytes());
        record[10..14].copy_from_slice(&size.to_le_bytes());
        record[14..18].copy_from_slice(&size.to_be_bytes());
        record[25] = flags;
        put_both_u16(&mut record, 28, 1);
        record[32] = identifier.len() as u8;
        record[33..33 + identifier.len()].copy_from_slice(identifier);
        record
    }

    fn valid_image() -> Vec<u8> {
        let mut image = vec![0u8; TEST_BLOCKS * LOGICAL_BLOCK_SIZE as usize];
        let pvd_offset = 16 * LOGICAL_BLOCK_SIZE as usize;
        let pvd = &mut image[pvd_offset..pvd_offset + LOGICAL_BLOCK_SIZE as usize];
        pvd[0] = 1;
        pvd[1..6].copy_from_slice(b"CD001");
        pvd[6] = 1;
        pvd[40..50].copy_from_slice(b"REPS2 TEST");
        put_both_u32(pvd, 80, TEST_BLOCKS as u32);
        put_both_u16(pvd, 120, 1);
        put_both_u16(pvd, 124, 1);
        put_both_u16(pvd, 128, LOGICAL_BLOCK_SIZE as u16);
        let root = directory_record(ROOT_LBA as u32, LOGICAL_BLOCK_SIZE as u32, 0x02, &[0]);
        pvd[156..156 + root.len()].copy_from_slice(&root);

        let terminator_offset = 17 * LOGICAL_BLOCK_SIZE as usize;
        let terminator = &mut image[terminator_offset..terminator_offset + LOGICAL_BLOCK_SIZE as usize];
        terminator[0] = 255;
        terminator[1..6].copy_from_slice(b"CD001");
        terminator[6] = 1;

        let system_cnf = b"BOOT2 = cdrom0:\\GAME.ELF;1\nVER = 1.00\nVMODE = NTSC\n";
        let executable = b"synthetic ps2 executable bytes";
        let root_offset = ROOT_LBA * LOGICAL_BLOCK_SIZE as usize;
        let mut cursor = root_offset;
        for record in [
            directory_record(ROOT_LBA as u32, LOGICAL_BLOCK_SIZE as u32, 0x02, &[0]),
            directory_record(ROOT_LBA as u32, LOGICAL_BLOCK_SIZE as u32, 0x02, &[1]),
            directory_record(SYSTEM_LBA as u32, system_cnf.len() as u32, 0, b"SYSTEM.CNF;1"),
            directory_record(EXECUTABLE_LBA as u32, executable.len() as u32, 0, b"GAME.ELF;1"),
        ] {
            image[cursor..cursor + record.len()].copy_from_slice(&record);
            cursor += record.len();
        }

        let system_offset = SYSTEM_LBA * LOGICAL_BLOCK_SIZE as usize;
        image[system_offset..system_offset + system_cnf.len()].copy_from_slice(system_cnf);
        let executable_offset = EXECUTABLE_LBA * LOGICAL_BLOCK_SIZE as usize;
        image[executable_offset..executable_offset + executable.len()].copy_from_slice(executable);
        image
    }

    #[test]
    fn opens_primary_volume_and_resolves_system_cnf_boot2() {
        let image = valid_image();
        let mut iso = Iso9660::open(Cursor::new(image)).expect("valid ISO");

        assert_eq!(iso.volume_info().volume_identifier, "REPS2 TEST");
        assert_eq!(iso.volume_info().logical_block_size, 2048);
        assert_eq!(iso.boot_executable_path().expect("SYSTEM.CNF"), Some("GAME.ELF".to_owned()));

        let executable = iso.read_file("cdrom0:\\GAME.ELF;1", 1024).expect("boot file");
        assert_eq!(executable, b"synthetic ps2 executable bytes");
    }

    #[test]
    fn lists_only_real_directory_entries_and_normalizes_versions() {
        let image = valid_image();
        let mut iso = Iso9660::open(Cursor::new(image)).expect("valid ISO");
        let entries = iso.root_entries().expect("root listing");
        assert_eq!(entries.len(), 2);
        assert!(entries.iter().any(|entry| entry.name == "SYSTEM.CNF;1"));
        assert!(entries.iter().any(|entry| normalize_identifier(&entry.name) == "GAME.ELF"));
    }

    #[test]
    fn rejects_non_2048_sector_images() {
        let image = vec![0u8; 17 * LOGICAL_BLOCK_SIZE as usize + 1];
        let error = Iso9660::open(Cursor::new(image)).expect_err("sector size mismatch");
        assert!(error.to_string().contains("2048-byte-sector"));
    }

    #[test]
    fn rejects_inconsistent_both_endian_volume_fields() {
        let mut image = valid_image();
        let pvd_offset = 16 * LOGICAL_BLOCK_SIZE as usize;
        image[pvd_offset + 84..pvd_offset + 88].copy_from_slice(&1u32.to_be_bytes());
        let error = Iso9660::open(Cursor::new(image)).expect_err("inconsistent volume length");
        assert!(error.to_string().contains("both-endian values disagree"));
    }

    #[test]
    fn rejects_file_extents_outside_the_declared_volume() {
        let mut image = valid_image();
        let root_offset = ROOT_LBA * LOGICAL_BLOCK_SIZE as usize;
        let mut cursor = root_offset;
        for _ in 0..3 {
            cursor += usize::from(image[cursor]);
        }
        image[cursor + 2..cursor + 6].copy_from_slice(&u32::MAX.to_le_bytes());
        image[cursor + 6..cursor + 10].copy_from_slice(&u32::MAX.to_be_bytes());
        let mut iso = Iso9660::open(Cursor::new(image)).expect("PVD valid");
        let error = iso.root_entries().expect_err("file outside volume");
        assert!(error.to_string().contains("extends beyond the declared volume"));
    }

    #[test]
    fn rejects_interleaved_and_multi_extent_files_when_read() {
        for (field, value) in [(26usize, 1u8), (27usize, 1u8), (25usize, 0x80u8)] {
            let mut image = valid_image();
            let root_offset = ROOT_LBA * LOGICAL_BLOCK_SIZE as usize;
            let mut cursor = root_offset;
            for _ in 0..3 {
                cursor += usize::from(image[cursor]);
            }
            image[cursor + field] = value;
            let mut iso = Iso9660::open(Cursor::new(image)).expect("PVD valid");
            let entries = iso.root_entries().expect("unrelated file metadata remains listable");
            let game = entries.iter().find(|entry| entry.name == "GAME.ELF;1")
                .expect("boot executable entry");
            assert!(iso.read_entry(game, 1024).is_err());
        }
    }

    #[test]
    fn rejects_parent_path_traversal_and_unknown_boot_devices() {
        assert!(normalize_path("../GAME.ELF").is_err());
        assert!(normalize_path("pfs0:/GAME.ELF").is_err());
        assert_eq!(
            parse_boot_path("BOOT = cdrom:\\GAME.ELF;1\n").expect("BOOT path"),
            "GAME.ELF"
        );
    }

    #[test]
    fn prefers_boot2_over_boot() {
        let value = parse_boot_path(
            "BOOT = cdrom:\\OLD.ELF;1\nBOOT2 = cdrom0:\\NEW.ELF;1\n",
        ).expect("boot path");
        assert_eq!(value, "NEW.ELF");
    }
}
