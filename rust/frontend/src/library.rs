// SPDX-License-Identifier: GPL-3.0-or-later
//! Per-game library import pipeline.
//!
//! One user-supplied ISO becomes one content-addressed game record. The ISO is
//! never copied or modified; the library stores its canonical source path,
//! boot ELF, deterministic metadata, and any artifacts the current AOT slice
//! can produce. A generated module is never represented as playable until a
//! complete runtime and title-stage evidence exist.

use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use crate::analysis::{self, AnalysisLimits};
use crate::control_codegen;
use crate::control_ir;
use crate::elf::{self, ParseLimits, PT_LOAD};
use crate::iso9660::{self, Iso9660};

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
        return Err(format!("ISO input is not a regular file: {}", source_path.display()));
    }

    // Hash the file through the same open handle later used by the ISO reader.
    // This is bounded-memory even for multi-gigabyte disc images.
    let mut source_file = File::open(&source_path)
        .map_err(|error| format!("cannot open ISO {}: {error}", source_path.display()))?;
    let iso_digest = sha256_reader(&mut source_file)
        .map_err(|error| format!("cannot hash ISO {}: {error}", source_path.display()))?;
    let iso_sha256 = hex_digest(&iso_digest);
    source_file.seek(SeekFrom::Start(0))
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

    let image = elf::parse(&boot_elf, ParseLimits::default())
        .map_err(|error| format!("SYSTEM.CNF target {boot_path} is not a valid supported ELF32 image: {error}"))?;
    if !image.is_mips() {
        return Err(format!("SYSTEM.CNF target {boot_path} is not EM_MIPS"));
    }
    if !image.is_executable() {
        return Err(format!("SYSTEM.CNF target {boot_path} is not ET_EXEC; dynamic/relocatable boot images are not supported yet"));
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
        .map_err(|error| format!("cannot prepare library directory {}: {error}", library_root.display()))?;
    let games_directory = library_root.join("games");
    fs::create_dir_all(&games_directory)
        .map_err(|error| format!("cannot create game library directory {}: {error}", games_directory.display()))?;
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
            hex_digest(&sha256_reader(&mut file)
                .map_err(|error| format!("cannot hash existing boot ELF: {error}"))?)
        };
        if !manifest.contains(&format!("\"iso_sha256\": \"{iso_sha256}\""))
            || !manifest.contains(&format!("\"boot_elf_sha256\": \"{boot_sha256}\""))
            || existing_boot_digest != boot_sha256
        {
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
        if generated_cpp.is_some() { "\"recompiled.cpp\"" } else { "null" },
        if main_ram.is_some() { "\"ee-main-ram.bin\"" } else { "null" },
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

    fs::rename(&staging_directory, &final_directory)
        .map_err(|error| format!("cannot publish imported game directory {}: {error}", final_directory.display()))?;
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

fn create_staging_directory(games_directory: &Path, game_id: &str) -> Result<(PathBuf, StagingGuard), String> {
    for attempt in 0..64u32 {
        let path = games_directory.join(format!(".{game_id}.staging-{}-{attempt}", std::process::id()));
        match fs::create_dir(&path) {
            Ok(()) => return Ok((path.clone(), StagingGuard(path))),
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(error) => return Err(format!("cannot create game import staging directory: {error}")),
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

fn json_optional_string(value: Option<&str>) -> String {
    value.map(json_string).unwrap_or_else(|| "null".to_owned())
}

fn json_string(value: &str) -> String {
    let mut result = String::with_capacity(value.len() + 2);
    result.push('"');
    for character in value.chars() {
        match character {
            '"' => result.push_str("\\\""),
            '\\' => result.push_str("\\\\"),
            '\n' => result.push_str("\\n"),
            '\r' => result.push_str("\\r"),
            '\t' => result.push_str("\\t"),
            character if character <= '\u{1F}' => {
                result.push_str(&format!("\\u{:04X}", u32::from(character)));
            }
            character => result.push(character),
        }
    }
    result.push('"');
    result
}

fn hex_digest(digest: &[u8; 32]) -> String {
    let mut output = String::with_capacity(64);
    for byte in digest {
        output.push_str(&format!("{byte:02x}"));
    }
    output
}

fn sha256_bytes(bytes: &[u8]) -> [u8; 32] {
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    hasher.finalize()
}

fn sha256_reader<R: Read>(reader: &mut R) -> Result<[u8; 32], String> {
    let mut hasher = Sha256::new();
    let mut buffer = [0u8; 64 * 1024];
    loop {
        let count = reader.read(&mut buffer)
            .map_err(|error| format!("read failed while hashing input: {error}"))?;
        if count == 0 {
            break;
        }
        hasher.update(&buffer[..count]);
    }
    Ok(hasher.finalize())
}

struct Sha256 {
    state: [u32; 8],
    buffer: [u8; 64],
    buffered: usize,
    total_bytes: u64,
}

impl Sha256 {
    const INITIAL_STATE: [u32; 8] = [
        0x6A09_E667, 0xBB67_AE85, 0x3C6E_F372, 0xA54F_F53A,
        0x510E_527F, 0x9B05_688C, 0x1F83_D9AB, 0x5BE0_CD19,
    ];

    const ROUND_CONSTANTS: [u32; 64] = [
        0x428A_2F98, 0x7137_4491, 0xB5C0_FBCF, 0xE9B5_DBA5,
        0x3956_C25B, 0x59F1_11F1, 0x923F_82A4, 0xAB1C_5ED5,
        0xD807_AA98, 0x1283_5B01, 0x2431_85BE, 0x550C_7DC3,
        0x72BE_5D74, 0x80DE_B1FE, 0x9BDC_06A7, 0xC19B_F174,
        0xE49B_69C1, 0xEFBE_4786, 0x0FC1_9DC6, 0x240C_A1CC,
        0x2DE9_2C6F, 0x4A74_84AA, 0x5CB0_A9DC, 0x76F9_88DA,
        0x983E_5152, 0xA831_C66D, 0xB003_27C8, 0xBF59_7FC7,
        0xC6E0_0BF3, 0xD5A7_9147, 0x06CA_6351, 0x1429_2967,
        0x27B7_0A85, 0x2E1B_2138, 0x4D2C_6DFC, 0x5338_0D13,
        0x650A_7354, 0x766A_0ABB, 0x81C2_C92E, 0x9272_2C85,
        0xA2BF_E8A1, 0xA81A_664B, 0xC24B_8B70, 0xC76C_51A3,
        0xD192_E819, 0xD699_0624, 0xF40E_3585, 0x106A_A070,
        0x19A4_C116, 0x1E37_6C08, 0x2748_774C, 0x34B0_BCB5,
        0x391C_0CB3, 0x4ED8_AA4A, 0x5B9C_CA4F, 0x682E_6FF3,
        0x748F_82EE, 0x78A5_636F, 0x84C8_7814, 0x8CC7_0208,
        0x90BE_FFFA, 0xA450_6CEB, 0xBEF9_A3F7, 0xC671_78F2,
    ];

    fn new() -> Self {
        Self {
            state: Self::INITIAL_STATE,
            buffer: [0; 64],
            buffered: 0,
            total_bytes: 0,
        }
    }

    fn update(&mut self, mut input: &[u8]) {
        self.total_bytes = self.total_bytes.wrapping_add(input.len() as u64);

        if self.buffered != 0 {
            let take = (64 - self.buffered).min(input.len());
            self.buffer[self.buffered..self.buffered + take].copy_from_slice(&input[..take]);
            self.buffered += take;
            input = &input[take..];
            if self.buffered == 64 {
                let block = self.buffer;
                self.compress(&block);
                self.buffered = 0;
            }
        }

        while input.len() >= 64 {
            let block: &[u8; 64] = input[..64].try_into().expect("64-byte SHA-256 block");
            self.compress(block);
            input = &input[64..];
        }

        if !input.is_empty() {
            self.buffer[..input.len()].copy_from_slice(input);
            self.buffered = input.len();
        }
    }

    fn finalize(mut self) -> [u8; 32] {
        let bit_length = self.total_bytes.wrapping_mul(8);
        self.buffer[self.buffered] = 0x80;
        self.buffered += 1;

        if self.buffered > 56 {
            self.buffer[self.buffered..].fill(0);
            let block = self.buffer;
            self.compress(&block);
            self.buffer = [0; 64];
            self.buffered = 0;
        } else {
            self.buffer[self.buffered..56].fill(0);
        }

        self.buffer[56..64].copy_from_slice(&bit_length.to_be_bytes());
        let block = self.buffer;
        self.compress(&block);

        let mut digest = [0u8; 32];
        for (index, word) in self.state.iter().enumerate() {
            digest[index * 4..index * 4 + 4].copy_from_slice(&word.to_be_bytes());
        }
        digest
    }

    fn compress(&mut self, block: &[u8; 64]) {
        let mut schedule = [0u32; 64];
        for (index, word) in schedule.iter_mut().take(16).enumerate() {
            let offset = index * 4;
            *word = u32::from_be_bytes([
                block[offset],
                block[offset + 1],
                block[offset + 2],
                block[offset + 3],
            ]);
        }

        for index in 16..64 {
            let x = schedule[index - 15];
            let y = schedule[index - 2];
            let sigma0 = x.rotate_right(7) ^ x.rotate_right(18) ^ (x >> 3);
            let sigma1 = y.rotate_right(17) ^ y.rotate_right(19) ^ (y >> 10);
            schedule[index] = schedule[index - 16]
                .wrapping_add(sigma0)
                .wrapping_add(schedule[index - 7])
                .wrapping_add(sigma1);
        }

        let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut h] = self.state;
        for (index, schedule_word) in schedule.iter().copied().enumerate() {
            let sum1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let choice = (e & f) ^ ((!e) & g);
            let temp1 = h
                .wrapping_add(sum1)
                .wrapping_add(choice)
                .wrapping_add(Self::ROUND_CONSTANTS[index])
                .wrapping_add(schedule_word);
            let sum0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let majority = (a & b) ^ (a & c) ^ (b & c);
            let temp2 = sum0.wrapping_add(majority);

            h = g;
            g = f;
            f = e;
            e = d.wrapping_add(temp1);
            d = c;
            c = b;
            b = a;
            a = temp1.wrapping_add(temp2);
        }

        self.state[0] = self.state[0].wrapping_add(a);
        self.state[1] = self.state[1].wrapping_add(b);
        self.state[2] = self.state[2].wrapping_add(c);
        self.state[3] = self.state[3].wrapping_add(d);
        self.state[4] = self.state[4].wrapping_add(e);
        self.state[5] = self.state[5].wrapping_add(f);
        self.state[6] = self.state[6].wrapping_add(g);
        self.state[7] = self.state[7].wrapping_add(h);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sha256_matches_standard_known_answer_vectors() {
        assert_eq!(
            hex_digest(&sha256_bytes(b"")),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
        assert_eq!(
            hex_digest(&sha256_bytes(b"abc")),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        assert_eq!(
            hex_digest(&sha256_bytes(&vec![b'a'; 1_000_000])),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
        );
    }

    #[test]
    fn sha256_is_independent_of_update_chunk_boundaries() {
        let payload = (0..=255).cycle().take(8193).collect::<Vec<_>>();
        let expected = sha256_bytes(&payload);
        let mut hasher = Sha256::new();
        for chunk in payload.chunks(37) {
            hasher.update(chunk);
        }
        assert_eq!(hasher.finalize(), expected);
    }

    #[test]
    fn serial_candidate_accepts_only_disc_serial_shape() {
        assert_eq!(serial_candidate("SLUS_212.34"), Some("SLUS_212.34".to_owned()));
        assert_eq!(serial_candidate("SCES_500.00"), Some("SCES_500.00".to_owned()));
        assert_eq!(serial_candidate("BOOT.ELF"), None);
        assert_eq!(serial_candidate("../SLUS_212.34"), None);
    }

    #[test]
    fn json_strings_escape_control_characters_and_quotes() {
        assert_eq!(json_string("a\"b\\c\n"), "\"a\\\"b\\\\c\\n\"");
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
            synthetic_iso_record(SYSTEM_LBA as u32, system_cnf.len() as u32, 0, b"SYSTEM.CNF;1"),
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
        assert_eq!(fs::read(&iso_path).expect("source ISO remains present"), original_iso);

        let manifest = fs::read_to_string(first.game_directory.join("game.json"))
            .expect("read game manifest");
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
