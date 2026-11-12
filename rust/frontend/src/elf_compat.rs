//! PS2 ELF compatibility diagnostics.
//!
//! This module provides a structured compatibility analysis for ELF files
//! intended for the PS2 Emotion Engine (EE/R5900) target. It separates
//! format validity, architectural identity, PS2-specific compatibility,
//! REPS2 implementation support, and unresolved/unknown factors.
//!
//! The analysis does not collapse these categories into a single boolean.
//! A structurally valid ELF may still be incompatible with REPS2, and a
//! recognized R5900 ELF does not automatically mean its instructions or
//! ABI are fully supported.

use crate::elf::{self, Image, MipsAbiFlags, MipsRegInfo, MipsRegInfoSegment};
use std::fmt;

// MIPS option descriptor kinds (from MIPS ELF psABI; not public in elf.rs)
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

// MIPS ABI flags1 bit (from MIPS ELF psABI; not public in elf.rs)
const MIPS_AFL_FLAGS1_ODDSPREG: u32 = 1;

/// A structured diagnostic finding.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Diagnostic {
    /// Unique diagnostic code for tooling and filtering.
    pub code: DiagnosticCode,
    /// Severity of the finding.
    pub severity: Severity,
    /// Human-readable message.
    pub message: String,
    /// Relevant ELF field or section (e.g., "e_flags", "SHT_MIPS_ABIFLAGS").
    pub field: String,
    /// Raw value observed in the ELF.
    pub raw_value: String,
    /// Expected value or rule when documented.
    pub expected: Option<String>,
    /// Technical explanation.
    pub explanation: String,
    /// Evidence reference (spec, ADR, observed behavior).
    pub evidence: String,
    /// Which stage this affects, if any.
    pub blocks_stage: Option<BlockedStage>,
}

/// Diagnostic codes grouped by category.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
pub enum DiagnosticCode {
    // Format validity
    ElfMagicInvalid,
    ElfClassNot32,
    ElfEndianNotLittle,
    ElfIdentVersionInvalid,
    ElfHeaderSizeInvalid,
    ProgramHeaderTableOverlap,
    SectionHeaderTableOverlap,
    SectionHeaderZeroInvalid,
    SectionAlignmentInvalid,
    SectionAddressMisaligned,
    StringTableMissingNullSentinel,
    SymbolTableNullEntryInvalid,
    SymbolLocalBoundaryInvalid,
    SymbolSectionIndexInvalid,
    RelocationEntrySizeInvalid,
    RelocationSymbolIndexInvalid,
    RelocationTargetSectionInvalid,
    ExtendedNumberingInvalid,
    LoadLimitExceeded,

    // Machine/endian/ABI
    MachineNotMips,
    UnsupportedOsAbi,
    UnsupportedFileType,
    UnsupportedAbiFlagsVersion,
    UnsupportedRegInfoSize,
    UnsupportedRegInfoOrder,
    UnsupportedAbiFlagsDuplicate,

    // PS2 compatibility
    R5900NotRecognized,
    NotMipsIIIOrCompatible,
    NotEabi64,
    MissingRegInfo,
    MissingAbiFlags,
    MissingOptions,
    UnsupportedIsaExtension,
    UnsupportedAse,
    GpValueSuspicious,
    GprMaskSuspicious,
    CoprocessorMaskSuspicious,
    /// A documented MIPS processor-specific section that REPS2 preserves but
    /// does not interpret (for example `SHT_MIPS_DWARF` or `SHT_MIPS_LIBLIST`).
    MipsSectionNotInterpreted,

    // REPS2 implementation support
    RelocationUnsupported,
    SegmentOutsideMainRam,
    EntryPointNotInExecutableSegment,
    EntryPointNotWordAligned,
    AllocatedRelocationRejected,
    BssOverlapDetected,
    UnsupportedAbiFlagsVersionInSegment,
    UnsupportedRegInfoVersionInSegment,

    // Unknown/unresolved
    UnknownCpuCode,
    UnknownAbiCode,
    UnknownArchitectureCode,
    UnknownFeatureBits,
    UnknownAbiFlagsVersion,
    UnknownAbiFlagsFlags1,
    UnknownAbiFlagsFlags2,
    UnknownAseBits,
    UnknownMipsSectionType,
    UnknownMipsOptionKind,
    UnknownOsAbiValue,
    UnknownFileTypeValue,
}

impl fmt::Display for DiagnosticCode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            DiagnosticCode::ElfMagicInvalid => "ELF-MAGIC-INVALID",
            DiagnosticCode::ElfClassNot32 => "ELF-CLASS-NOT-32",
            DiagnosticCode::ElfEndianNotLittle => "ELF-ENDIAN-NOT-LITTLE",
            DiagnosticCode::ElfIdentVersionInvalid => "ELF-IDENT-VERSION-INVALID",
            DiagnosticCode::ElfHeaderSizeInvalid => "ELF-HEADER-SIZE-INVALID",
            DiagnosticCode::ProgramHeaderTableOverlap => "PROGRAM-HEADER-TABLE-OVERLAP",
            DiagnosticCode::SectionHeaderTableOverlap => "SECTION-HEADER-TABLE-OVERLAP",
            DiagnosticCode::SectionHeaderZeroInvalid => "SECTION-HEADER-ZERO-INVALID",
            DiagnosticCode::SectionAlignmentInvalid => "SECTION-ALIGNMENT-INVALID",
            DiagnosticCode::SectionAddressMisaligned => "SECTION-ADDRESS-MISALIGNED",
            DiagnosticCode::StringTableMissingNullSentinel => "STRING-TABLE-MISSING-NULL-SENTINEL",
            DiagnosticCode::SymbolTableNullEntryInvalid => "SYMBOL-TABLE-NULL-ENTRY-INVALID",
            DiagnosticCode::SymbolLocalBoundaryInvalid => "SYMBOL-LOCAL-BOUNDARY-INVALID",
            DiagnosticCode::SymbolSectionIndexInvalid => "SYMBOL-SECTION-INDEX-INVALID",
            DiagnosticCode::RelocationEntrySizeInvalid => "RELOCATION-ENTRY-SIZE-INVALID",
            DiagnosticCode::RelocationSymbolIndexInvalid => "RELOCATION-SYMBOL-INDEX-INVALID",
            DiagnosticCode::RelocationTargetSectionInvalid => "RELOCATION-TARGET-SECTION-INVALID",
            DiagnosticCode::ExtendedNumberingInvalid => "EXTENDED-NUMBERING-INVALID",
            DiagnosticCode::LoadLimitExceeded => "LOAD-LIMIT-EXCEEDED",
            DiagnosticCode::MachineNotMips => "MACHINE-NOT-MIPS",
            DiagnosticCode::UnsupportedOsAbi => "UNSUPPORTED-OS-ABI",
            DiagnosticCode::UnsupportedFileType => "UNSUPPORTED-FILE-TYPE",
            DiagnosticCode::UnsupportedAbiFlagsVersion => "UNSUPPORTED-ABI-FLAGS-VERSION",
            DiagnosticCode::UnsupportedRegInfoSize => "UNSUPPORTED-REGINFO-SIZE",
            DiagnosticCode::UnsupportedRegInfoOrder => "UNSUPPORTED-REGINFO-ORDER",
            DiagnosticCode::UnsupportedAbiFlagsDuplicate => "UNSUPPORTED-ABI-FLAGS-DUPLICATE",
            DiagnosticCode::R5900NotRecognized => "R5900-NOT-RECOGNIZED",
            DiagnosticCode::NotMipsIIIOrCompatible => "NOT-MIPS-III-OR-COMPATIBLE",
            DiagnosticCode::NotEabi64 => "NOT-EABI64",
            DiagnosticCode::MissingRegInfo => "MISSING-REGINFO",
            DiagnosticCode::MissingAbiFlags => "MISSING-ABIFLAGS",
            DiagnosticCode::MissingOptions => "MISSING-OPTIONS",
            DiagnosticCode::UnsupportedIsaExtension => "UNSUPPORTED-ISA-EXTENSION",
            DiagnosticCode::UnsupportedAse => "UNSUPPORTED-ASE",
            DiagnosticCode::GpValueSuspicious => "GP-VALUE-SUSPICIOUS",
            DiagnosticCode::GprMaskSuspicious => "GPR-MASK-SUSPICIOUS",
            DiagnosticCode::CoprocessorMaskSuspicious => "COPROCESSOR-MASK-SUSPICIOUS",
            DiagnosticCode::RelocationUnsupported => "RELOCATION-UNSUPPORTED",
            DiagnosticCode::SegmentOutsideMainRam => "SEGMENT-OUTSIDE-MAIN-RAM",
            DiagnosticCode::EntryPointNotInExecutableSegment => {
                "ENTRY-POINT-NOT-IN-EXECUTABLE-SEGMENT"
            }
            DiagnosticCode::EntryPointNotWordAligned => "ENTRY-POINT-NOT-WORD-ALIGNED",
            DiagnosticCode::AllocatedRelocationRejected => "ALLOCATED-RELOCATION-REJECTED",
            DiagnosticCode::BssOverlapDetected => "BSS-OVERLAP-DETECTED",
            DiagnosticCode::UnsupportedAbiFlagsVersionInSegment => {
                "UNSUPPORTED-ABI-FLAGS-VERSION-IN-SEGMENT"
            }
            DiagnosticCode::UnsupportedRegInfoVersionInSegment => {
                "UNSUPPORTED-REGINFO-VERSION-IN-SEGMENT"
            }
            DiagnosticCode::UnknownCpuCode => "UNKNOWN-CPU-CODE",
            DiagnosticCode::UnknownAbiCode => "UNKNOWN-ABI-CODE",
            DiagnosticCode::UnknownArchitectureCode => "UNKNOWN-ARCHITECTURE-CODE",
            DiagnosticCode::UnknownFeatureBits => "UNKNOWN-FEATURE-BITS",
            DiagnosticCode::UnknownAbiFlagsVersion => "UNKNOWN-ABI-FLAGS-VERSION",
            DiagnosticCode::UnknownAbiFlagsFlags1 => "UNKNOWN-ABI-FLAGS-FLAGS1",
            DiagnosticCode::UnknownAbiFlagsFlags2 => "UNKNOWN-ABI-FLAGS-FLAGS2",
            DiagnosticCode::UnknownAseBits => "UNKNOWN-ASE-BITS",
            DiagnosticCode::UnknownMipsSectionType => "UNKNOWN-MIPS-SECTION-TYPE",
            DiagnosticCode::MipsSectionNotInterpreted => "MIPS-SECTION-NOT-INTERPRETED",
            DiagnosticCode::UnknownMipsOptionKind => "UNKNOWN-MIPS-OPTION-KIND",
            DiagnosticCode::UnknownOsAbiValue => "UNKNOWN-OS-ABI-VALUE",
            DiagnosticCode::UnknownFileTypeValue => "UNKNOWN-FILE-TYPE-VALUE",
        };
        f.write_str(s)
    }
}

