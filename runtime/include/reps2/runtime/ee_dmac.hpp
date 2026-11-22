// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace reps2::runtime {

// EE DMAC register addresses. Names follow PS2SDK `ee/dma/include/
// dma_registers.h`; PCSX2's `Hw.h` uses the same numbering.
inline constexpr std::uint32_t kEeDmacCtrlAddress{0x1000'E000U};   // D_CTRL
inline constexpr std::uint32_t kEeDmacDStatAddress{0x1000'E010U};  // D_STAT
inline constexpr std::uint32_t kEeDmacPcrAddress{0x1000'E020U};    // D_PCR

// SIF0 (channel 5) registers.
inline constexpr std::uint32_t kEeDmacChcr5Address{0x1000'C000U};
inline constexpr std::uint32_t kEeDmacMadr5Address{0x1000'C010U};
inline constexpr std::uint32_t kEeDmacQwc5Address{0x1000'C020U};
inline constexpr std::uint32_t kEeDmacTadr5Address{0x1000'C030U};

// VIF0 (channel 1) registers. ADR 0184.
inline constexpr std::uint32_t kEeDmacChcr1Address{0x1000'9000U};
inline constexpr std::uint32_t kEeDmacMadr1Address{0x1000'9010U};
inline constexpr std::uint32_t kEeDmacQwc1Address{0x1000'9020U};
inline constexpr std::uint32_t kEeDmacTadr1Address{0x1000'9030U};

// GIF (channel 2) registers. ADR 0185.
inline constexpr std::uint32_t kEeDmacChcr2Address{0x1000'A000U};
inline constexpr std::uint32_t kEeDmacMadr2Address{0x1000'A010U};
inline constexpr std::uint32_t kEeDmacQwc2Address{0x1000'A020U};
inline constexpr std::uint32_t kEeDmacTadr2Address{0x1000'A030U};

// D_CTRL bit masks.
inline constexpr std::uint32_t kEeDmacCtrlDmaeMask{0x0000'0001U};  // bit 0: master enable

// D_STAT bit masks.
inline constexpr std::uint32_t kEeDmacDStatChannelStatusMask{0x0000'03FFU};
inline constexpr std::uint32_t kEeDmacDStatUnmodelledStatusWriteMask{0x0000'E000U};
inline constexpr std::uint32_t kEeDmacDStatInterruptMaskMask{0x63FF'0000U};
inline constexpr std::uint32_t kEeDmacDStatReadableMask{0x63FF'E3FFU};
inline constexpr std::uint32_t kEeDmacDStatSif0StatusBit{0x0000'0020U};
inline constexpr std::uint32_t kEeDmacDStatBusErrorStatusBit{0x0000'8000U};

// The documented DMAC interrupt condition, independent of device state.
//
// PCSX2 `tDMAC_STAT::TestForInterrupt()` is
// `((_u16[0] & _u16[1]) != 0) || BEIS`: a status bit asserts only when its
// matching mask bit in the upper half is set, and the bus-error status bit
// notifies unconditionally. PS2SDK's
// `DMA_SET_STAT(CIS,SIS,MEIS,BEIS,CIM,SIM,MEIM)` places the same masks at the
// same bit positions, so the half-word AND is the documented comparison.
// PCSX2 `dmacInterrupt()` reports nothing while `D_CTRL.DMAE` is clear or while
// the hold register suspends the controller, and delivers the result on the EE's
// own DMAC interrupt line (COP0 `Cause` bit 11, IP3) — not the INTC's IP2.
//
// The caller supplies the inputs, so no reset value is assumed. This answers
// "would the line assert for these register values"; it delivers no exception.
// Defined before the device struct so EeDmacState::interrupt_pending() can call
// it. See ADR 0105, ADR 0153.
[[nodiscard]] constexpr bool ee_dmac_interrupt_pending(
    std::uint32_t d_stat,
    bool dmae,
    bool controller_held
) noexcept {
    if (!dmae || controller_held) {
        return false;
    }
    const std::uint32_t status = d_stat & 0x0000'FFFFU;
    const std::uint32_t mask = (d_stat >> 16U) & 0x0000'FFFFU;
    return (status & mask) != 0U
        || (d_stat & kEeDmacDStatBusErrorStatusBit) != 0U;
}

// CHCR bit masks (per PS2Tek / PS2SDK).
inline constexpr std::uint32_t kEeDmacChcrStrMask{0x0000'0100U};     // bit 8: start/trigger
inline constexpr std::uint32_t kEeDmacChcrDirMask{0x0000'0001U};     // bit 0: direction

struct EeDmacState {
    // D_CTRL: DMAC control register. Only the master-enable bit is named; the
    // other fields (cycle stealing, MFIFO drain, stall control) are not
    // interpreted by this bounded model.
    std::uint32_t ctrl{};
    bool ctrl_known{};

    // D_STAT: DMAC status register.
    std::uint32_t d_stat{};
    bool d_stat_known{};

    // D_PCR: DMAC priority-control register (CPC 0..9, CDE 16..25, PCE 31).
    // It holds channel priority, not interrupt state; the DMAC interrupt is
    // gated by D_STAT's CIM half and D_CTRL.DMAE and is delivered on the EE's
    // own COP0 line, not through the INTC. See ADR 0105.
    std::uint32_t pcr{};
    bool pcr_known{};

    // SIF0 (channel 5) registers.
    std::uint32_t chcr5{};
    bool chcr5_known{};
    std::uint32_t madr5{};
    bool madr5_known{};
    std::uint32_t qwc5{};
    bool qwc5_known{};
    std::uint32_t tadr5{};
    bool tadr5_known{};

    // VIF0 (channel 1) registers: bounded storage only. ADR 0184.
    std::uint32_t chcr1{};
    bool chcr1_known{};
    std::uint32_t madr1{};
    bool madr1_known{};
    std::uint32_t qwc1{};
    bool qwc1_known{};
    std::uint32_t tadr1{};
    bool tadr1_known{};

    // GIF (channel 2) registers: bounded storage only, reset known zero. ADR 0185.
    std::uint32_t chcr2{};
    std::uint32_t madr2{};
    std::uint32_t qwc2{};
    std::uint32_t tadr2{};

    // Establish the HLE boot environment's DMAC state: every modelled register
    // is known and idle. This models the kernel-initialized DMAC the title
    // inherits, not a hardware reset claim. Evidence: PS2SDK's own `dma_reset`
    // (R5900 code in libdma.erl) clears each channel and the status register, so
    // the SDK-visible idle state is "all channels stopped, no pending status,
    // no interrupt masks". PCSX2's `dmacReset` corroborates stat=0, chcr=0 as a
    // non-normative behavioural cross-check. Production reads still fail closed
    // when this initialization was not applied. See ADR 0086.
    void seed_idle_boot_state() noexcept {
        ctrl = 0; ctrl_known = true;
        d_stat = 0; d_stat_known = true;
        pcr = 0; pcr_known = true;
        chcr5 = 0; chcr5_known = true;
        madr5 = 0; madr5_known = true;
        qwc5 = 0; qwc5_known = true;
        tadr5 = 0; tadr5_known = true;
    }

    // Seed D_CTRL from device/test input. No hardware reset default.
    void seed_ctrl(std::uint32_t value) noexcept {
        ctrl = value;
        ctrl_known = true;
    }

    // Seed D_PCR from device/test input.
    void seed_pcr(std::uint32_t value) noexcept {
        pcr = value;
        pcr_known = true;
    }

    // Seed D_STAT from device/test input.
    void seed_d_stat(std::uint32_t value) noexcept {
        d_stat = value & kEeDmacDStatReadableMask;
        d_stat_known = true;
    }

    // Seed SIF0 CHCR.
    void seed_chcr5(std::uint32_t value) noexcept {
        chcr5 = value;
        chcr5_known = true;
    }

    // Seed SIF0 MADR.
    void seed_madr5(std::uint32_t value) noexcept {
        madr5 = value;
        madr5_known = true;
    }

    // Seed SIF0 QWC.
    void seed_qwc5(std::uint32_t value) noexcept {
        qwc5 = value;
        qwc5_known = true;
    }

    // Seed SIF0 TADR.
    void seed_tadr5(std::uint32_t value) noexcept {
        tadr5 = value;
        tadr5_known = true;
    }

    // Read D_CTRL. Fails if not seeded.
    [[nodiscard]] bool read_ctrl(std::uint32_t& value) const noexcept {
        if (!ctrl_known) return false;
        value = ctrl;
        return true;
    }

    // Write D_CTRL. Fails if not seeded.
    [[nodiscard]] bool write_ctrl(std::uint32_t value) noexcept {
        if (!ctrl_known) return false;
        ctrl = value;
        return true;
    }

    // Read D_PCR. Fails if not seeded.
    [[nodiscard]] bool read_pcr(std::uint32_t& value) const noexcept {
        if (!pcr_known) return false;
        value = pcr;
        return true;
    }

    // Write D_PCR. Fails if not seeded.
    [[nodiscard]] bool write_pcr(std::uint32_t value) noexcept {
        if (!pcr_known) return false;
        pcr = value;
        return true;
    }

    // Read D_STAT. Fails if not seeded.
    [[nodiscard]] bool read_d_stat(std::uint32_t& value) const noexcept {
        if (!d_stat_known) return false;
        value = d_stat & kEeDmacDStatReadableMask;
        return true;
    }

    // Write D_STAT. Fails if not seeded or writing unmodelled status bits.
    [[nodiscard]] bool write_d_stat(std::uint32_t value) noexcept {
        if (!d_stat_known
            || (value & kEeDmacDStatUnmodelledStatusWriteMask) != 0U) {
            return false;
        }

        // Acknowledge only the documented channel status fields (W1C).
        // Writes of one to interrupt-mask fields reverse those bits.
        d_stat &= ~(value & kEeDmacDStatChannelStatusMask);
        d_stat ^= value & kEeDmacDStatInterruptMaskMask;
        d_stat &= kEeDmacDStatReadableMask;
        return true;
    }

    // Would this device assert the EE's own DMAC interrupt line (COP0 Cause IP3)
    // right now? The answer is the documented condition (ADR 0105) applied to the
    // modelled registers: a channel status bit whose matching mask bit is set, or
    // the bus-error status bit, and only while D_CTRL.DMAE is set.
    //
    // The EE DMAC's D_CTRL has a single documented bit, DMAE (PS2SDK
    // `dma_registers.h`, PCSX2 `HwWrite.cpp`); the EE register map has no hold
    // register, so the condition's `controller_held` input is always false for
    // this device and is retained only as the ADR 0105 fail-closed seam.
    //
    // Fail-closed: false unless both D_CTRL and D_STAT were seeded, so an
    // unseeded device never reports an interrupt. See ADR 0153.
    [[nodiscard]] bool interrupt_pending() const noexcept {
        if (!ctrl_known || !d_stat_known) return false;
        return ee_dmac_interrupt_pending(
            d_stat, (ctrl & kEeDmacCtrlDmaeMask) != 0U, false);
    }

    // Publish the SIF0 (channel 5) completion event for an inbound IOP->EE
    // transfer. The EE SIF interface raises the channel-5 status bit when a
    // receive-direction transfer finishes; this is the same D_STAT bit an
    // outbound CHCR start publishes. Fails when D_STAT state is unknown so a
    // completion is never fabricated. See ADR 0100.
    [[nodiscard]] bool publish_sif0_event() noexcept {
        if (!d_stat_known) return false;
        d_stat |= kEeDmacDStatSif0StatusBit;
        d_stat &= kEeDmacDStatReadableMask;
        return true;
    }

    // Read SIF0 CHCR. Fails if not seeded.
    [[nodiscard]] bool read_chcr5(std::uint32_t& value) const noexcept {
        if (!chcr5_known) return false;
        value = chcr5;
        return true;
    }

    // Write SIF0 CHCR. Fails if not seeded.
    //
    // Starting the channel (STR 0->1) is modelled as an immediately completed
    // SIF0 transfer: the completion event is published to D_STAT bit 5 and the
    // STR bit self-clears, matching the "channel finished" observation the SDK
    // polls for. Other CHCR bits are preserved. This is a deliberate
    // simplification with no IOP backpressure or transfer timing; it never
    // fabricates an event for a channel that was already running, and it does
    // nothing if D_STAT itself is unknown. See ADR 0086.
    [[nodiscard]] bool write_chcr5(std::uint32_t value) noexcept {
        if (!chcr5_known) return false;

        const bool was_str = (chcr5 & kEeDmacChcrStrMask) != 0U;
        const bool now_str = (value & kEeDmacChcrStrMask) != 0U;

        chcr5 = value;

        if (!was_str && now_str && d_stat_known) {
            d_stat |= kEeDmacDStatSif0StatusBit;
            d_stat &= kEeDmacDStatReadableMask;
            chcr5 &= ~kEeDmacChcrStrMask;
        }
        return true;
    }

    // Read SIF0 MADR. Fails if not seeded.
    [[nodiscard]] bool read_madr5(std::uint32_t& value) const noexcept {
        if (!madr5_known) return false;
        value = madr5;
        return true;
    }

    // Write SIF0 MADR. Fails if not seeded.
    [[nodiscard]] bool write_madr5(std::uint32_t value) noexcept {
        if (!madr5_known) return false;
        madr5 = value;
        return true;
    }

    // Read SIF0 QWC. Fails if not seeded.
    [[nodiscard]] bool read_qwc5(std::uint32_t& value) const noexcept {
        if (!qwc5_known) return false;
        value = qwc5;
        return true;
    }

    // Write SIF0 QWC. Fails if not seeded.
    [[nodiscard]] bool write_qwc5(std::uint32_t value) noexcept {
        if (!qwc5_known) return false;
        qwc5 = value;
        return true;
    }

    // Read SIF0 TADR. Fails if not seeded.
    [[nodiscard]] bool read_tadr5(std::uint32_t& value) const noexcept {
        if (!tadr5_known) return false;
        value = tadr5;
        return true;
    }

    // Write SIF0 TADR. Fails if not seeded.
    [[nodiscard]] bool write_tadr5(std::uint32_t value) noexcept {
        if (!tadr5_known) return false;
        tadr5 = value;
        return true;
    }

    // VIF0 (channel 1) registers: bounded storage only, reset known zero.
    // No DMA transfer, interrupt, or completion is modelled. ADR 0184.
    [[nodiscard]] bool read_chcr1(std::uint32_t& value) const noexcept {
        value = chcr1;
        return true;
    }
    [[nodiscard]] bool write_chcr1(std::uint32_t value) noexcept {
        chcr1 = value;
        chcr1_known = true;
        return true;
    }
    [[nodiscard]] bool read_madr1(std::uint32_t& value) const noexcept {
        value = madr1;
        return true;
    }
    [[nodiscard]] bool write_madr1(std::uint32_t value) noexcept {
        madr1 = value;
        madr1_known = true;
        return true;
    }
    [[nodiscard]] bool read_qwc1(std::uint32_t& value) const noexcept {
        value = qwc1;
        return true;
    }
    [[nodiscard]] bool write_qwc1(std::uint32_t value) noexcept {
        qwc1 = value;
        qwc1_known = true;
        return true;
    }
    [[nodiscard]] bool read_tadr1(std::uint32_t& value) const noexcept {
        value = tadr1;
        return true;
    }
    [[nodiscard]] bool write_tadr1(std::uint32_t value) noexcept {
        tadr1 = value;
        tadr1_known = true;
        return true;
    }

    // GIF (channel 2) registers: bounded storage only, reset known zero.
    // No DMA transfer, interrupt, or completion is modelled. ADR 0185.
    [[nodiscard]] bool read_chcr2(std::uint32_t& value) const noexcept {
        value = chcr2;
        return true;
    }
    [[nodiscard]] bool write_chcr2(std::uint32_t value) noexcept {
        chcr2 = value;
        return true;
    }
    [[nodiscard]] bool read_madr2(std::uint32_t& value) const noexcept {
        value = madr2;
        return true;
    }
    [[nodiscard]] bool write_madr2(std::uint32_t value) noexcept {
        madr2 = value;
        return true;
    }
    [[nodiscard]] bool read_qwc2(std::uint32_t& value) const noexcept {
        value = qwc2;
        return true;
    }
    [[nodiscard]] bool write_qwc2(std::uint32_t value) noexcept {
        qwc2 = value;
        return true;
    }
    [[nodiscard]] bool read_tadr2(std::uint32_t& value) const noexcept {
        value = tadr2;
        return true;
    }
    [[nodiscard]] bool write_tadr2(std::uint32_t value) noexcept {
        tadr2 = value;
        return true;
    }
};

} // namespace reps2::runtime
