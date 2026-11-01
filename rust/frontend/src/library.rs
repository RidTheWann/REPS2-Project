// SPDX-License-Identifier: GPL-3.0-or-later
//! Per-game library import pipeline.
//!
//! One user-supplied ISO becomes one content-addressed game record. The ISO is
//! never copied or modified; the library stores its canonical source path,
//! boot ELF, deterministic metadata, and any artifacts the current AOT slice
//! can produce. A generated module is never represented as playable until a
//! complete runtime and title-stage evidence exist.

use std::fs::{self, File, OpenOptions};
use std::io::{Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use crate::analysis::{self, AnalysisLimits};
use crate::control_codegen;
use crate::control_ir;
use crate::digest::{hex_digest, sha256_bytes, sha256_reader};
use crate::elf::{self, PT_LOAD, ParseLimits};
use crate::iso9660::{self, Iso9660};
use crate::json::{json_optional_string, json_string};

const MANIFEST_SCHEMA_VERSION: u32 = 1;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ImportSummary {
    pub game_id: String,
    pub title: String,
    pub game_directory: PathBuf,
    pub already_imported: bool,
    pub generated_cpp: bool,
    pub generated_ram_image: bool,
}

/// Imports one local PS2 ISO into a managed per-game library directory.
///
/// Layout:
///   <library>/games/<sha256>/game.json
///   <library>/games/<sha256>/boot.elf
///   <library>/games/<sha256>/recompiled.cpp       (when lift/codegen succeeds)
///   <library>/games/<sha256>/ee-main-ram.bin      (when the current loader accepts PT_LOAD)
///
/// The original ISO remains at its source path. Import is idempotent for an
/// unchanged ISO and uses a staging directory plus atomic rename so a failed
/// import cannot leave a half-written game record.
pub fn import_disc(iso_path: &Path, library_root: &Path) -> Result<ImportSummary, String> {
    let source_path = iso_path
        .canonicalize()
        .map_err(|error| format!("cannot resolve ISO path {}: {error}", iso_path.display()))?;
    let source_metadata = fs::metadata(&source_path)
        .map_err(|error| format!("cannot inspect ISO {}: {error}", source_path.display()))?;
    if !source_metadata.is_file() {
        return Err(format!(
            "ISO input is not a regular file: {}",
            source_path.display()
        ));
    }

    // Hash the file through the same open handle later used by the ISO reader.
    // This is bounded-memory even for multi-gigabyte disc images.
    let mut source_file = File::open(&source_path)
        .map_err(|error| format!("cannot open ISO {}: {error}", source_path.display()))?;
    let iso_digest = sha256_reader(&mut source_file)
        .map_err(|error| format!("cannot hash ISO {}: {error}", source_path.display()))?;
    let iso_sha256 = hex_digest(&iso_digest);
    source_file
        .seek(SeekFrom::Start(0))
        .map_err(|error| format!("cannot rewind ISO {}: {error}", source_path.display()))?;

    let mut iso = Iso9660::open(source_file).map_err(|error| error.to_string())?;
    let volume = iso.volume_info().clone();
    let boot_path = iso
        .boot_executable_path()
        .map_err(|error| error.to_string())?
        .ok_or_else(|| "ISO does not contain SYSTEM.CNF with a BOOT2 or BOOT target".to_owned())?;
    let boot_elf = iso
        .read_file(&boot_path, iso9660::MAX_BOOT_EXECUTABLE_BYTES)
        .map_err(|error| format!("cannot read boot executable {boot_path}: {error}"))?;
    drop(iso);

    let image = elf::parse(&boot_elf, ParseLimits::default()).map_err(|error| {
        format!("SYSTEM.CNF target {boot_path} is not a valid supported ELF32 image: {error}")
    })?;
    if !image.is_mips() {
        return Err(format!("SYSTEM.CNF target {boot_path} is not EM_MIPS"));
    }
    if !image.is_executable() {
        return Err(format!(
            "SYSTEM.CNF target {boot_path} is not ET_EXEC; dynamic/relocatable boot images are not supported yet"
        ));
    }

    let boot_digest = sha256_bytes(&boot_elf);
    let boot_sha256 = hex_digest(&boot_digest);
    let boot_filename = boot_path
        .rsplit(['/', '\\'])
        .next()
        .unwrap_or(boot_path.as_str())
        .split(';')
        .next()
        .unwrap_or(boot_path.as_str())
        .to_owned();
    let serial = serial_candidate(&boot_filename);
    let title = if volume.volume_identifier.trim().is_empty() {
        boot_filename.clone()
    } else {
        volume.volume_identifier.clone()
    };
    let game_id = format!("sha256-{iso_sha256}");

    let library_root = library_root
        .canonicalize()
        .or_else(|_| {
            fs::create_dir_all(library_root)?;
            library_root.canonicalize()
        })
        .map_err(|error| {
            format!(
                "cannot prepare library directory {}: {error}",
                library_root.display()
            )
        })?;
    let games_directory = library_root.join("games");
    fs::create_dir_all(&games_directory).map_err(|error| {
        format!(
            "cannot create game library directory {}: {error}",
            games_directory.display()
        )
    })?;
    let final_directory = games_directory.join(&game_id);

    if final_directory.exists() {
        let manifest_path = final_directory.join("game.json");
        let boot_path = final_directory.join("boot.elf");
        if !manifest_path.is_file() || !boot_path.is_file() {
            return Err(format!(
                "library entry {} already exists but is incomplete; refusing to overwrite it",
                final_directory.display()
            ));
        }
        let manifest = fs::read_to_string(&manifest_path)
            .map_err(|error| format!("cannot verify existing game manifest: {error}"))?;
        let existing_boot_digest = {
            let mut file = File::open(&boot_path)
                .map_err(|error| format!("cannot verify existing boot ELF: {error}"))?;
            hex_digest(
                &sha256_reader(&mut file)
                    .map_err(|error| format!("cannot hash existing boot ELF: {error}"))?,
            )
        };
        let iso_hash_recorded = manifest.contains(&format!("\"sha256\": \"{iso_sha256}\""));
        let boot_hash_recorded = manifest.contains(&format!("\"sha256\": \"{boot_sha256}\""));
        if !iso_hash_recorded || !boot_hash_recorded || existing_boot_digest != boot_sha256 {
            return Err(format!(
                "existing library entry {} failed its content-integrity check; refusing to overwrite it",
                final_directory.display()
            ));
        }
        let generated_cpp = final_directory.join("recompiled.cpp").is_file();
        let generated_ram_image = final_directory.join("ee-main-ram.bin").is_file();
        return Ok(ImportSummary {
            game_id,
            title,
            game_directory: final_directory,
            already_imported: true,
            generated_cpp,
            generated_ram_image,
        });
    }

    let mut main_ram = None;
    let mut main_ram_error = None;
    match elf::build_main_ram_image(&boot_elf, &image) {
        Ok(ram) => main_ram = Some(ram),
        Err(error) => main_ram_error = Some(error.to_string()),
    }

    let mut analysis_status = "failed";
    let mut analysis_error: Option<String> = None;
    let mut entry_point_count = 0usize;
    let mut basic_block_count = 0usize;
    let mut decoded_instruction_count = 0usize;
    let mut static_edge_count = 0usize;
    let mut unsupported_block_count = 0usize;
    let mut unresolved_indirect_count = 0usize;
    let mut external_target_count = 0usize;

    let mut codegen_error: Option<String> = None;
    let mut generated_cpp: Option<String> = None;
    let mut translated_instruction_count = 0usize;
    let mut explicit_unsupported_count = 0usize;

    match analysis::discover_elf(&image, &boot_elf, AnalysisLimits::default()) {
        Ok(graph) => {
            analysis_status = "completed";
            entry_point_count = graph.entry_points.len();
            basic_block_count = graph.blocks.len();
            decoded_instruction_count = graph.decoded_instruction_count;
            static_edge_count = graph.edges().len();
            unsupported_block_count = graph.unsupported_block_count();
            unresolved_indirect_count = graph.unresolved_indirect_jumps.len();
            external_target_count = graph.external_targets.len();

            match control_ir::ControlProgram::lift(&graph) {
                Ok(program) => {
                    translated_instruction_count = program.instruction_count();
                    explicit_unsupported_count = program.unsupported_count();
                    match control_codegen::emit_cpp_with_entry(&program, image.entry_point) {
                        Ok(cpp) => generated_cpp = Some(cpp),
                        Err(error) => codegen_error = Some(error.to_string()),
                    }
                }
                Err(error) => codegen_error = Some(error.to_string()),
            }
        }
        Err(error) => {
            analysis_error = Some(error.to_string());
        }
    }

    let load_segments_json = image.program_headers
        .iter()
        .filter(|header| header.kind == PT_LOAD)
        .map(|header| {
            let physical = elf::ee_main_ram_physical_address(header.virtual_address)
                .map(|address| format!("\"0x{address:08X}\""))
                .unwrap_or_else(|| "null".to_owned());
            format!(
                "      {{\"virtual_address\": \"0x{:08X}\", \"physical_main_ram_address\": {}, \"flags\": {}, \"file_size\": {}, \"memory_size\": {}, \"alignment\": {}}}",
                header.virtual_address, physical, header.flags, header.file_size, header.memory_size, header.alignment
            )
        })
        .collect::<Vec<_>>()
        .join(",\n");

    let manifest = format!(
        concat!(
            "{{\n",
            "  \"schema_version\": {},\n",
            "  \"game_id\": {},\n",
            "  \"display_name\": {},\n",
            "  \"serial_candidate\": {},\n",
            "  \"source_iso\": {{\n",
            "    \"path\": {},\n",
            "    \"size_bytes\": {},\n",
            "    \"sha256\": \"{}\",\n",
            "    \"copied_into_library\": false\n",
            "  }},\n",
            "  \"disc\": {{\n",
            "    \"volume_identifier\": {},\n",
            "    \"logical_block_size\": {},\n",
            "    \"volume_space_blocks\": {},\n",
            "    \"image_size_bytes\": {}\n",
            "  }},\n",
            "  \"boot\": {{\n",
            "    \"system_cnf_target\": {},\n",
            "    \"filename\": {},\n",
            "    \"size_bytes\": {},\n",
            "    \"sha256\": \"{}\",\n",
            "    \"elf\": {{\n",
            "      \"class\": 32,\n",
            "      \"endianness\": \"little\",\n",
            "      \"machine\": {},\n",
            "      \"file_type\": {},\n",
            "      \"entry_point\": \"0x{:08X}\",\n",
            "      \"program_header_count\": {},\n",
            "      \"loadable_segment_count\": {},\n",
            "      \"section_header_count\": {},\n",
            "      \"symbol_count\": {},\n",
            "      \"relocation_count\": {},\n",
            "      \"pt_load_segments\": [\n",
            "{}\n",
            "      ]\n",
            "    }}\n",
            "  }},\n",
            "  \"analysis\": {{\n",
            "    \"status\": \"{}\",\n",
            "    \"error\": {},\n",
            "    \"entry_point_count\": {},\n",
            "    \"basic_block_count\": {},\n",
            "    \"decoded_instruction_count\": {},\n",
            "    \"static_edge_count\": {},\n",
            "    \"unsupported_block_count\": {},\n",
            "    \"unresolved_indirect_count\": {},\n",
            "    \"external_target_count\": {}\n",
            "  }},\n",
            "  \"native_build\": {{\n",
            "    \"generated_cpp\": {},\n",
            "    \"generated_ram_image\": {},\n",
            "    \"ram_image_error\": {},\n",
            "    \"codegen_error\": {},\n",
            "    \"translated_instruction_count\": {},\n",
            "    \"explicit_unsupported_count\": {},\n",
            "    \"playable\": false,\n",
            "    \"playability_status\": \"blocked_runtime_incomplete\"\n",
            "  }},\n",
            "  \"artifacts\": {{\n",
            "    \"boot_elf\": \"boot.elf\",\n",
            "    \"generated_cpp\": {},\n",
            "    \"initial_ee_ram\": {}\n",
            "  }}\n",
            "}}\n"
        ),
        MANIFEST_SCHEMA_VERSION,
        json_string(&game_id),
        json_string(&title),
        json_optional_string(serial.as_deref()),
        json_string(&source_path.to_string_lossy()),
        source_metadata.len(),
        iso_sha256,
        json_string(&volume.volume_identifier),
        volume.logical_block_size,
        volume.volume_space_blocks,
        volume.image_bytes,
        json_string(&boot_path),
        json_string(&boot_filename),
        boot_elf.len(),
        boot_sha256,
        image.machine,
        image.file_type,
        image.entry_point,
        image.program_headers.len(),
        image.loadable_segment_count(),
        image.section_headers.len(),
        image.symbols.len(),
        image.relocations.len(),
        load_segments_json,
        analysis_status,
        json_optional_string(analysis_error.as_deref()),
        entry_point_count,
        basic_block_count,
        decoded_instruction_count,
        static_edge_count,
        unsupported_block_count,
        unresolved_indirect_count,
        external_target_count,
        generated_cpp.is_some(),
        main_ram.is_some(),
        json_optional_string(main_ram_error.as_deref()),
        json_optional_string(codegen_error.as_deref()),
        translated_instruction_count,
        explicit_unsupported_count,
        if generated_cpp.is_some() {
            "\"recompiled.cpp\""
        } else {
            "null"
        },
        if main_ram.is_some() {
            "\"ee-main-ram.bin\""
        } else {
            "null"
        },
    );

    let (staging_directory, staging_guard) = create_staging_directory(&games_directory, &game_id)?;
    let write_result = (|| -> Result<(), String> {
        write_new_file(&staging_directory.join("boot.elf"), &boot_elf)?;
        if let Some(cpp) = &generated_cpp {
            write_new_file(&staging_directory.join("recompiled.cpp"), cpp.as_bytes())?;
        }
        if let Some(ram) = &main_ram {
            write_new_file(&staging_directory.join("ee-main-ram.bin"), &ram.bytes)?;
        }
        write_new_file(&staging_directory.join("game.json"), manifest.as_bytes())?;
        Ok(())
    })();

    if let Err(error) = write_result {
        let _ = fs::remove_dir_all(&staging_directory);
        return Err(error);
    }

    fs::rename(&staging_directory, &final_directory).map_err(|error| {
        format!(
            "cannot publish imported game directory {}: {error}",
            final_directory.display()
        )
    })?;
    drop(staging_guard);

    Ok(ImportSummary {
        game_id,
        title,
        game_directory: final_directory,
        already_imported: false,
        generated_cpp: generated_cpp.is_some(),
        generated_ram_image: main_ram.is_some(),
    })
}

struct StagingGuard(PathBuf);

impl Drop for StagingGuard {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

fn create_staging_directory(
    games_directory: &Path,
    game_id: &str,
) -> Result<(PathBuf, StagingGuard), String> {
    for attempt in 0..64u32 {
        let path = games_directory.join(format!(
            ".{game_id}.staging-{}-{attempt}",
            std::process::id()
        ));
        match fs::create_dir(&path) {
            Ok(()) => return Ok((path.clone(), StagingGuard(path))),
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(error) => {
                return Err(format!(
                    "cannot create game import staging directory: {error}"
                ));
            }
        }
    }
    Err("could not allocate a unique game import staging directory".to_owned())
}

fn write_new_file(path: &Path, bytes: &[u8]) -> Result<(), String> {
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .map_err(|error| format!("cannot create {}: {error}", path.display()))?;
    file.write_all(bytes)
        .and_then(|()| file.sync_all())
        .map_err(|error| format!("cannot finish writing {}: {error}", path.display()))
}

fn serial_candidate(filename: &str) -> Option<String> {
    let bytes = filename.as_bytes();
    if bytes.len() != 11
        || !bytes[..4].iter().all(u8::is_ascii_alphabetic)
        || bytes[4] != b'_'
        || !bytes[5..8].iter().all(u8::is_ascii_digit)
        || bytes[8] != b'.'
        || !bytes[9..11].iter().all(u8::is_ascii_digit)
    {
        return None;
    }
    Some(filename.to_ascii_uppercase())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn serial_candidate_accepts_only_disc_serial_shape() {
        assert_eq!(
            serial_candidate("SLUS_212.34"),
            Some("SLUS_212.34".to_owned())
        );
        assert_eq!(
            serial_candidate("SCES_500.00"),
            Some("SCES_500.00".to_owned())
        );
        assert_eq!(serial_candidate("BOOT.ELF"), None);
        assert_eq!(serial_candidate("../SLUS_212.34"), None);
    }
    fn put_iso_u16(bytes: &mut [u8], offset: usize, value: u16) {
        bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
        bytes[offset + 2..offset + 4].copy_from_slice(&value.to_be_bytes());
    }

    fn put_iso_u32(bytes: &mut [u8], offset: usize, value: u32) {
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        bytes[offset + 4..offset + 8].copy_from_slice(&value.to_be_bytes());
    }

    fn put_elf_u16(bytes: &mut [u8], offset: usize, value: u16) {
        bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
    }

    fn put_elf_u32(bytes: &mut [u8], offset: usize, value: u32) {
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }

    fn synthetic_boot_elf() -> Vec<u8> {
        let mut bytes = vec![0u8; 112];
        bytes[0..4].copy_from_slice(b"\x7FELF");
        bytes[4] = 1;
        bytes[5] = 1;
        bytes[6] = 1;
        put_elf_u16(&mut bytes, 16, elf::ET_EXEC);
        put_elf_u16(&mut bytes, 18, elf::EM_MIPS);
        put_elf_u32(&mut bytes, 20, 1);
        put_elf_u32(&mut bytes, 24, 0x0010_0000);
        put_elf_u32(&mut bytes, 28, 52);
        put_elf_u16(&mut bytes, 40, 52);
        put_elf_u16(&mut bytes, 42, 32);
        put_elf_u16(&mut bytes, 44, 1);
        put_elf_u16(&mut bytes, 46, 40);
        put_elf_u32(&mut bytes, 52, elf::PT_LOAD);
        put_elf_u32(&mut bytes, 56, 96);
        put_elf_u32(&mut bytes, 60, 0x0010_0000);
        put_elf_u32(&mut bytes, 64, 0x0010_0000);
        put_elf_u32(&mut bytes, 68, 8);
        put_elf_u32(&mut bytes, 72, 16);
        put_elf_u32(&mut bytes, 76, 5);
        put_elf_u32(&mut bytes, 80, 1);
        // Two EE NOPs, deliberately only a synthetic parser/compiler fixture.
        bytes[96..104].copy_from_slice(&[0; 8]);
        bytes
    }

    fn synthetic_iso_record(extent: u32, size: u32, flags: u8, identifier: &[u8]) -> Vec<u8> {
        let padding = usize::from(identifier.len().is_multiple_of(2));
        let record_length = 33 + identifier.len() + padding;
        let mut record = vec![0u8; record_length];
        record[0] = record_length as u8;
        put_iso_u32(&mut record, 2, extent);
        put_iso_u32(&mut record, 10, size);
        record[25] = flags;
        put_iso_u16(&mut record, 28, 1);
        record[32] = identifier.len() as u8;
        record[33..33 + identifier.len()].copy_from_slice(identifier);
        record
    }

    fn synthetic_ps2_iso() -> Vec<u8> {
        const BLOCK: usize = 2048;
        const ROOT_LBA: usize = 20;
        const SYSTEM_LBA: usize = 21;
        const ELF_LBA: usize = 22;
        const BLOCKS: usize = 32;

        let mut image = vec![0u8; BLOCK * BLOCKS];
        let pvd_offset = 16 * BLOCK;
        let pvd = &mut image[pvd_offset..pvd_offset + BLOCK];
        pvd[0] = 1;
        pvd[1..6].copy_from_slice(b"CD001");
        pvd[6] = 1;
        pvd[40..50].copy_from_slice(b"REPS2 TEST");
        put_iso_u32(pvd, 80, BLOCKS as u32);
        put_iso_u16(pvd, 120, 1);
        put_iso_u16(pvd, 124, 1);
        put_iso_u16(pvd, 128, BLOCK as u16);
        let root = synthetic_iso_record(ROOT_LBA as u32, BLOCK as u32, 0x02, &[0]);
        pvd[156..156 + root.len()].copy_from_slice(&root);

        let terminator = &mut image[17 * BLOCK..18 * BLOCK];
        terminator[0] = 255;
        terminator[1..6].copy_from_slice(b"CD001");
        terminator[6] = 1;

        let system_cnf = b"BOOT2 = cdrom0:\\GAME.ELF;1\nVER = 1.00\nVMODE = NTSC\n";
        let boot_elf = synthetic_boot_elf();
        let mut cursor = ROOT_LBA * BLOCK;
        for record in [
            synthetic_iso_record(ROOT_LBA as u32, BLOCK as u32, 0x02, &[0]),
            synthetic_iso_record(ROOT_LBA as u32, BLOCK as u32, 0x02, &[1]),
            synthetic_iso_record(
                SYSTEM_LBA as u32,
                system_cnf.len() as u32,
                0,
                b"SYSTEM.CNF;1",
            ),
            synthetic_iso_record(ELF_LBA as u32, boot_elf.len() as u32, 0, b"GAME.ELF;1"),
        ] {
            image[cursor..cursor + record.len()].copy_from_slice(&record);
            cursor += record.len();
        }
        let system_offset = SYSTEM_LBA * BLOCK;
        image[system_offset..system_offset + system_cnf.len()].copy_from_slice(system_cnf);
        let elf_offset = ELF_LBA * BLOCK;
        image[elf_offset..elf_offset + boot_elf.len()].copy_from_slice(&boot_elf);
        image
    }

    struct TestDirectory(PathBuf);

    impl TestDirectory {
        fn new(label: &str) -> Self {
            use std::sync::atomic::{AtomicUsize, Ordering};
            static NEXT: AtomicUsize = AtomicUsize::new(0);
            let path = std::env::temp_dir().join(format!(
                "reps2-library-{label}-{}-{}",
                std::process::id(),
                NEXT.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir_all(&path).expect("create isolated test directory");
            Self(path)
        }

        fn path(&self) -> &Path {
            &self.0
        }
    }

    impl Drop for TestDirectory {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    #[test]
    fn imports_one_iso_into_a_content_addressed_library_and_reimport_is_idempotent() {
        let temp = TestDirectory::new("import");
        let iso_path = temp.path().join("owned-game.iso");
        let library_path = temp.path().join("library");
        let original_iso = synthetic_ps2_iso();
        fs::write(&iso_path, &original_iso).expect("write synthetic ISO fixture");

        let first = import_disc(&iso_path, &library_path).expect("first ISO import");
        assert!(!first.already_imported);
        assert!(first.game_directory.join("game.json").is_file());
        assert!(first.game_directory.join("boot.elf").is_file());
        assert_eq!(
            fs::read(first.game_directory.join("boot.elf")).expect("read extracted boot ELF"),
            synthetic_boot_elf()
        );
        assert_eq!(
            fs::read(&iso_path).expect("source ISO remains present"),
            original_iso
        );

        let manifest =
            fs::read_to_string(first.game_directory.join("game.json")).expect("read game manifest");
        assert!(manifest.contains("\"schema_version\": 1"));
        assert!(manifest.contains("\"playable\": false"));
        assert!(manifest.contains("\"playability_status\": \"blocked_runtime_incomplete\""));
        assert!(manifest.contains("\"copied_into_library\": false"));
        assert!(manifest.contains("\"sha256\":"));

        let second = import_disc(&iso_path, &library_path).expect("idempotent re-import");
        assert!(second.already_imported);
        assert_eq!(second.game_id, first.game_id);
        assert_eq!(second.game_directory, first.game_directory);

        // Re-import must not trust a damaged cached boot executable just
        // because the manifest still contains the expected digest string.
        fs::write(first.game_directory.join("boot.elf"), b"tampered")
            .expect("tamper cached boot ELF");
        let error = import_disc(&iso_path, &library_path)
            .expect_err("tampered cached boot executable must be rejected");
        assert!(error.contains("integrity check"));
    }

    #[test]
    fn rejects_invalid_iso_without_publishing_a_partial_game_entry() {
        let temp = TestDirectory::new("invalid");
        let iso_path = temp.path().join("not-a-disc.iso");
        fs::write(&iso_path, b"not an ISO").expect("write invalid fixture");
        let library_path = temp.path().join("library");
        assert!(import_disc(&iso_path, &library_path).is_err());
        assert!(!library_path.join("games").exists());
    }
}