/// Severity of a diagnostic finding.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub enum Severity {
    /// Structural violation; parsing failed or would be unsound.
    Error,
    /// Valid format but incompatible with PS2 EE target expectations.
    Incompatible,
    /// Valid and recognized but not supported by current REPS2 implementation.
    Unsupported,
    /// Valid, recognized, and compatible but advisory information.
    Advisory,
    /// Metadata that cannot be classified with available knowledge.
    Unknown,
}

/// Which pipeline stage a diagnostic blocks, if any.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BlockedStage {
    Parsing,
    Translation,
    NativeBuild,
    CompatibilityConfidence,
}

/// Summary of compatibility analysis.
#[derive(Clone, Debug, Default)]
pub struct CompatibilityReport {
    /// All diagnostic findings.
    pub diagnostics: Vec<Diagnostic>,
    /// Whether the ELF is structurally valid per REPS2's parser.
    pub format_valid: bool,
    /// Whether the ELF declares EM_MIPS, ELF32, little-endian.
    pub architectural_identity_recognized: bool,
    /// Whether the ELF carries R5900/PS2-compatible metadata.
    pub ps2_compatible: bool,
    /// Whether REPS2 can translate and build this ELF with current implementation.
    pub reps2_supported: bool,
    /// Whether there are unresolved/unknown factors.
    pub has_unresolved: bool,
}

impl CompatibilityReport {
    /// Returns true if there are any error-level diagnostics.
    pub fn has_errors(&self) -> bool {
        self.diagnostics
            .iter()
            .any(|d| d.severity == Severity::Error)
    }

    /// Returns true if there are any incompatible-level diagnostics.
    pub fn has_incompatible(&self) -> bool {
        self.diagnostics
            .iter()
            .any(|d| d.severity == Severity::Incompatible)
    }

    /// Returns true if there are any unsupported-level diagnostics.
    pub fn has_unsupported(&self) -> bool {
        self.diagnostics
            .iter()
            .any(|d| d.severity == Severity::Unsupported)
    }

    /// Returns diagnostics that block a specific stage.
    pub fn blocking(&self, stage: BlockedStage) -> Vec<&Diagnostic> {
        self.diagnostics
            .iter()
            .filter(|d| d.blocks_stage == Some(stage))
            .collect()
    }

    /// Returns a summary string for CLI display.
    pub fn summary(&self) -> String {
        let mut parts = Vec::new();
        parts.push(format!(
            "Format: {}",
            if self.format_valid {
                "valid"
            } else {
                "INVALID"
            }
        ));
        parts.push(format!(
            "Arch identity: {}",
            if self.architectural_identity_recognized {
                "recognized"
            } else {
                "NOT RECOGNIZED"
            }
        ));
        parts.push(format!(
            "PS2 compatible: {}",
            if self.ps2_compatible { "yes" } else { "no" }
        ));
        parts.push(format!(
            "REPS2 support: {}",
            if self.reps2_supported { "yes" } else { "no" }
        ));
        if self.has_unresolved {
            parts.push("Unresolved: yes".to_string());
        }
        parts.join(" | ")
    }
}

/// Performs structured compatibility analysis on a parsed ELF image.
pub fn analyze_compatibility(image: &Image) -> CompatibilityReport {
    // 1. FORMAT VALIDITY
    // The parser already validated structure, but we re-check key properties
    // to produce structured diagnostics. The parser would have rejected
    // structurally invalid ELFs earlier, so format_valid is typically true
    // if we reach here.
    let mut report = CompatibilityReport {
        format_valid: true,
        ..CompatibilityReport::default()
    };

    // 2. ARCHITECTURAL IDENTITY
    let is_mips = image.machine == elf::EM_MIPS;
    let is_elf32_le = true; // Parser enforces this
    report.architectural_identity_recognized = is_mips && is_elf32_le;

    if !is_mips {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::MachineNotMips,
            severity: Severity::Incompatible,
            message: format!(
                "e_machine = {} (expected EM_MIPS = {})",
                image.machine,
                elf::EM_MIPS
            ),
            field: "e_machine".to_string(),
            raw_value: image.machine.to_string(),
            expected: Some(format!("{} (EM_MIPS)", elf::EM_MIPS)),
            explanation: "PS2 EE executables must use the MIPS architecture (EM_MIPS = 8). \
                This ELF declares a different machine type and is not a PS2 EE executable."
                .to_string(),
            evidence: "System V ABI; MIPS ELF psABI".to_string(),
            blocks_stage: Some(BlockedStage::CompatibilityConfidence),
        });
    }

    if image.file_type != elf::ET_EXEC
        && image.file_type != elf::ET_REL
        && image.file_type != elf::ET_DYN
    {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::UnsupportedFileType,
            severity: Severity::Advisory,
            message: format!("e_type = {} ({})", image.file_type, image.file_type_name()),
            field: "e_type".to_string(),
            raw_value: image.file_type.to_string(),
            expected: Some("ET_EXEC (2) for PS2 EE boot executables".to_string()),
            explanation: "PS2 EE boot executables are typically ET_EXEC. Relocatable (ET_REL) \
                or shared (ET_DYN) objects may appear in SDK libraries but are not \
                direct boot targets."
                .to_string(),
            evidence: "PS2SDK ELF loader; observed retail ELF files".to_string(),
            blocks_stage: Some(BlockedStage::CompatibilityConfidence),
        });
    }

    // OS ABI
    if image.os_abi != 0 && image.os_abi != 8 && image.os_abi < 64 {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::UnknownOsAbiValue,
            severity: Severity::Unknown,
            message: format!("EI_OSABI = {} ({})", image.os_abi, image.os_abi_name()),
            field: "EI_OSABI".to_string(),
            raw_value: image.os_abi.to_string(),
            expected: Some("0 (System V) or 8 (IRIX) for MIPS".to_string()),
            explanation: "This OS ABI value is not commonly seen in PS2 EE executables. \
                It is preserved for diagnostics but does not affect parsing."
                .to_string(),
            evidence: "ELF specification; observed PS2 toolchain output".to_string(),
            blocks_stage: Some(BlockedStage::CompatibilityConfidence),
        });
    }

    // 3. PS2 COMPATIBILITY
    if is_mips {
        analyze_mips_compatibility(image, &mut report);
    }

    // 4. REPS2 IMPLEMENTATION SUPPORT
    analyze_reps2_support(image, &mut report);

    // 5. UNRESOLVED/UNKNOWN
    analyze_unresolved(image, &mut report);

    // Final determination
    report.ps2_compatible =
        !report.has_incompatible() && is_mips && report.architectural_identity_recognized;
    report.reps2_supported =
        report.format_valid && !report.has_errors() && !report.has_unsupported();
    report.has_unresolved = report
        .diagnostics
        .iter()
        .any(|d| d.severity == Severity::Unknown);

    report
}

