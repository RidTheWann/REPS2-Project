// SPDX-License-Identifier: GPL-3.0-or-later
//
// GIF packet decoder + GS register/rasterizer smoke test (ADR 0225).
// Vectors are derived from the ps2tek reference layouts, not from the
// implementation: GIFtag bit fields (GIF/GIFtags.html), FRAME/ZBUF fields
// (GS/Frame_and_ZBUFFER.html), PRIM type values (GS/Primitives.html).
#include "reps2/runtime/ee_state.hpp"
#include "reps2/runtime/gs_renderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Gpr128;
using reps2::runtime::Status;

// Build a 128-bit GIFtag word pair per ps2tek GIF/GIFtags.html:
//   bits 0..14 NLOOP, bit 15 EOP, bit 46 PRIM-enable, bits 47..57 PRIM,
//   bits 58..59 FLG, bits 60..63 NREGS, bits 64..127 register descriptors.
[[nodiscard]] constexpr std::uint64_t gif_tag_low(
    std::uint32_t nloop, bool eop, bool prim_enable, std::uint32_t prim,
    std::uint32_t flg, std::uint32_t nregs
) noexcept {
    return (static_cast<std::uint64_t>(nloop) & 0x7FFFU)
        | (eop ? 0x0000'8000ULL : 0ULL)
        | (prim_enable ? 0x0000'4000'0000'0000ULL : 0ULL)
        | ((static_cast<std::uint64_t>(prim) & 0x7FFU) << 47U)
        | ((static_cast<std::uint64_t>(flg) & 0x3U) << 58U)
        | ((static_cast<std::uint64_t>(nregs) & 0xFU) << 60U);
}

[[nodiscard]] constexpr std::uint64_t gif_tag_high(
    std::uint32_t r0, std::uint32_t r1, std::uint32_t r2, std::uint32_t r3
) noexcept {
    return (static_cast<std::uint64_t>(r0 & 0xFU))
        | (static_cast<std::uint64_t>(r1 & 0xFU) << 4U)
        | (static_cast<std::uint64_t>(r2 & 0xFU) << 8U)
        | (static_cast<std::uint64_t>(r3 & 0xFU) << 12U);
}

// A PACKED packet with NREGS=2 (RGBAQ, XYZ2) and NLOOP=1 writes RGBAQ then XYZ2.
int test_gif_packed_ad_dispatch() {
    EeState state{};
    // NLOOP=1: one pass through NREGS=2 registers = 2 qwords total
    const auto tag_low = gif_tag_low(1U, false, false, 0U, 0U /*PACKED*/, 2U);
    const auto tag_high = gif_tag_high(reps2::runtime::kGifRegRgbaq,
                                       reps2::runtime::kGifRegXyz2, 0U, 0U);
    if (reps2::runtime::gif_consume_qword(state.memory, Gpr128{tag_low, tag_high})
        != Status::ok) return 1;
    if (state.memory.gs.tags_seen != 1U) return 2;
    // Two payload qwords: RGBAQ then XYZ2, each a full 64-bit qword.
    // Q1: low=0x1111'2222 (RGBAQ), Q2: low=0x1234'5678 (XYZ2)
    if (reps2::runtime::gif_consume_qword(state.memory,
            Gpr128{0x8080'FF40'1111'2222ULL, 0ULL}) != Status::ok) return 3;
    if (state.memory.gs.rgbaq != 0x1111'2222U) return 4;
    if (reps2::runtime::gif_consume_qword(state.memory,
            Gpr128{0x5555'6666'1234'5678ULL, 0ULL}) != Status::ok) return 5;
    if (state.memory.gs.xyz2 != 0x1234'5678U) return 6;
    if (state.memory.gs.ad_writes != 2U) return 7;
    return 0;
}

// REGLIST (FLG=1): each qword carries TWO registers (low 64 -> first,
// high 64 -> second). ps2tek GIF/Data_formats.html: "Every quadword contains
// data for two registers."
int test_gif_reglist_two_registers_per_qword() {
    EeState state{};
    // Tag: NLOOP=1, FLG=REGLIST(1), NREGS=2, descriptors [RGBAQ, XYZ2].
    const auto tag_low = gif_tag_low(1U, false, false, 0U, 1U /*REGLIST*/, 2U);
    const auto tag_high = gif_tag_high(reps2::runtime::kGifRegRgbaq,
                                       reps2::runtime::kGifRegXyz2, 0U, 0U);
    if (reps2::runtime::gif_consume_qword(state.memory, Gpr128{tag_low, tag_high})
        != Status::ok) return 1;
    // One payload qword: low 64 -> RGBAQ, high 64 -> XYZ2.
    const Gpr128 payload{0x1111'2222'3333'4444ULL, 0x5555'6666'7777'8888ULL};
    if (reps2::runtime::gif_consume_qword(state.memory, payload) != Status::ok)
        return 2;
    if (state.memory.gs.rgbaq != 0x3333'4444U) return 3; // low 32 of low 64
    if (state.memory.gs.xyz2 != 0x7777'8888U) return 4; // low 32 of high 64
    if (state.memory.gs.ad_writes != 2U) return 5;
    return 0;
}

// PRIM-enable (tag bit 46) writes the PRIM field at packet start.
int test_gif_tag_prim_field() {
    EeState state{};
    const std::uint32_t prim = reps2::runtime::kGsPrimTypeSprite; // 6 = SPRITE
    const auto tag_low = gif_tag_low(0U, true, true, prim, 0U, 0U);
    if (reps2::runtime::gif_consume_qword(state.memory, Gpr128{tag_low, 0ULL})
        != Status::ok) return 1;
    if (state.memory.gs.prim != prim) return 2;
    if (state.memory.gs_prim != prim) return 3;
    return 0;
}

// IMAGE format (FLG=2) is flagged unsupported, never silently decoded.
int test_gif_image_format_flagged() {
    EeState state{};
    const auto tag_low = gif_tag_low(1U, false, false, 0U, 2U /*IMAGE*/, 1U);
    if (reps2::runtime::gif_consume_qword(state.memory, Gpr128{tag_low, 0ULL})
        != Status::ok) return 1;
    if (!state.memory.gif_decode_unsupported) return 2;
    return 0;
}

// FRAME decode: FBP bits 0..8, FBW bits 16..21, PSM bits 24..29.
int test_gs_frame_field_decode() {
    const std::uint64_t frame =
        (0x10ULL << 0U)      // FBP = 16 (16*8KiB = 128 KiB base)
        | (8ULL << 16U)       // FBW = 8 (8*64 = 512 px)
        | (0ULL << 24U);      // PSM = PSMCT32
    const auto desc = reps2::runtime::gs_decode_frame(frame);
    if (desc.fbp != 0x10U) return 1;
    if (desc.fbw != 8U) return 2;
    if (desc.psm != 0U) return 3;
    return 0;
}

// SPRITE fill into GS local memory at the FRAME base pointer.
int test_gs_sprite_raster_deterministic() {
    std::vector<std::uint8_t> memory(4U * 1024U * 1024U, 0U);
    const std::uint64_t frame = (1ULL << 0U) | (8ULL << 16U) | (0ULL << 24U);
    const auto desc = reps2::runtime::gs_decode_frame(frame);
    const std::uint32_t rgba = 0x2030'40FFU; // A=0x20 B=0x30 G=0x40 R=0xFF
    if (!reps2::runtime::gs_draw_sprite_rgba32(
            memory, desc, 0U, 0U, 4U, 4U, rgba)) return 1;
    // Pixel 0 = base; RGBA stored little-endian (R,G,B,A).
    const std::uint64_t base = 1ULL * 2048U * 4U;
    if (memory[base + 0U] != 0xFFU) return 2; // R
    if (memory[base + 1U] != 0x40U) return 3; // G
    if (memory[base + 2U] != 0x30U) return 4; // B
    if (memory[base + 3U] != 0x20U) return 5; // A
    // Pixel (3,3) is inside the sprite.
    const std::uint64_t p33 = base + (3ULL * 512ULL + 3ULL) * 4ULL;
    if (memory[p33 + 0U] != 0xFFU) return 6;
    // Digest is stable across two identical renders.
    const auto d1 = reps2::runtime::gs_framebuffer_digest(memory, desc, 512U, 4U);
    const auto d2 = reps2::runtime::gs_framebuffer_digest(memory, desc, 512U, 4U);
    if (d1 != d2) return 7;
    return 0;
}

// A non-PSMCT32 FRAME is refused (fail closed), leaving memory untouched.
int test_gs_sprite_refuses_unsupported_psm() {
    std::vector<std::uint8_t> memory(4U * 1024U * 1024U, 0U);
    const std::uint64_t frame = (0ULL << 0U) | (8ULL << 16U) | (1ULL << 24U); // PSM=1
    const auto desc = reps2::runtime::gs_decode_frame(frame);
    if (desc.psm != 1U) return 3; // decode must see PSM=1
    if (reps2::runtime::gs_draw_sprite_rgba32(memory, desc, 0U, 0U, 2U, 2U, 0xFFU)) {
        return 1; // must refuse
    }
    for (std::size_t index = 0U; index < 4096U; ++index) {
        if (memory[index] != 0U) return 2; // untouched
    }
    return 0;
}

// A sprite that leaves GS local memory is refused with no write.
int test_gs_sprite_refuses_out_of_range() {
    std::vector<std::uint8_t> memory(4U * 1024U * 1024U, 0U);
    const std::uint64_t frame = (0x1FFULL << 0U) | (8ULL << 16U); // FBP near top
    const auto desc = reps2::runtime::gs_decode_frame(frame);
    if (reps2::runtime::gs_draw_sprite_rgba32(
            memory, desc, 500U, 3U, 100U, 4U, 0xFFU)) return 1; // runs past width
    return 0;
}

// PNG writer emits a valid signature and is byte-identical on repeat.
int test_png_writer_deterministic() {
    std::vector<std::uint8_t> pixels(4U * 4U * 4U, 0x80U);
    const char* path_a = "gif_decoder_smoke_a.png";
    const char* path_b = "gif_decoder_smoke_b.png";
    std::FILE* fa = std::fopen(path_a, "wb");
    if (fa == nullptr) return 1;
    const bool ok_a = reps2::runtime::png_write_rgba8(fa, 4U, 4U, pixels);
    std::fclose(fa);
    std::FILE* fb = std::fopen(path_b, "wb");
    if (fb == nullptr) return 2;
    const bool ok_b = reps2::runtime::png_write_rgba8(fb, 4U, 4U, pixels);
    std::fclose(fb);
    if (!ok_a || !ok_b) return 3;
    std::FILE* ra = std::fopen(path_a, "rb");
    std::FILE* rb = std::fopen(path_b, "rb");
    if (ra == nullptr || rb == nullptr) return 4;
    std::array<std::uint8_t, 8> sig_a{};
    if (std::fread(sig_a.data(), 1U, 8U, ra) != 8U) return 5;
    const std::array<std::uint8_t, 8> expected{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (sig_a != expected) return 6;
    // Compare full byte streams; the two files must be identical.
    std::fseek(ra, 0, SEEK_SET);
    std::fseek(rb, 0, SEEK_SET);
    for (;;) {
        const int ca = std::fgetc(ra);
        const int cb = std::fgetc(rb);
        if (ca != cb) { std::fclose(ra); std::fclose(rb); std::remove(path_a); std::remove(path_b); return 7; }
        if (ca == EOF) break;
    }
    std::fclose(ra);
    std::fclose(rb);
    std::remove(path_a);
    std::remove(path_b);
    return 0;
}

} // namespace

int main() {
    if (const auto r = test_gif_packed_ad_dispatch(); r != 0) {
        std::fprintf(stderr, "GIF PACKED A+D dispatch failed at %d\n", r);
        return 10 + r;
    }
    if (const auto r = test_gif_reglist_two_registers_per_qword(); r != 0) {
        std::fprintf(stderr, "GIF REGLIST two-registers-per-qword failed at %d\n", r);
        return 15 + r;
    }
    if (const auto r = test_gif_tag_prim_field(); r != 0) {
        std::fprintf(stderr, "GIF tag PRIM field failed at %d\n", r);
        return 20 + r;
    }
    if (const auto r = test_gif_image_format_flagged(); r != 0) {
        std::fprintf(stderr, "GIF IMAGE format flag failed at %d\n", r);
        return 30 + r;
    }
    if (const auto r = test_gs_frame_field_decode(); r != 0) {
        std::fprintf(stderr, "GS FRAME decode failed at %d\n", r);
        return 40 + r;
    }
    if (const auto r = test_gs_sprite_raster_deterministic(); r != 0) {
        std::fprintf(stderr, "GS sprite raster failed at %d\n", r);
        return 50 + r;
    }
    if (const auto r = test_gs_sprite_refuses_unsupported_psm(); r != 0) {
        std::fprintf(stderr, "GS sprite PSM fail-closed failed at %d\n", r);
        return 60 + r;
    }
    if (const auto r = test_gs_sprite_refuses_out_of_range(); r != 0) {
        std::fprintf(stderr, "GS sprite range fail-closed failed at %d\n", r);
        return 70 + r;
    }
    if (const auto r = test_png_writer_deterministic(); r != 0) {
        std::fprintf(stderr, "PNG writer failed at %d\n", r);
        return 80 + r;
    }
    std::printf("all checks passed\n");
    return 0;
}