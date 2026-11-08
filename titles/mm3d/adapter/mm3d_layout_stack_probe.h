#pragma once

// Developer diagnostic, off unless TRIAEVUM_MM3D_SP_PROBE names an output
// file: the stack headroom of the thread that draws dekui's layouts, at the
// Root (NativeInventory SVC case 2, before the native layout-draw BL) and End
// (case 3, after it) hook PCs.
//
// At Root it records the thread's SP before and after dekui's Root chain
// (the difference is every scratch the chain placed under the SP, the
// HudItemBackground 0x1200 bytes among them) and the mapped stack range
// around it. To measure how deep the native draw itself goes, it fills the
// words below the lowered SP with an address-dependent pattern and, at End,
// finds the lowest word the draw changed; every word still holding the
// pattern gets its earlier value back, so after End the guest memory is what
// it would be without the probe (the draw's own writes stand).
// One line per Root/End pair.

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

class Mm3dLayoutStackProbe {
  public:
    static constexpr std::uint32_t kRootPc = 0x001262C8U; // dekui NativeInventory PCs[2]
    static constexpr std::uint32_t kEndPc = 0x001262D4U;  // dekui NativeInventory PCs[3]
    static constexpr std::uint32_t kPage = 0x1000U;
    // Farthest the stack range is followed from the SP either way.
    static constexpr std::uint32_t kScanLimit = 0x100000U;
    // Deepest region under the lowered SP that is filled and checked.
    static constexpr std::uint32_t kPaintWindow = 0x10000U;

    explicit Mm3dLayoutStackProbe(std::FILE* out) : mOut(out) {}
    ~Mm3dLayoutStackProbe() {
        if (mOut != nullptr) {
            std::fclose(mOut);
        }
    }
    Mm3dLayoutStackProbe(const Mm3dLayoutStackProbe&) = delete;
    Mm3dLayoutStackProbe& operator=(const Mm3dLayoutStackProbe&) = delete;

    static std::unique_ptr<Mm3dLayoutStackProbe> FromEnvironment() {
        const char* path = std::getenv("TRIAEVUM_MM3D_SP_PROBE");
        if (path == nullptr || *path == '\0') {
            return nullptr;
        }
        std::FILE* out = std::fopen(path, "w");
        if (out == nullptr) {
            return nullptr;
        }
        std::fprintf(out, "# mm3d layout-draw stack probe: root_pc=%08X end_pc=%08X window=%X\n",
                     kRootPc, kEndPc, kPaintWindow);
        return std::make_unique<Mm3dLayoutStackProbe>(out);
    }

    static std::uint32_t Pattern(std::uint32_t address) { return 0x5EADC0DEU ^ (address * 0x9E3779B1U); }

    void BeforeRoot(const oot3d::recomp::a32::GuestState& state, oot3d::recomp::a32::MemoryBus& bus) {
        Pending pending;
        pending.EntrySp = state.r[13];
        pending.Layout = state.r[4];
        pending.ThreadPointer = state.thread_pointer;
        pending.Name = LayoutName(bus, state.r[4]);
        mPending.push_back(std::move(pending));
    }

    // hudItemBackgroundSp: the SP HudItemBackground's Root committed its
    // scratch under in this call, 0 when it did not commit.
    void AfterRoot(const oot3d::recomp::a32::GuestState& state, oot3d::recomp::a32::MemoryBus& bus,
                   std::uint32_t hudItemBackgroundSp) {
        if (mPending.empty()) {
            return;
        }
        Pending& p = mPending.back();
        p.Index = mIndex++;
        p.ExitSp = state.r[13];
        p.HudItemBackgroundSp = hudItemBackgroundSp;
        p.StackLow = MappedLow(bus, p.ExitSp);
        p.StackHigh = MappedHigh(bus, p.EntrySp);
        const std::uint32_t floor =
            p.ExitSp - p.StackLow > kPaintWindow ? p.ExitSp - kPaintWindow : p.StackLow;
        p.PaintLow = floor;
        p.Saved.reserve((p.ExitSp - floor) / 4U);
        for (std::uint32_t at = floor; at < p.ExitSp; at += 4U) {
            std::uint32_t value = 0;
            if (!bus.Read32(at, &value)) {
                p.PaintLow = at + 4U;
                p.Saved.clear();
                continue;
            }
            p.Saved.push_back(value);
        }
        for (std::uint32_t at = p.PaintLow, i = 0; at < p.ExitSp; at += 4U, ++i) {
            (void)bus.Write32(at, Pattern(at));
        }
        p.Painted = true;
    }