fn analyze_mips_compatibility(image: &Image, report: &mut CompatibilityReport) {
    // Decode MIPS e_flags
    if let Some(flags) = image.mips_elf_flags() {
        // CPU subtype
        if flags.cpu_code != 0x92 {
            if flags.cpu_code == 0 {
                report.diagnostics.push(Diagnostic {
                    code: DiagnosticCode::R5900NotRecognized,
                    severity: Severity::Advisory,
                    message: "e_flags CPU field is 0x00 (generic MIPS), not R5900 (0x92)".to_string(),
                    field: "e_flags (CPU)".to_string(),
                    raw_value: format!("0x{:02X}", flags.cpu_code),
                    expected: Some("0x92 (R5900)".to_string()),
                    explanation: "The CPU subtype field does not identify the R5900. \
                        Some PS2 toolchains may emit generic MIPS; this is advisory, not a rejection.".to_string(),
                    evidence: "PCSX2 Elfheader.h (0x20924001 example); PS2SDK".to_string(),
                    blocks_stage: Some(BlockedStage::CompatibilityConfidence),
                });
            } else {
                report.diagnostics.push(Diagnostic {
                    code: DiagnosticCode::UnknownCpuCode,
                    severity: Severity::Unknown,
                    message: format!("e_flags CPU field = 0x{:02X} ({})", flags.cpu_code, flags.cpu_name()),
                    field: "e_flags (CPU)".to_string(),
                    raw_value: format!("0x{:02X}", flags.cpu_code),
                    expected: Some("0x92 (R5900) for PS2 EE".to_string()),
                    explanation: "This CPU code is not the R5900 and is not recognized as a PS2-compatible variant.".to_string(),
                    evidence: "MIPS ELF psABI CPU field assignments".to_string(),
                    blocks_stage: Some(BlockedStage::CompatibilityConfidence),
                });
            }
        } else {
            // R5900 recognized - advisory positive
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::R5900NotRecognized, // Reusing code but as positive finding
                severity: Severity::Advisory,
                message: "e_flags identifies R5900 (CPU code 0x92)".to_string(),
                field: "e_flags (CPU)".to_string(),
                raw_value: "0x92".to_string(),
                expected: Some("0x92 (R5900)".to_string()),
                explanation:
                    "The CPU subtype field matches the PlayStation 2 Emotion Engine processor."
                        .to_string(),
                evidence: "PCSX2 Elfheader.h; PS2SDK; reverseengineering.wiki".to_string(),
                blocks_stage: None,
            });
        }

        // ABI
        if flags.abi_code != 0x4000 {
            if flags.abi_code == 0 {
                report.diagnostics.push(Diagnostic {
                    code: DiagnosticCode::NotEabi64,
                    severity: Severity::Advisory,
                    message: "e_flags ABI field is 0x0000 (unspecified), not EABI64 (0x4000)"
                        .to_string(),
                    field: "e_flags (ABI)".to_string(),
                    raw_value: format!("0x{:04X}", flags.abi_code),
                    expected: Some("0x4000 (EABI64)".to_string()),
                    explanation:
                        "The ABI field is unspecified. PS2 EE executables typically use EABI64. \
                        This does not change the ELF32 class; it is an ABI tag only."
                            .to_string(),
                    evidence: "MIPS ELF psABI; PS2SDK".to_string(),
                    blocks_stage: Some(BlockedStage::CompatibilityConfidence),
                });
            } else {
                report.diagnostics.push(Diagnostic {
                    code: DiagnosticCode::UnknownAbiCode,
                    severity: Severity::Unknown,
                    message: format!(
                        "e_flags ABI field = 0x{:04X} ({})",
                        flags.abi_code,
                        flags.abi_name()
                    ),
                    field: "e_flags (ABI)".to_string(),
                    raw_value: format!("0x{:04X}", flags.abi_code),
                    expected: Some("0x4000 (EABI64) for PS2 EE".to_string()),
                    explanation: "This ABI code is not EABI64 and is not recognized for PS2 EE."
                        .to_string(),
                    evidence: "MIPS ELF psABI ABI field assignments".to_string(),
                    blocks_stage: Some(BlockedStage::CompatibilityConfidence),
                });
            }
        }

        // Architecture level
        if flags.architecture_code != 2 && flags.architecture_code != 3 {
            if flags.architecture_code == 0 {
                // MIPS I - too old
                report.diagnostics.push(Diagnostic {
                    code: DiagnosticCode::NotMipsIIIOrCompatible,
                    severity: Severity::Incompatible,
                    message: "e_flags architecture field = 0 (MIPS I), expected MIPS III (2) or MIPS IV (3)".to_string(),
                    field: "e_flags (architecture)".to_string(),
                    raw_value: format!("{} ({})", flags.architecture_code, flags.architecture_name()),
                    expected: Some("2 (MIPS III) or 3 (MIPS IV)".to_string()),
                    explanation: "MIPS I lacks the 64-bit operations used by R5900. \
                        PS2 EE requires at least MIPS III." .to_string(),
                    evidence: "MIPS Architecture; R5900 instruction set".to_string(),
                    blocks_stage: Some(BlockedStage::CompatibilityConfidence),
                });
            } else {
                report.diagnostics.push(Diagnostic {
                    code: DiagnosticCode::UnknownArchitectureCode,
                    severity: Severity::Unknown,
                    message: format!(
                        "e_flags architecture field = {} ({})",
                        flags.architecture_code,
                        flags.architecture_name()
                    ),
                    field: "e_flags (architecture)".to_string(),
                    raw_value: flags.architecture_code.to_string(),
                    expected: Some("2 (MIPS III) or 3 (MIPS IV)".to_string()),
                    explanation:
                        "This architecture level is not MIPS III/IV. Compatibility is uncertain."
                            .to_string(),
                    evidence: "MIPS ELF psABI architecture field assignments".to_string(),
                    blocks_stage: Some(BlockedStage::CompatibilityConfidence),
                });
            }
        }

        // Feature bits
        for name in flags.feature_names() {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnknownFeatureBits,
                severity: Severity::Advisory,
                message: format!("e_flags feature: {}", name),
                field: "e_flags (features)".to_string(),
                raw_value: name.to_string(),
                expected: None,
                explanation: "Recognized MIPS feature flag. Informational only.".to_string(),
                evidence: "MIPS ELF psABI feature bit assignments".to_string(),
                blocks_stage: None,
            });
        }

        // Unknown feature bits
        if flags.unknown_bits != 0 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnknownFeatureBits,
                severity: Severity::Unknown,
                message: format!(
                    "e_flags has unclassified bits: 0x{:08X}",
                    flags.unknown_bits
                ),
                field: "e_flags (unknown bits)".to_string(),
                raw_value: format!("0x{:08X}", flags.unknown_bits),
                expected: Some("0".to_string()),
                explanation: "These bits are not classified by the current decoder. \
                    They may represent future or vendor-specific flag assignments."
                    .to_string(),
                evidence: "MIPS ELF psABI; preserved per ADR 0039".to_string(),
                blocks_stage: Some(BlockedStage::CompatibilityConfidence),
            });
        }
    }

    // Check for required MIPS-specific sections
    let has_reginfo_section = image
        .mips_reginfo
        .iter()
        .any(|r| r.section_index < image.section_headers.len());
    let has_reginfo_segment = !image.mips_reginfo_segments.is_empty();
    if !has_reginfo_section && !has_reginfo_segment {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::MissingRegInfo,
            severity: Severity::Advisory,
            message: "No SHT_MIPS_REGINFO section or PT_MIPS_REGINFO segment found".to_string(),
            field: "SHT_MIPS_REGINFO / PT_MIPS_REGINFO".to_string(),
            raw_value: "absent".to_string(),
            expected: Some("present".to_string()),
            explanation: "MIPS register usage metadata (.reginfo) is commonly present in PS2 EE executables \
                to describe GPR/CPR usage and the global pointer ($gp) value. Its absence is not fatal \
                but limits relocation analysis.".to_string(),
            evidence: "MIPS ELF psABI section 4.14; PS2SDK".to_string(),
            blocks_stage: Some(BlockedStage::Translation),
        });
    }

    let has_abi_flags_section = !image.mips_abi_flags.is_empty();
    let has_abi_flags_segment = !image.mips_abi_flags_segments.is_empty();
    if !has_abi_flags_section && !has_abi_flags_segment {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::MissingAbiFlags,
            severity: Severity::Advisory,
            message: "No SHT_MIPS_ABIFLAGS section or PT_MIPS_ABIFLAGS segment found".to_string(),
            field: "SHT_MIPS_ABIFLAGS / PT_MIPS_ABIFLAGS".to_string(),
            raw_value: "absent".to_string(),
            expected: Some("present with R5900 extension".to_string()),
            explanation: "MIPS ABI flags describe ISA level, register sizes, FP ABI, ISA extensions (e.g., R5900), \
                and ASEs. Without this, REPS2 cannot verify ISA compatibility from ELF metadata alone.".to_string(),
            evidence: "MIPS ELF psABI section 5.4; Linux MIPS VDSO linker script; ADR 0046".to_string(),
            blocks_stage: Some(BlockedStage::Translation),
        });
    }

    // Analyze SHT_MIPS_ABIFLAGS sections
    for abi_flags in &image.mips_abi_flags {
        analyze_abi_flags_section(abi_flags, report);
    }

    // Analyze PT_MIPS_ABIFLAGS segments
    for abi_flags in &image.mips_abi_flags_segments {
        analyze_abi_flags_segment(abi_flags, report);
    }

    // Analyze SHT_MIPS_REGINFO sections
    for reginfo in &image.mips_reginfo {
        analyze_reginfo_section(reginfo, report);
    }

    // Analyze PT_MIPS_REGINFO segments
    for reginfo in &image.mips_reginfo_segments {
        analyze_reginfo_segment(reginfo, report);
    }

    // Check SHT_MIPS_OPTIONS
    if image.mips_options.is_empty() {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::MissingOptions,
            severity: Severity::Advisory,
            message: "No SHT_MIPS_OPTIONS section found".to_string(),
            field: "SHT_MIPS_OPTIONS".to_string(),
            raw_value: "absent".to_string(),
            expected: Some("present with ODK_REGINFO".to_string()),
            explanation: "MIPS options sections can carry additional register usage metadata \
                (ODK_REGINFO), exception handling info (ODK_EXCEPTIONS), hardware workarounds \
                (ODK_HWPATCH), and other toolchain directives. Absence is not fatal."
                .to_string(),
            evidence: "MIPS ELF psABI; observed in some PS2 toolchain output".to_string(),
            blocks_stage: Some(BlockedStage::Translation),
        });
    } else {
        for options_section in &image.mips_options {
            for descriptor in &options_section.descriptors {
                match descriptor.kind {
                    ODK_REGINFO => {
                        // Already decoded
                    }
                    ODK_EXCEPTIONS | ODK_PAD | ODK_HWPATCH | ODK_FILL | ODK_TAGS | ODK_HWAND
                    | ODK_HWOR | ODK_GP_GROUP | ODK_IDENT | ODK_PAGESIZE => {
                        report.diagnostics.push(Diagnostic {
                            code: DiagnosticCode::UnknownMipsOptionKind,
                            severity: Severity::Advisory,
                            message: format!("SHT_MIPS_OPTIONS descriptor: {}", descriptor.kind_name()),
                            field: "SHT_MIPS_OPTIONS".to_string(),
                            raw_value: descriptor.kind_name().to_string(),
                            expected: None,
                            explanation: "Known MIPS option descriptor kind. Payload preserved but not interpreted.".to_string(),
                            evidence: "MIPS ELF psABI ODK_* definitions".to_string(),
                            blocks_stage: None,
                        });
                    }
                    _ => {
                        report.diagnostics.push(Diagnostic {
                            code: DiagnosticCode::UnknownMipsOptionKind,
                            severity: Severity::Unknown,
                            message: format!(
                                "SHT_MIPS_OPTIONS descriptor: unknown kind 0x{:02X}",
                                descriptor.kind
                            ),
                            field: "SHT_MIPS_OPTIONS".to_string(),
                            raw_value: format!("0x{:02X}", descriptor.kind),
                            expected: None,
                            explanation:
                                "Unknown MIPS option descriptor kind. Payload preserved opaquely."
                                    .to_string(),
                            evidence: "MIPS ELF psABI; unknown kinds retained per parser policy"
                                .to_string(),
                            blocks_stage: Some(BlockedStage::CompatibilityConfidence),
                        });
                    }
                }
            }
        }
    }

    // Classify MIPS processor-specific sections. The three interpreted types are
    // skipped; a documented-but-uninterpreted type gets a named advisory finding;
    // only a value the psABI does not define stays Unknown/unresolved.
    for section in &image.section_headers {
        if !(0x7000_0000..=0x7FFF_FFFF).contains(&section.kind) {
            continue;
        }
        let is_interpreted = matches!(
            section.kind,
            elf::SHT_MIPS_REGINFO | elf::SHT_MIPS_ABIFLAGS | elf::SHT_MIPS_OPTIONS
        );
        if is_interpreted {
            continue;
        }
        if let Some(name) = elf::mips_section_type_name(section.kind) {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::MipsSectionNotInterpreted,
                severity: Severity::Advisory,
                message: format!(
                    "MIPS processor-specific section {} (0x{:08X}, \"{}\") is recognized but not interpreted",
                    name, section.kind, section.name
                ),
                field: "sh_type".to_string(),
                raw_value: format!("0x{:08X}", section.kind),
                expected: None,
                explanation: "This documented MIPS metadata section is preserved but not used by REPS2 \
                    analysis, so it does not affect translation."
                    .to_string(),
                evidence: "MIPS ELF psABI processor-specific section types".to_string(),
                blocks_stage: None,
            });
        } else {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnknownMipsSectionType,
                severity: Severity::Unknown,
                message: format!(
                    "Unknown MIPS processor-specific section type: 0x{:08X} ({})",
                    section.kind, section.name
                ),
                field: "sh_type".to_string(),
                raw_value: format!("0x{:08X}", section.kind),
                expected: None,
                explanation:
                    "This section type is in the MIPS processor-specific range but is not \
                    recognized. Raw metadata is preserved; interpretation is unavailable."
                        .to_string(),
                evidence: "MIPS ELF psABI processor-specific section range".to_string(),
                blocks_stage: Some(BlockedStage::CompatibilityConfidence),
            });
        }
    }
}

