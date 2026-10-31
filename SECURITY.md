# Security policy

## Supported versions

REPS2 is experimental. Security fixes are applied to the current main branch; no stable release support window is currently promised.

## Reporting a vulnerability

Do not publish malformed-file exploit details before maintainers have had an opportunity to investigate. Use GitHub's private vulnerability reporting feature when enabled, or contact the repository owner through the contact method listed on the GitHub profile.

## Parser and runtime threat model

ELF, disc metadata, instruction streams, save data, and future runtime inputs are untrusted. Changes must consider malformed lengths, integer overflow, out-of-range offsets, oversized allocations, invalid control flow, and host undefined behavior. Parsers should reject invalid structures with bounded diagnostics rather than crash or continue with partial state.

Do not include real game files or BIOS data in bug reports. Provide a minimal synthetic reproducer instead.
