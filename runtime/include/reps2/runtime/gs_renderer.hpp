// SPDX-License-Identifier: GPL-3.0-or-later
//
// GS privileged-register MMIO map, GIF tag decoder, deterministic SPRITE
// rasterizer and PNG framebuffer dump. ADR 0225.
//
// Formats are derived from the public reference (ps2tek):
//   * GS privileged register addresses, GIF/Register_list.html
//   * GIFtag bit layout, GIF/GIFtags.html
//   * FRAME/ZBUF field layout, GS/Frame_and_ZBUFFER.html
//   * PRIM primitive-type values, GS/Primitives.html
// The title's observed values are used only as a consistency check.
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace reps2::runtime {

// ---------------------------------------------------------------------------
// GS privileged register addresses on the EE bus (0x12000000 page).
// ps2tek GS/Register_list.html + PS2SDK gs_privileged.h.
// ---------------------------------------------------------------------------
inline constexpr std::uint32_t kEeGsPrimAddress{0x1200'0000U};      // PRIM
inline constexpr std::uint32_t kEeGsRgbaqAddress{0x1200'0010U};     // RGBAQ
inline constexpr std::uint32_t kEeGsStAddress{0x1200'0020U};        // ST
inline constexpr std::uint32_t kEeGsUvAddress{0x1200'0030U};        // UV
inline constexpr std::uint32_t kEeGsXyzf2Address{0x1200'0040U};     // XYZF2
inline constexpr std::uint32_t kEeGsXyz2Address{0x1200'0050U};      // XYZ2
inline constexpr std::uint32_t kEeGsTex0_1Address{0x1200'0060U};    // TEX0_1
inline constexpr std::uint32_t kEeGsTex0_2Address{0x1200'0070U};    // TEX0_2
inline constexpr std::uint32_t kEeGsXyoffset1Address{0x1200'0180U}; // XYOFFSET_1
inline constexpr std::uint32_t kEeGsXyoffset2Address{0x1200'0190U}; // XYOFFSET_2
inline constexpr std::uint32_t kEeGsScissor1Address{0x1200'0400U};  // SCISSOR_1
inline constexpr std::uint32_t kEeGsScissor2Address{0x1200'0410U};  // SCISSOR_2
inline constexpr std::uint32_t kEeGsFrame1Address{0x1200'04C0U};    // FRAME_1
inline constexpr std::uint32_t kEeGsFrame2Address{0x1200'04D0U};    // FRAME_2
inline constexpr std::uint32_t kEeGsZbuf1Address{0x1200'04E0U};     // ZBUF_1
inline constexpr std::uint32_t kEeGsZbuf2Address{0x1200'04F0U};     // ZBUF_2

// ---------------------------------------------------------------------------
// GIFtag, 128 bits (ps2tek GIF/GIFtags.html).
//   bits  0..14  NLOOP  - data words per register (NLOOP==0 ends the packet)
//   bit  15      EOP    - end of packet
//   bit  46      PRIM enable
//   bits 47..57  PRIM data (written to PRIM when bit 46 is set)
//   bits 58..59  FLG: 0=PACKED, 1=REGLIST, 2=IMAGE, 3=IMAGE(disabled)
//   bits 60..63  NREGS - number of register descriptors (0 means 16)
//   bits 64..127 REGS  - NREGS 4-bit descriptors, first nibble processed first
// ---------------------------------------------------------------------------
inline constexpr std::uint32_t kGifTagNloopMask{0x0000'7FFFU};
inline constexpr std::uint32_t kGifTagEopMask{0x0000'8000U};
inline constexpr std::uint64_t kGifTagPrimEnableMask{0x0000'4000'0000'0000ULL}; // bit 46
inline constexpr std::uint32_t kGifTagPrimShift{47U};
inline constexpr std::uint32_t kGifTagPrimMask{0x0000'07FFU};
inline constexpr std::uint32_t kGifTagFlgShift{58U};
inline constexpr std::uint32_t kGifTagFlgMask{0x3U};
inline constexpr std::uint32_t kGifTagNregsShift{60U};
inline constexpr std::uint32_t kGifTagNregsMask{0xFU};

inline constexpr std::uint32_t kGifFlgPacked{0U};
inline constexpr std::uint32_t kGifFlgReglist{1U};
inline constexpr std::uint32_t kGifFlgImage{2U};

// GIF A+D register descriptor indices (ps2tek GIF/Register_list.html).
inline constexpr std::uint32_t kGifRegPrim{0U};
inline constexpr std::uint32_t kGifRegRgbaq{1U};
inline constexpr std::uint32_t kGifRegSt{2U};
inline constexpr std::uint32_t kGifRegUv{3U};
inline constexpr std::uint32_t kGifRegXyzf2{4U};
inline constexpr std::uint32_t kGifRegXyz2{5U};
inline constexpr std::uint32_t kGifRegTex0_1{6U};
inline constexpr std::uint32_t kGifRegTex0_2{7U};
inline constexpr std::uint32_t kGifRegXyzf3{11U};
inline constexpr std::uint32_t kGifRegXyz3{12U};
inline constexpr std::uint32_t kGifRegNop{15U};

// PRIM primitive-type field, bits 0..2 (ps2tek GS/Primitives.html).
inline constexpr std::uint32_t kGsPrimTypeMask{0x7U};
inline constexpr std::uint32_t kGsPrimTypeSprite{6U};

// FRAME_1/FRAME_2 field layout (ps2tek GS/Frame_and_ZBUFFER.html):
//   FBP   bits 0..8   base pointer, in 2048-word (8 KiB) units
//   FBW   bits 16..21 buffer width, in 64-pixel units
//   PSM   bits 24..29 pixel format
//   FBMSK bits 32..63 framebuffer mask
inline constexpr std::uint64_t kGsFrameFbpMask{0x0000'01FFULL};
inline constexpr std::uint64_t kGsFrameFbpShift{0U};
inline constexpr std::uint64_t kGsFrameFbwMask{0x003F'0000ULL};
inline constexpr std::uint64_t kGsFrameFbwShift{16U};
inline constexpr std::uint64_t kGsFramePsmMask{0x3F00'0000ULL};
inline constexpr std::uint64_t kGsFramePsmShift{24U};
inline constexpr std::uint32_t kGsPsmCt32{0U};   // 32-bit RGBA, the only format modelled

// XYOFFSET_1/2: OFX bits 0..15 (4-bit fixed point), OFY bits 32..47.
inline constexpr std::uint64_t kGsXyoffsetOfxMask{0x0000'FFFFULL};
inline constexpr std::uint64_t kGsXyoffsetOfyShift{32U};

// SCISSOR_1/2: SCAX0 bits 0..10, SCAX1 bits 16..26, SCAY0 bits 32..42,
// SCAY1 bits 48..58.
inline constexpr std::uint64_t kGsScissorX0Mask{0x7FFULL};
inline constexpr std::uint64_t kGsScissorX1Shift{16U};
inline constexpr std::uint64_t kGsScissorY0Shift{32U};
inline constexpr std::uint64_t kGsScissorY1Shift{48U};

// ---------------------------------------------------------------------------
// GIF packet decoder state. The decoder consumes one 128-bit qword at a time,
// so a real chain never needs a fixed-size data buffer (ADR 0224/0225).
// ---------------------------------------------------------------------------
struct GifDecoderState {
    bool in_payload{};
    std::uint32_t flg{kGifFlgPacked};
    std::uint32_t nreg{};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t loop_remaining{};
    std::uint32_t word_in_loop{};   // which register within the current loop
    bool ended{};
};

struct GsState {
    std::uint32_t prim{};
    std::uint32_t rgbaq{};
    std::uint32_t xyz2{};   // last X/Y packed
    std::uint32_t xyz3{};
    std::uint32_t uv{};
    std::uint32_t tex0{};
    std::uint64_t frame{};
    std::uint64_t zbuf{};
    std::uint64_t xyoffset{};
    std::uint64_t scissor{};
    // Diagnostics, never part of the pixel path.
    std::uint64_t tags_seen{};
    std::uint64_t ad_writes{};
    std::uint64_t sprites_drawn{};
    std::uint64_t unsupported_writes{};
};

// Extract the packed GIFtag fields from the tag's low 64-bit word. The REGS
// nibbles live in the high 64 bits.
inline void gif_decode_tag(
    std::uint64_t tag_low,
    std::uint64_t tag_high,
    GifDecoderState& decoder,
    std::uint32_t& prim_out,
    bool& prim_enable_out
) noexcept {
    const auto nloop = static_cast<std::uint32_t>(tag_low & kGifTagNloopMask);
    const bool eop = (tag_low & kGifTagEopMask) != 0U;
    prim_enable_out = (tag_low & kGifTagPrimEnableMask) != 0U;
    prim_out = static_cast<std::uint32_t>(
        (tag_low >> kGifTagPrimShift) & kGifTagPrimMask);
    decoder.flg = static_cast<std::uint32_t>(
        (tag_low >> kGifTagFlgShift) & kGifTagFlgMask);
    std::uint32_t nreg = static_cast<std::uint32_t>(
        (tag_low >> kGifTagNregsShift) & kGifTagNregsMask);
    if (nreg == 0U) nreg = 16U; // ps2tek: 0 means 16 registers
    decoder.nreg = nreg;
    for (std::uint32_t index = 0U; index < 16U; ++index) {
        decoder.regs[index] = static_cast<std::uint32_t>(
            (tag_high >> (index * 4U)) & 0xFU);
    }
    // ps2tek: "Registers are handled in little-endian order; i.e. bits 64-67
    // are processed first". NLOOP counts data words; NLOOP==0 ends the packet.
    decoder.loop_remaining = nloop;
    decoder.word_in_loop = 0U;
    decoder.in_payload = nloop != 0U;
    decoder.ended = eop || nloop == 0U;
}

// ---------------------------------------------------------------------------
// GS local memory helpers. GS local memory is 4 MiB, addressed in 32-bit words
// but stored byte-wise here.
// ---------------------------------------------------------------------------
inline constexpr std::uint32_t kGsLocalMemoryWords{1024U * 1024U}; // 4 MiB / 4
inline constexpr std::uint32_t kGsPageWords{2048U};                // FRAME FBP unit

// FRAME field extraction.
struct GsFrameDesc {
    std::uint32_t fbp{};
    std::uint32_t fbw{};
    std::uint32_t psm{};
};

inline GsFrameDesc gs_decode_frame(std::uint64_t frame) noexcept {
    return GsFrameDesc{
        static_cast<std::uint32_t>((frame & kGsFrameFbpMask) >> kGsFrameFbpShift),
        static_cast<std::uint32_t>((frame & kGsFrameFbwMask) >> kGsFrameFbwShift),
        static_cast<std::uint32_t>((frame & kGsFramePsmMask) >> kGsFramePsmShift),
    };
}

// Deterministic SPRITE fill into GS local memory at the FRAME base pointer.
// The sprite occupies a fixed rectangle; the colour comes from RGBAQ. Only
// PSMCT32 (0) is modelled; any other PSM is refused (fail closed).
// Returns false when the rectangle leaves GS local memory or the PSM is
// unsupported, leaving memory untouched.
[[nodiscard]] inline bool gs_draw_sprite_rgba32(
    std::span<std::uint8_t> local_memory,
    const GsFrameDesc& frame,
    std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height,
    std::uint32_t rgba
) noexcept {
    if (frame.psm != kGsPsmCt32) return false;
    if (frame.fbw == 0U) return false;
    const std::uint32_t fb_width = frame.fbw * 64U;               // pixels
    const std::uint64_t base_bytes =
        static_cast<std::uint64_t>(frame.fbp) * kGsPageWords * 4U; // 8 KiB units
    if (base_bytes >= local_memory.size()) return false;
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * 4U;
    for (std::uint32_t row = 0U; row < height; ++row) {
        if (y + row >= fb_width) return false; // sprite taller than the buffer
        const std::uint64_t offset =
            base_bytes + (static_cast<std::uint64_t>(y + row) * fb_width + x) * 4U;
        if (offset + row_bytes > local_memory.size()) return false;
        for (std::uint32_t column = 0U; column < width; ++column) {
            const std::uint64_t pixel = offset + column * 4U;
            local_memory[pixel + 0U] = static_cast<std::uint8_t>(rgba & 0xFFU);
            local_memory[pixel + 1U] = static_cast<std::uint8_t>((rgba >> 8U) & 0xFFU);
            local_memory[pixel + 2U] = static_cast<std::uint8_t>((rgba >> 16U) & 0xFFU);
            local_memory[pixel + 3U] = static_cast<std::uint8_t>((rgba >> 24U) & 0xFFU);
        }
    }
    return true;
}

// FNV-1a over the FRAME region: the deterministic framebuffer digest (BS-3).
[[nodiscard]] inline std::uint64_t gs_framebuffer_digest(
    std::span<const std::uint8_t> local_memory,
    const GsFrameDesc& frame,
    std::uint32_t width, std::uint32_t height
) noexcept {
    std::uint64_t hash = 0xCBF2'9CE4'8422'2325ULL;
    const std::uint64_t base_bytes =
        static_cast<std::uint64_t>(frame.fbp) * kGsPageWords * 4U;
    const std::uint64_t fb_width = static_cast<std::uint64_t>(frame.fbw) * 64U;
    for (std::uint32_t row = 0U; row < height; ++row) {
        const std::uint64_t offset =
            base_bytes + (static_cast<std::uint64_t>(row) * fb_width) * 4U;
        for (std::uint64_t index = 0U; index < static_cast<std::uint64_t>(width) * 4U; ++index) {
            if (offset + index >= local_memory.size()) return hash;
            hash ^= local_memory[static_cast<std::size_t>(offset + index)];
            hash *= 0x0000'0100'0000'01B3ULL;
        }
    }
    return hash;
}

// Minimal PNG writer for an RGBA8 buffer (no external library). Deterministic
// byte output for identical pixels, so a PNG digest is reproducible.
// `pixels` is width*height RGBA bytes in row-major order.
[[nodiscard]] inline bool png_write_rgba8(
    std::FILE* file, std::uint32_t width, std::uint32_t height,
    std::span<const std::uint8_t> pixels
) noexcept {
    if (file == nullptr || width == 0U || height == 0U) return false;
    if (pixels.size() < static_cast<std::size_t>(width) * height * 4U) return false;

    std::uint32_t crc_table[256];
    for (std::uint32_t n = 0U; n < 256U; ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1U) != 0U ? (0xEDB8'8320U ^ (c >> 1U)) : (c >> 1U);
        }
        crc_table[n] = c;
    }
    const auto crc = [&crc_table](const std::uint8_t* data, std::size_t length) {
        std::uint32_t c = 0xFFFF'FFFFU;
        for (std::size_t index = 0U; index < length; ++index) {
            c = crc_table[(c ^ data[index]) & 0xFFU] ^ (c >> 8U);
        }
        return c ^ 0xFFFF'FFFFU;
    };
    const auto be32 = [](std::uint8_t* out, std::uint32_t value) {
        out[0] = static_cast<std::uint8_t>(value >> 24U);
        out[1] = static_cast<std::uint8_t>(value >> 16U);
        out[2] = static_cast<std::uint8_t>(value >> 8U);
        out[3] = static_cast<std::uint8_t>(value);
    };

    static constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (std::fwrite(kSignature, 1U, 8U, file) != 8U) return false;

    // IHDR
    std::uint8_t ihdr[25];
    be32(ihdr + 0U, 13U);
    std::memcpy(ihdr + 4U, "IHDR", 4U);
    be32(ihdr + 8U, width);
    be32(ihdr + 12U, height);
    ihdr[16] = 8U;  // bit depth
    ihdr[17] = 6U;  // colour type RGBA
    ihdr[18] = 0U;  // compression
    ihdr[19] = 0U;  // filter
    ihdr[20] = 0U;  // interlace
    be32(ihdr + 21U, crc(ihdr + 4U, 17U));
    if (std::fwrite(ihdr, 1U, 25U, file) != 25U) return false;

    // IDAT: one stored block holding filter byte 0 + all RGBA rows.
    const std::uint64_t raw_size =
        static_cast<std::uint64_t>(height) * (1U + width * 4U);
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(raw_size) + 8U);
    raw.push_back(0x78U); // zlib CMF (deflate)
    raw.push_back(0x01U); // zlib FLG
    const std::uint16_t len = static_cast<std::uint16_t>(raw_size);
    raw.push_back(0x01U); // BFINAL=1, BTYPE=00 stored
    raw.push_back(static_cast<std::uint8_t>(len & 0xFFU));
    raw.push_back(static_cast<std::uint8_t>(len >> 8U));
    raw.push_back(static_cast<std::uint8_t>((~len) & 0xFFU));
    raw.push_back(static_cast<std::uint8_t>((~len >> 8U) & 0xFFU));
    raw.push_back(0U); // filter type None for row 0
    const std::uint8_t* row_pixels = pixels.data();
    raw.insert(raw.end(), row_pixels, row_pixels + width * 4U);
    for (std::uint32_t row = 1U; row < height; ++row) {
        raw.push_back(0U); // filter type None
        row_pixels += width * 4U;
        raw.insert(raw.end(), row_pixels, row_pixels + width * 4U);
    }
    // Adler-32 over the uncompressed stream (filter bytes included), starting
    // after the zlib header (CMF+FLG+5-byte stored-block header = 7 bytes).
    std::uint32_t a = 1U, b = 0U;
    for (std::size_t index = 7U; index < raw.size(); ++index) {
        a = (a + raw[index]) % 65521U;
        b = (b + a) % 65521U;
    }
    raw.push_back(static_cast<std::uint8_t>(b >> 8U));
    raw.push_back(static_cast<std::uint8_t>(b));
    raw.push_back(static_cast<std::uint8_t>(a >> 8U));
    raw.push_back(static_cast<std::uint8_t>(a));

    std::uint8_t idat_header[8];
    be32(idat_header + 0U, static_cast<std::uint32_t>(raw.size()));
    std::memcpy(idat_header + 4U, "IDAT", 4U);
    if (std::fwrite(idat_header, 1U, 8U, file) != 8U) return false;
    if (std::fwrite(raw.data(), 1U, raw.size(), file) != raw.size()) return false;
    std::uint8_t idat_crc[4];
    be32(idat_crc, crc(raw.data(), raw.size()));
    if (std::fwrite(idat_crc, 1U, 4U, file) != 4U) return false;

    static constexpr std::uint8_t kIend[12] = {
        0x00, 0x00, 0x00, 0x00, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
    return std::fwrite(kIend, 1U, 12U, file) == 12U;
}

} // namespace reps2::runtime