fn analyze_abi_flags_section(abi_flags: &MipsAbiFlags, report: &mut CompatibilityReport) {
    if let Some(v0) = abi_flags.version_zero {
        // ISA level
        if v0.isa_level != 3 && v0.isa_level != 4 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::NotMipsIIIOrCompatible,
                severity: Severity::Incompatible,
                message: format!(
                    "SHT_MIPS_ABIFLAGS ISA level = {} ({}), expected MIPS III (3) or MIPS IV (4)",
                    v0.isa_level,
                    v0.isa_level_name()
                ),
                field: "SHT_MIPS_ABIFLAGS (isa_level)".to_string(),
                raw_value: v0.isa_level.to_string(),
                expected: Some("3 (MIPS III) or 4 (MIPS IV)".to_string()),
                explanation: "The ISA level in ABI flags does not match PS2 EE requirements. \
                    This is a stronger signal than e_flags architecture field."
                    .to_string(),
                evidence: "MIPS ELF psABI ABI flags version 0; PS2 EE ISA".to_string(),
                blocks_stage: Some(BlockedStage::Translation),
            });
        }

        // ISA extension - R5900 is 6
        if v0.isa_extension == 6 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnsupportedIsaExtension,
                severity: Severity::Advisory,
                message: "SHT_MIPS_ABIFLAGS ISA extension = 6 (R5900) recognized".to_string(),
                field: "SHT_MIPS_ABIFLAGS (isa_extension)".to_string(),
                raw_value: v0.isa_extension.to_string(),
                expected: Some("6 (R5900)".to_string()),
                explanation:
                    "The ISA extension field matches the PlayStation 2 Emotion Engine processor."
                        .to_string(),
                evidence: "MIPS ELF psABI AFL_EXT_5900 = 6; ADR 0046".to_string(),
                blocks_stage: None,
            });
        } else if v0.isa_extension == 0 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnsupportedIsaExtension,
                severity: Severity::Advisory,
                message: "SHT_MIPS_ABIFLAGS ISA extension = 0 (none), not R5900 (6)".to_string(),
                field: "SHT_MIPS_ABIFLAGS (isa_extension)".to_string(),
                raw_value: v0.isa_extension.to_string(),
                expected: Some("6 (R5900)".to_string()),
                explanation: "The ISA extension field does not identify the R5900. \
                    Some toolchains may omit this; it is advisory."
                    .to_string(),
                evidence: "MIPS ELF psABI AFL_EXT_5900 = 6; ADR 0046".to_string(),
                blocks_stage: Some(BlockedStage::CompatibilityConfidence),
            });
        } else {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnsupportedIsaExtension,
                severity: Severity::Incompatible,
                message: format!(
                    "SHT_MIPS_ABIFLAGS ISA extension = {} ({}), expected 6 (R5900)",
                    v0.isa_extension,
                    v0.isa_extension_name()
                ),
                field: "SHT_MIPS_ABIFLAGS (isa_extension)".to_string(),
                raw_value: v0.isa_extension.to_string(),
                expected: Some("6 (R5900)".to_string()),
                explanation:
                    "The ISA extension identifies a different MIPS processor variant, not R5900."
                        .to_string(),
                evidence: "MIPS ELF psABI ISA extension assignments".to_string(),
                blocks_stage: Some(BlockedStage::CompatibilityConfidence),
            });
        }

        // FP ABI
        if v0.fp_abi != 3 {
            // 3 = soft-float, commonly used on PS2 EE
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnsupportedAbiFlagsVersion,
                severity: Severity::Advisory,
                message: format!(
                    "SHT_MIPS_ABIFLAGS FP ABI = {} ({}), PS2 EE typically uses soft-float (3)",
                    v0.fp_abi,
                    v0.fp_abi_name()
                ),
                field: "SHT_MIPS_ABIFLAGS (fp_abi)".to_string(),
                raw_value: v0.fp_abi.to_string(),
                expected: Some("3 (soft-float)".to_string()),
                explanation: "The FP ABI differs from the typical PS2 EE convention. \
                    This may indicate a different toolchain configuration."
                    .to_string(),
                evidence: "MIPS ELF psABI FP ABI assignments; PS2SDK".to_string(),
                blocks_stage: Some(BlockedStage::CompatibilityConfidence),
            });
        }

        // GPR size
        if v0.gpr_size != 1 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnsupportedAbiFlagsVersion,
                severity: Severity::Incompatible,
                message: format!(
                    "SHT_MIPS_ABIFLAGS GPR size = {} ({}), PS2 EE uses 32-bit GPRs (1)",
                    v0.gpr_size,
                    MipsAbiFlagsV0::register_size_name(v0.gpr_size)
                ),
                field: "SHT_MIPS_ABIFLAGS (gpr_size)".to_string(),
                raw_value: v0.gpr_size.to_string(),
                expected: Some("1 (32-bit)".to_string()),
                explanation:
                    "PS2 EE is a 32-bit GPR architecture despite having 64-bit operations. \
                    A 64-bit GPR size would be incompatible."
                        .to_string(),
                evidence: "MIPS ELF psABI; R5900 architecture".to_string(),
                blocks_stage: Some(BlockedStage::Translation),
            });
        }

        // CPR1 size
        if v0.cpr1_size != 1 && v0.cpr1_size != 2 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnsupportedAbiFlagsVersion,
                severity: Severity::Advisory,
                message: format!(
                    "SHT_MIPS_ABIFLAGS CPR1 size = {} ({})",
                    v0.cpr1_size,
                    MipsAbiFlagsV0::register_size_name(v0.cpr1_size)
                ),
                field: "SHT_MIPS_ABIFLAGS (cpr1_size)".to_string(),
                raw_value: v0.cpr1_size.to_string(),
                expected: Some("1 (32-bit) or 2 (64-bit)".to_string()),
                explanation: "Unusual CPR1 register size. PS2 EE typically uses 32-bit FPRs."
                    .to_string(),
                evidence: "MIPS ELF psABI; PS2 EE FPU".to_string(),
                blocks_stage: Some(BlockedStage::CompatibilityConfidence),
            });
        }

        // ASEs
        let known_ases = v0.ase_names();
        let unknown_ases = v0.unknown_ases();
        if !known_ases.is_empty() {
            for ase in known_ases {
                report.diagnostics.push(Diagnostic {
                    code: DiagnosticCode::UnsupportedAse,
                    severity: Severity::Advisory,
                    message: format!("SHT_MIPS_ABIFLAGS ASE: {}", ase),
                    field: "SHT_MIPS_ABIFLAGS (ases)".to_string(),
                    raw_value: ase.to_string(),
                    expected: None,
                    explanation: "Recognized Application-Specific Extension. Informational."
                        .to_string(),
                    evidence: "MIPS ELF psABI ASE assignments".to_string(),
                    blocks_stage: None,
                });
            }
        }
        if unknown_ases != 0 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnknownAseBits,
                severity: Severity::Unknown,
                message: format!("SHT_MIPS_ABIFLAGS unknown ASE bits: 0x{:08X}", unknown_ases),
                field: "SHT_MIPS_ABIFLAGS (ases)".to_string(),
                raw_value: format!("0x{:08X}", unknown_ases),
                expected: Some("0".to_string()),
                explanation: "These ASE bits are not classified by the current decoder."
                    .to_string(),
                evidence: "MIPS ELF psABI; preserved per parser policy".to_string(),
                blocks_stage: Some(BlockedStage::CompatibilityConfidence),
            });
        }

        // flags1
        if v0.flags1 & MIPS_AFL_FLAGS1_ODDSPREG != 0 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnsupportedAbiFlagsVersion,
                severity: Severity::Advisory,
                message:
                    "SHT_MIPS_ABIFLAGS flags1: odd single-precision registers (AFL_FLAGS1_ODDSPREG)"
                        .to_string(),
                field: "SHT_MIPS_ABIFLAGS (flags1)".to_string(),
                raw_value: "ODDSPREG".to_string(),
                expected: None,
                explanation: "Indicates use of odd-numbered single-precision FP registers. \
                    This is a valid MIPS feature."
                    .to_string(),
                evidence: "MIPS ELF psABI flags1 assignments".to_string(),
                blocks_stage: None,
            });
        }
        let unknown_flags1 = v0.unknown_flags1();
        if unknown_flags1 != 0 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnknownAbiFlagsFlags1,
                severity: Severity::Unknown,
                message: format!(
                    "SHT_MIPS_ABIFLAGS unknown flags1 bits: 0x{:08X}",
                    unknown_flags1
                ),
                field: "SHT_MIPS_ABIFLAGS (flags1)".to_string(),
                raw_value: format!("0x{:08X}", unknown_flags1),
                expected: Some("0".to_string()),
                explanation: "Unclassified flags1 bits.".to_string(),
                evidence: "MIPS ELF psABI; preserved per parser policy".to_string(),
                blocks_stage: Some(BlockedStage::CompatibilityConfidence),
            });
        }

        // flags2
        if v0.flags2 != 0 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnknownAbiFlagsFlags2,
                severity: Severity::Unknown,
                message: format!("SHT_MIPS_ABIFLAGS flags2 = 0x{:08X}", v0.flags2),
                field: "SHT_MIPS_ABIFLAGS (flags2)".to_string(),
                raw_value: format!("0x{:08X}", v0.flags2),
                expected: Some("0".to_string()),
                explanation:
                    "flags2 is reserved in the MIPS ABI; non-zero values are vendor-specific."
                        .to_string(),
                evidence: "MIPS ELF psABI flags2 reservation".to_string(),
                blocks_stage: Some(BlockedStage::CompatibilityConfidence),
            });
        }
    } else {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::UnknownAbiFlagsVersion,
            severity: Severity::Unknown,
            message: format!(
                "SHT_MIPS_ABIFLAGS version {} (not version 0)",
                abi_flags.version
            ),
            field: "SHT_MIPS_ABIFLAGS (version)".to_string(),
            raw_value: abi_flags.version.to_string(),
            expected: Some("0".to_string()),
            explanation: "Unknown ABI flags version. Payload preserved but not interpreted."
                .to_string(),
            evidence: "MIPS ELF psABI versioned ABI flags".to_string(),
            blocks_stage: Some(BlockedStage::CompatibilityConfidence),
        });
    }
}