    void BeforeEnd(const oot3d::recomp::a32::GuestState& state, oot3d::recomp::a32::MemoryBus& bus) {
        if (mPending.empty()) {
            return;
        }
        Pending p = std::move(mPending.back());
        mPending.pop_back();
        std::uint32_t deepest = p.ExitSp;
        if (p.Painted) {
            for (std::uint32_t at = p.PaintLow, i = 0; at < p.ExitSp; at += 4U, ++i) {
                std::uint32_t value = 0;
                if (!bus.Read32(at, &value)) {
                    continue;
                }
                if (value == Pattern(at)) {
                    (void)bus.Write32(at, p.Saved[i]);
                } else if (at < deepest) {
                    deepest = at;
                }
            }
        }
        const bool exhausted = p.Painted && deepest == p.PaintLow && p.PaintLow > p.StackLow;
        std::fprintf(mOut,
                     "pair=%u tp=%08X layout=%08X name=%s entry_sp=%08X exit_sp=%08X end_sp=%08X "
                     "chain_scratch=%X hib=%d hib_sp=%08X stack_low=%08X stack_high=%08X "
                     "headroom=%X deepest=%08X draw_bytes=%X margin=%X exhausted=%d\n",
                     p.Index, p.ThreadPointer, p.Layout, p.Name.empty() ? "-" : p.Name.c_str(),
                     p.EntrySp, p.ExitSp, state.r[13], p.EntrySp - p.ExitSp,
                     p.HudItemBackgroundSp != 0 ? 1 : 0, p.HudItemBackgroundSp, p.StackLow,
                     p.StackHigh, p.ExitSp - p.StackLow, deepest, p.ExitSp - deepest,
                     deepest - p.StackLow, exhausted ? 1 : 0);
        if ((p.Index & 255U) == 0U) {
            std::fflush(mOut);
        }
    }

  private:
    struct Pending {
        std::uint32_t Index = 0, EntrySp = 0, ExitSp = 0, Layout = 0, ThreadPointer = 0;
        std::uint32_t HudItemBackgroundSp = 0, StackLow = 0, StackHigh = 0, PaintLow = 0;
        std::string Name;
        std::vector<std::uint32_t> Saved;
        bool Painted = false;
    };

    static bool Mapped(oot3d::recomp::a32::MemoryBus& bus, std::uint32_t address) {
        std::uint32_t value = 0;
        return bus.Read32(address, &value);
    }
    // Lowest address of the contiguous mapped pages that hold sp.
    static std::uint32_t MappedLow(oot3d::recomp::a32::MemoryBus& bus, std::uint32_t sp) {
        std::uint32_t low = sp & ~(kPage - 1U);
        while (sp - low < kScanLimit && low >= kPage && Mapped(bus, low - kPage)) {
            low -= kPage;
        }
        return low;
    }
    static std::uint32_t MappedHigh(oot3d::recomp::a32::MemoryBus& bus, std::uint32_t sp) {
        std::uint32_t high = (sp & ~(kPage - 1U)) + kPage;
        while (high - sp < kScanLimit && high != 0 && Mapped(bus, high)) {
            high += kPage;
        }
        return high;
    }
    // The layout's resource name (dekui NativeInventory::LayoutNamed reads
    // the same fields: *(layout+4)+0x18).
    static std::string LayoutName(oot3d::recomp::a32::MemoryBus& bus, std::uint32_t layout) {
        std::uint32_t resource = 0, name = 0;
        if (layout < 0x100000U || !bus.Read32(layout + 4U, &resource) || resource < 0x100000U ||
            !bus.Read32(resource + 0x18U, &name) || name < 0x100000U) {
            return {};
        }
        std::string result;
        for (std::uint32_t i = 0; i < 48U; ++i) {
            std::uint8_t c = 0;
            if (!bus.Read8(name + i, &c) || c == 0) {
                break;
            }
            result.push_back(c > 0x20 && c < 0x7F ? static_cast<char>(c) : '?');
        }
        return result;
    }

    std::FILE* mOut;
    std::uint32_t mIndex = 0;
    std::vector<Pending> mPending;
};

} // namespace Oot3dNativeGame