fn analyze_abi_flags_segment(segment: &elf::MipsAbiFlagsSegment, report: &mut CompatibilityReport) {
    if let Some(v0) = segment.version_zero {
        // Same checks as section form
        if v0.isa_extension != 6 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnsupportedIsaExtension,
                severity: Severity::Advisory,
                message: format!(
                    "PT_MIPS_ABIFLAGS ISA extension = {} ({}), expected 6 (R5900)",
                    v0.isa_extension,
                    v0.isa_extension_name()
                ),
                field: "PT_MIPS_ABIFLAGS (isa_extension)".to_string(),
                raw_value: v0.isa_extension.to_string(),
                expected: Some("6 (R5900)".to_string()),
                explanation: "Program-header form of ABI flags also carries ISA extension."
                    .to_string(),
                evidence: "Linux MIPS VDSO linker script; ADR 0046".to_string(),
                blocks_stage: Some(BlockedStage::CompatibilityConfidence),
            });
        }
    } else if segment.version != 0 {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::UnsupportedAbiFlagsVersionInSegment,
            severity: Severity::Unknown,
            message: format!(
                "PT_MIPS_ABIFLAGS version {} (not version 0)",
                segment.version
            ),
            field: "PT_MIPS_ABIFLAGS (version)".to_string(),
            raw_value: segment.version.to_string(),
            expected: Some("0".to_string()),
            explanation: "Unknown ABI flags version in program header. Payload preserved."
                .to_string(),
            evidence: "Linux MIPS ELF definitions; ADR 0046".to_string(),
            blocks_stage: Some(BlockedStage::CompatibilityConfidence),
        });
    }
}

fn analyze_reginfo_section(reginfo: &MipsRegInfo, report: &mut CompatibilityReport) {
    if reginfo.gpr_mask == 0 {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::GprMaskSuspicious,
            severity: Severity::Advisory,
            message: "SHT_MIPS_REGINFO GPR mask is zero (no GPRs used)".to_string(),
            field: "SHT_MIPS_REGINFO (gpr_mask)".to_string(),
            raw_value: format!("0x{:08X}", reginfo.gpr_mask),
            expected: Some("non-zero".to_string()),
            explanation: "A zero GPR mask is unusual for an executable. This may indicate \
                stripped metadata or an atypical toolchain output."
                .to_string(),
            evidence: "MIPS ELF psABI RegInfo definition".to_string(),
            blocks_stage: Some(BlockedStage::CompatibilityConfidence),
        });
    }

    // Check $gp value reasonableness
    let gp = reginfo.gp_value;
    if gp != 0 && (gp as u32) < 0x0010_0000 {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::GpValueSuspicious,
            severity: Severity::Advisory,
            message: format!(
                "SHT_MIPS_REGINFO $gp = 0x{:08X} (unusually low for PS2 EE)",
                gp
            ),
            field: "SHT_MIPS_REGINFO (gp_value)".to_string(),
            raw_value: format!("0x{:08X}", gp as u32),
            expected: Some("typically 0x003xxxxx or similar in PS2 EE".to_string()),
            explanation: "The global pointer value is lower than typical PS2 EE executables. \
                This may be valid for small executables or different link layouts."
                .to_string(),
            evidence: "Observed PS2 EE ELF $gp values; MIPS GP-relative addressing".to_string(),
            blocks_stage: Some(BlockedStage::CompatibilityConfidence),
        });
    }
}

fn analyze_reginfo_segment(segment: &MipsRegInfoSegment, report: &mut CompatibilityReport) {
    // Same checks as section form
    if segment.gpr_mask == 0 {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::GprMaskSuspicious,
            severity: Severity::Advisory,
            message: "PT_MIPS_REGINFO GPR mask is zero".to_string(),
            field: "PT_MIPS_REGINFO (gpr_mask)".to_string(),
            raw_value: format!("0x{:08X}", segment.gpr_mask),
            expected: Some("non-zero".to_string()),
            explanation: "Program-header form of register info also carries GPR mask.".to_string(),
            evidence: "MIPS ELF psABI PT_MIPS_REGINFO".to_string(),
            blocks_stage: Some(BlockedStage::CompatibilityConfidence),
        });
    }
}

fn analyze_reps2_support(image: &Image, report: &mut CompatibilityReport) {
    // Check relocations
    for relocation in &image.relocations {
        if image
            .section_headers
            .get(relocation.target_section_index)
            .is_some_and(|t| t.flags & elf::SHF_ALLOC != 0)
        {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::AllocatedRelocationRejected,
                severity: Severity::Unsupported,
                message: format!("Allocated relocation type {} at 0x{:08X} is not supported",
                    relocation.relocation_type, relocation.offset),
                field: "REL/RELA".to_string(),
                raw_value: format!("type {} at 0x{:08X}", relocation.relocation_type, relocation.offset),
                expected: Some("non-allocated relocations only".to_string()),
                explanation: "REPS2 currently rejects ELF relocations that target allocated \
                    (SHF_ALLOC) sections. These require a relocation engine that is not yet implemented.".to_string(),
                evidence: "REPS2 parser behavior; ADR 0033".to_string(),
                blocks_stage: Some(BlockedStage::NativeBuild),
            });
        }
    }

    // Check PT_LOAD segments for main RAM compatibility
    for header in &image.program_headers {
        if header.kind == elf::PT_LOAD
            && header.memory_size > 0
            && elf::ee_main_ram_physical_address(header.virtual_address).is_none()
        {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::SegmentOutsideMainRam,
                severity: Severity::Unsupported,
                message: format!("PT_LOAD at 0x{:08X} is outside supported EE main-RAM aliases",
                    header.virtual_address),
                field: "PT_LOAD".to_string(),
                raw_value: format!("vaddr=0x{:08X} memsz=0x{:X}", header.virtual_address, header.memory_size),
                expected: Some("0x00000000-0x01FFFFFF, 0x20000000-0x21FFFFFF, 0x30100000-0x31FFFFFF, 0x80000000-0x9FFFFFFF, 0xA0000000-0xBFFFFFFF".to_string()),
                explanation: "This segment maps to an address range not covered by REPS2's \
                    default EE main-RAM physical mapping. MMIO, BIOS, IOP, VU, and scratchpad \
                    memory require COP0/TLB support not yet implemented.".to_string(),
                evidence: "EE memory map; REPS2 elf.rs ee_main_ram_physical_address".to_string(),
                blocks_stage: Some(BlockedStage::NativeBuild),
            });
        }
    }

    // Entry point checks
    if image.entry_point & 3 != 0 {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::EntryPointNotWordAligned,
            severity: Severity::Error,
            message: format!(
                "Entry point 0x{:08X} is not word-aligned",
                image.entry_point
            ),
            field: "e_entry".to_string(),
            raw_value: format!("0x{:08X}", image.entry_point),
            expected: Some("4-byte aligned".to_string()),
            explanation:
                "MIPS instructions must be word-aligned. An unaligned entry point is invalid."
                    .to_string(),
            evidence: "MIPS architecture; REPS2 build_main_ram_image validation".to_string(),
            blocks_stage: Some(BlockedStage::Parsing),
        });
    }

    // Check if entry point is in executable PT_LOAD
    let entry_in_executable = image.program_headers.iter().any(|h| {
        h.kind == elf::PT_LOAD
            && h.flags & elf::PF_X != 0
            && image.entry_point >= h.virtual_address
            && image.entry_point < h.virtual_address + h.file_size
    });
    if !entry_in_executable && image.is_executable() {
        report.diagnostics.push(Diagnostic {
            code: DiagnosticCode::EntryPointNotInExecutableSegment,
            severity: Severity::Error,
            message: format!(
                "Entry point 0x{:08X} is not inside file-backed executable PT_LOAD data",
                image.entry_point
            ),
            field: "e_entry / PT_LOAD".to_string(),
            raw_value: format!("0x{:08X}", image.entry_point),
            expected: Some("inside executable PT_LOAD file_size range".to_string()),
            explanation:
                "The entry point must point to actual executable code in a PT_LOAD segment."
                    .to_string(),
            evidence: "REPS2 build_main_ram_image validation".to_string(),
            blocks_stage: Some(BlockedStage::NativeBuild),
        });
    }
}

fn analyze_unresolved(image: &Image, report: &mut CompatibilityReport) {
    // MIPS processor-specific sections are classified in
    // analyze_mips_compatibility; keeping that classification in one place means
    // each section type is reported exactly once.

    // Check program headers for unknown MIPS types
    for header in &image.program_headers {
        if header.kind >= 0x7000_0000 && header.kind <= 0x7FFF_FFFF {
            let is_known = matches!(header.kind, elf::PT_MIPS_REGINFO | elf::PT_MIPS_ABIFLAGS);
            if !is_known {
                report.diagnostics.push(Diagnostic {
                    code: DiagnosticCode::UnknownMipsSectionType, // Reusing for program headers too
                    severity: Severity::Unknown,
                    message: format!(
                        "Unrecognized MIPS program header type 0x{:08X}",
                        header.kind
                    ),
                    field: "p_type".to_string(),
                    raw_value: format!("0x{:08X}", header.kind),
                    expected: None,
                    explanation:
                        "This MIPS processor-specific program header type is not recognized."
                            .to_string(),
                    evidence: "MIPS ELF psABI processor-specific program header range".to_string(),
                    blocks_stage: Some(BlockedStage::CompatibilityConfidence),
                });
            }
        }
    }

    // Symbol and relocation types
    for symbol in &image.symbols {
        // Check for unusual symbol types
        if symbol.kind > 15 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::UnknownMipsSectionType, // Reusing
                severity: Severity::Unknown,
                message: format!("Unusual symbol type {}: {}", symbol.kind, symbol.name),
                field: "st_info (type)".to_string(),
                raw_value: symbol.kind.to_string(),
                expected: Some("0..=15 (ELF standard)".to_string()),
                explanation: "Symbol type exceeds standard ELF range. May be MIPS-specific."
                    .to_string(),
                evidence: "ELF specification; MIPS psABI".to_string(),
                blocks_stage: Some(BlockedStage::CompatibilityConfidence),
            });
        }
    }

    for relocation in &image.relocations {
        // R_MIPS relocation types go up to ~100+, many are unsupported
        if relocation.relocation_type > 50 {
            report.diagnostics.push(Diagnostic {
                code: DiagnosticCode::RelocationUnsupported,
                severity: Severity::Unknown,
                message: format!(
                    "High relocation type {} (may be MIPS-specific)",
                    relocation.relocation_type
                ),
                field: "r_info (type)".to_string(),
                raw_value: relocation.relocation_type.to_string(),
                expected: Some("standard MIPS relocation types".to_string()),
                explanation:
                    "This relocation type may require MIPS-specific handling not yet implemented."
                        .to_string(),
                evidence: "MIPS ELF psABI relocation types".to_string(),
                blocks_stage: Some(BlockedStage::Translation),
            });
        }
    }
}

/// MIPS ABI flags version 0 - re-exported for compatibility checks.
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

    #[must_use]
    pub const fn has_odd_single_precision_registers(self) -> bool {
        self.flags1 & MIPS_AFL_FLAGS1_ODDSPREG != 0
    }

    #[must_use]
    pub const fn unknown_flags1(self) -> u32 {
        self.flags1 & !MIPS_AFL_FLAGS1_ODDSPREG
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::elf::{
        EM_MIPS, ET_EXEC, Image, MipsAbiFlags, MipsAbiFlagsV0, PF_R, PF_X, PT_LOAD, ProgramHeader,
        SHF_ALLOC, SHF_EXECINSTR, SHT_PROGBITS, SHT_STRTAB, SectionHeader,
    };

    fn minimal_mips_elf() -> Image {
        Image {
            file_type: ET_EXEC,
            os_abi: 0,
            abi_version: 0,
            machine: EM_MIPS,
            entry_point: 0x0010_0000,
            flags: 0x2092_4001, // R5900, EABI64, MIPS III, NOREORDER
            section_name_table_index: 1,
            program_headers: vec![ProgramHeader {
                kind: PT_LOAD,
                offset: 52,
                virtual_address: 0x0010_0000,
                physical_address: 0x0010_0000,
                file_size: 16,
                memory_size: 32,
                flags: PF_R | PF_X,
                alignment: 16,
            }],
            section_headers: vec![
                SectionHeader {
                    name: "".to_string(),
                    name_offset: 0,
                    kind: 0,
                    flags: 0,
                    virtual_address: 0,
                    offset: 0,
                    size: 0,
                    link: 0,
                    info: 0,
                    alignment: 0,
                    entry_size: 0,
                },
                SectionHeader {
                    name: ".text".to_string(),
                    name_offset: 1,
                    kind: SHT_PROGBITS,
                    flags: SHF_ALLOC | SHF_EXECINSTR,
                    virtual_address: 0x0010_0000,
                    offset: 52,
                    size: 16,
                    link: 0,
                    info: 0,
                    alignment: 16,
                    entry_size: 0,
                },
                SectionHeader {
                    name: ".shstrtab".to_string(),
                    name_offset: 7,
                    kind: SHT_STRTAB,
                    flags: 0,
                    virtual_address: 0,
                    offset: 68,
                    size: 16,
                    link: 0,
                    info: 0,
                    alignment: 1,
                    entry_size: 0,
                },
            ],
            symbols: vec![],
            relocations: vec![],
            mips_reginfo: vec![],
            mips_reginfo_segments: vec![],
            mips_abi_flags: vec![],
            mips_abi_flags_segments: vec![],
            mips_options: vec![],
        }
    }

    #[test]
    fn r5900_elf_is_recognized_as_compatible() {
        let image = minimal_mips_elf();
        let report = analyze_compatibility(&image);

        assert!(report.format_valid);
        assert!(report.architectural_identity_recognized);
        assert!(report.ps2_compatible);
        assert!(!report.has_errors());
        // R5900 recognized
        let r5900_diag = report
            .diagnostics
            .iter()
            .find(|d| d.message.contains("R5900") || d.code == DiagnosticCode::R5900NotRecognized);
        assert!(r5900_diag.is_some(), "Should have R5900 diagnostic");
    }

    #[test]
    fn non_mips_elf_is_incompatible() {
        let mut image = minimal_mips_elf();
        image.machine = 3; // EM_386

        let report = analyze_compatibility(&image);

        assert!(!report.architectural_identity_recognized);
        assert!(!report.ps2_compatible);
        assert!(report.has_incompatible());
        let machine_diag = report
            .diagnostics
            .iter()
            .find(|d| d.code == DiagnosticCode::MachineNotMips);
        assert!(machine_diag.is_some());
        assert_eq!(machine_diag.unwrap().severity, Severity::Incompatible);
    }

    #[test]
    fn mips_i_architecture_is_incompatible() {
        let mut image = minimal_mips_elf();
        // Architecture = 0 (MIPS I)
        image.flags &= !0xF000_0000;

        let report = analyze_compatibility(&image);

        assert!(report.has_incompatible());
        let arch_diag = report
            .diagnostics
            .iter()
            .find(|d| d.code == DiagnosticCode::NotMipsIIIOrCompatible);
        assert!(arch_diag.is_some());
        assert_eq!(arch_diag.unwrap().severity, Severity::Incompatible);
    }

    #[test]
    fn unknown_cpu_code_is_unknown() {
        let mut image = minimal_mips_elf();
        // CPU = 0xAA (unrecognized)
        image.flags = (image.flags & !0x00FF_0000) | 0x00AA_0000;

        let report = analyze_compatibility(&image);

        assert!(report.has_unresolved);
        let cpu_diag = report
            .diagnostics
            .iter()
            .find(|d| d.code == DiagnosticCode::UnknownCpuCode);
        assert!(cpu_diag.is_some());
        assert_eq!(cpu_diag.unwrap().severity, Severity::Unknown);
    }

    #[test]
    fn allocated_relocation_is_unsupported() {
        let mut image = minimal_mips_elf();
        image.relocations.push(crate::elf::Relocation {
            offset: 0x1000,
            symbol_index: 0,
            relocation_type: 2, // R_MIPS_32
            addend: None,
            relocation_section_index: 0,
            target_section_index: 1, // .text (SHF_ALLOC)
            symbol_table_section_index: 0,
        });
        // Make .text have SHF_ALLOC
        image.section_headers[1].flags = SHF_ALLOC | SHF_EXECINSTR;

        let report = analyze_compatibility(&image);

        assert!(report.has_unsupported());
        let reloc_diag = report
            .diagnostics
            .iter()
            .find(|d| d.code == DiagnosticCode::AllocatedRelocationRejected);
        assert!(reloc_diag.is_some());
        assert_eq!(reloc_diag.unwrap().severity, Severity::Unsupported);
        assert_eq!(
            reloc_diag.unwrap().blocks_stage,
            Some(BlockedStage::NativeBuild)
        );
    }

    #[test]
    fn segment_outside_main_ram_is_unsupported() {
        let mut image = minimal_mips_elf();
        // Add a PT_LOAD at MMIO address
        image.program_headers.push(ProgramHeader {
            kind: PT_LOAD,
            offset: 100,
            virtual_address: 0x1000_0000, // MMIO
            physical_address: 0x1000_0000,
            file_size: 4,
            memory_size: 4,
            flags: PF_R,
            alignment: 4,
        });

        let report = analyze_compatibility(&image);

        assert!(report.has_unsupported());
        let seg_diag = report
            .diagnostics
            .iter()
            .find(|d| d.code == DiagnosticCode::SegmentOutsideMainRam);
        assert!(seg_diag.is_some());
        assert_eq!(seg_diag.unwrap().severity, Severity::Unsupported);
    }

    #[test]
    fn abi_flags_r5900_extension_recognized() {
        let mut image = minimal_mips_elf();
        image.mips_abi_flags.push(MipsAbiFlags {
            section_index: 3,
            section_size: 24,
            version: 0,
            version_zero: Some(MipsAbiFlagsV0 {
                isa_level: 3,
                isa_revision: 0,
                gpr_size: 1,
                cpr1_size: 1,
                cpr2_size: 0,
                fp_abi: 3,
                isa_extension: 6, // R5900
                ases: 1,          // DSP
                flags1: 0,
                flags2: 0,
            }),
        });

        let report = analyze_compatibility(&image);

        // Should have positive R5900 detection from ABI flags
        let isa_ext_diag = report
            .diagnostics
            .iter()
            .find(|d| d.message.contains("R5900") && d.field.contains("isa_extension"));
        assert!(
            isa_ext_diag.is_some(),
            "Should recognize R5900 ISA extension"
        );
    }

    #[test]
    fn abi_flags_wrong_isa_extension_is_incompatible() {
        let mut image = minimal_mips_elf();
        image.mips_abi_flags.push(MipsAbiFlags {
            section_index: 3,
            section_size: 24,
            version: 0,
            version_zero: Some(MipsAbiFlagsV0 {
                isa_level: 3,
                isa_revision: 0,
                gpr_size: 1,
                cpr1_size: 1,
                cpr2_size: 0,
                fp_abi: 3,
                isa_extension: 5, // Octeon, not R5900
                ases: 0,
                flags1: 0,
                flags2: 0,
            }),
        });

        let report = analyze_compatibility(&image);

        assert!(report.has_incompatible());
        let isa_diag = report.diagnostics.iter().find(|d| {
            d.code == DiagnosticCode::UnsupportedIsaExtension
                && d.severity == Severity::Incompatible
        });
        assert!(isa_diag.is_some());
    }

    #[test]
    fn gpr_size_64bit_is_incompatible() {
        let mut image = minimal_mips_elf();
        image.mips_abi_flags.push(MipsAbiFlags {
            section_index: 3,
            section_size: 24,
            version: 0,
            version_zero: Some(MipsAbiFlagsV0 {
                isa_level: 3,
                isa_revision: 0,
                gpr_size: 2, // 64-bit - incompatible with PS2 EE
                cpr1_size: 1,
                cpr2_size: 0,
                fp_abi: 3,
                isa_extension: 6,
                ases: 0,
                flags1: 0,
                flags2: 0,
            }),
        });

        let report = analyze_compatibility(&image);

        assert!(report.has_incompatible());
        let gpr_diag = report
            .diagnostics
            .iter()
            .find(|d| d.message.contains("GPR size") && d.severity == Severity::Incompatible);
        assert!(gpr_diag.is_some());
    }

    #[test]
    fn missing_reginfo_is_advisory() {
        let image = minimal_mips_elf();
        let report = analyze_compatibility(&image);

        let reginfo_diag = report
            .diagnostics
            .iter()
            .find(|d| d.code == DiagnosticCode::MissingRegInfo);
        assert!(reginfo_diag.is_some());
        assert_eq!(reginfo_diag.unwrap().severity, Severity::Advisory);
    }

    #[test]
    fn unaligned_entry_point_is_error() {
        let mut image = minimal_mips_elf();
        image.entry_point = 0x0010_0002;

        let report = analyze_compatibility(&image);

        assert!(report.has_errors());
        let entry_diag = report
            .diagnostics
            .iter()
            .find(|d| d.code == DiagnosticCode::EntryPointNotWordAligned);
        assert!(entry_diag.is_some());
        assert_eq!(entry_diag.unwrap().severity, Severity::Error);
    }

    #[test]
    fn entry_not_in_executable_segment_is_error() {
        let mut image = minimal_mips_elf();
        image.entry_point = 0x0020_0000; // Not in the PT_LOAD

        let report = analyze_compatibility(&image);

        assert!(report.has_errors());
        let entry_diag = report
            .diagnostics
            .iter()
            .find(|d| d.code == DiagnosticCode::EntryPointNotInExecutableSegment);
        assert!(entry_diag.is_some());
        assert_eq!(entry_diag.unwrap().severity, Severity::Error);
    }

    #[test]
    fn summary_contains_key_info() {
        let image = minimal_mips_elf();
        let report = analyze_compatibility(&image);
        let summary = report.summary();

        assert!(summary.contains("Format: valid"));
        assert!(summary.contains("Arch identity: recognized"));
        assert!(summary.contains("PS2 compatible: yes"));
        assert!(summary.contains("REPS2 support: yes"));
    }

    #[test]
    fn unknown_mips_section_type_is_unknown() {
        let mut image = minimal_mips_elf();
        image.section_headers.push(SectionHeader {
            name: ".MIPS.unknown".to_string(),
            name_offset: 20,
            kind: 0x7000_FFFF, // Not defined by the MIPS psABI
            flags: 0,
            virtual_address: 0,
            offset: 100,
            size: 16,
            link: 0,
            info: 0,
            alignment: 4,
            entry_size: 0,
        });

        let report = analyze_compatibility(&image);

        assert!(report.has_unresolved);
        let unknown_diag = report
            .diagnostics
            .iter()
            .find(|d| d.code == DiagnosticCode::UnknownMipsSectionType);
        assert!(unknown_diag.is_some());
        assert_eq!(unknown_diag.unwrap().severity, Severity::Unknown);
    }

    #[test]
    fn documented_mips_section_type_is_advisory() {
        let mut image = minimal_mips_elf();
        image.section_headers.push(SectionHeader {
            name: ".debug_info".to_string(),
            name_offset: 20,
            kind: 0x7000_001E, // SHT_MIPS_DWARF
            flags: 0,
            virtual_address: 0,
            offset: 100,
            size: 16,
            link: 0,
            info: 0,
            alignment: 4,
            entry_size: 0,
        });

        let report = analyze_compatibility(&image);

        let diag = report
            .diagnostics
            .iter()
            .find(|d| d.code == DiagnosticCode::MipsSectionNotInterpreted)
            .expect("documented MIPS section must be reported");
        assert_eq!(diag.severity, Severity::Advisory);
        assert!(diag.message.contains("SHT_MIPS_DWARF"));
        assert!(diag.blocks_stage.is_none());
        // A documented metadata section is not an unresolved "unknown" type:
        // the same section kind must not also appear as UnknownMipsSectionType.
        assert!(
            report
                .diagnostics
                .iter()
                .all(|d| !(d.code == DiagnosticCode::UnknownMipsSectionType
                    && d.raw_value == "0x7000001E"))
        );
    }

    #[test]
    fn high_relocation_type_is_unknown() {
        let mut image = minimal_mips_elf();
        image.relocations.push(crate::elf::Relocation {
            offset: 0x1000,
            symbol_index: 0,
            relocation_type: 100, // High type number
            addend: None,
            relocation_section_index: 0,
            target_section_index: 1,
            symbol_table_section_index: 0,
        });

        let report = analyze_compatibility(&image);

        assert!(report.has_unresolved);
        let reloc_diag = report.diagnostics.iter().find(|d| {
            d.code == DiagnosticCode::RelocationUnsupported && d.severity == Severity::Unknown
        });
        assert!(reloc_diag.is_some());
    }
}
