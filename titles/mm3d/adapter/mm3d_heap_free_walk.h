#pragma once

#include "mm3d_guest_patches.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

namespace Oot3dNativeGame {

// MM3D's heap free (mm3d_fun_0012DD70, reached through 0x00151CE4) inserts
// the freed block into the arena's address-ordered free list by walking it
// node by node:
//   0x0012DDC0 cmp  r2, r1          ; freed block vs node
//   0x0012DDC4 bls  0x0012DDDC      ; insertion point found
//   0x0012DDC8 mov  r0, r1          ; previous = node
//   0x0012DDCC ldr  r1, [r1, #0xC]  ; node = node->next
//   0x0012DDD0 cmp  r1, #0
//   0x0012DDD4 bne  0x0012DDC0
//   0x0012DDD8 b    0x0012DE10      ; end of list
// With a long free list one game update can walk it millions of times (East
// Clock Town: about 5 M iterations, 50 ms of compiled code in one update,
// PERF/LINUX_r7 round 5). This site runs the same walk on the host: same
// registers, the same flags at each exit, the same reads; it writes nothing.
// A read the bus refuses declines the site with the registers untouched, so
// the compiled loop runs and faults exactly as before.
inline constexpr std::uint32_t kMm3dHeapFreeWalkPc = 0x0012DDC0U;
inline constexpr std::uint32_t kMm3dHeapFreeWalkFound = 0x0012DDDCU;
inline constexpr std::uint32_t kMm3dHeapFreeWalkEnd = 0x0012DDD8U;
inline constexpr std::array<std::uint32_t, 7> kMm3dHeapFreeWalkWords{
    0xE1520001U, 0x9A000004U, 0xE1A00001U, 0xE591100CU,
    0xE3510000U, 0x1AFFFFF9U, 0xEA00000CU};

// The CPSR after `cmp a, b` (N, Z, C, V; other bits kept).
inline std::uint32_t Mm3dCompareFlags(std::uint32_t cpsr, std::uint32_t a, std::uint32_t b) {
    const std::uint32_t result = a - b;
    const bool n = (result >> 31U) != 0U;
    const bool z = result == 0U;
    const bool c = a >= b;
    const bool v = (((a ^ b) & (a ^ result)) >> 31U) != 0U;
    return (cpsr & 0x0FFFFFFFU) | (n ? 0x80000000U : 0U) | (z ? 0x40000000U : 0U) |
           (c ? 0x20000000U : 0U) | (v ? 0x10000000U : 0U);
}

// Where the previous walk of a list stopped (the node before its insertion
// point), per list head slot (r3 = arena + 0x24; nodes link prev at +0x8 and
// next at +0xC, as the unlink above the loop writes them). Only a hint: it
// starts a walk after it is proven to be a linked node between the list head
// and the freed block, so the walk from it ends exactly where the walk from
// the head would (the list is address-ordered). The walk itself is memory
// latency bound (one dependent load per node), so a shorter walk is the win.
struct Mm3dHeapFreeWalkHints {
    std::unordered_map<std::uint32_t, std::uint32_t> AfterNode;
    std::uint64_t Walks = 0;
    std::uint64_t HintedWalks = 0;
    std::uint64_t Steps = 0;
    std::uint64_t MaximumSteps = 0;
    bool Report = false;
    ~Mm3dHeapFreeWalkHints() {
        if (Report)
            std::fprintf(stderr,
                         "TRIAEVUM_MM3D_HEAP_FREE_WALK walks=%llu hinted=%llu steps=%llu max_steps=%llu\n",
                         static_cast<unsigned long long>(Walks), static_cast<unsigned long long>(HintedWalks),
                         static_cast<unsigned long long>(Steps), static_cast<unsigned long long>(MaximumSteps));
    }
};

// A hint node H may start the walk when head < H < freed and H is linked:
// H->prev->next == H and (H->next == 0 or H->next->prev == H).
inline bool Mm3dHeapFreeWalkHintValid(oot3d::recomp::a32::MemoryBus& memory, std::uint32_t head,
                                      std::uint32_t freed, std::uint32_t hint, std::uint32_t* previous) {
    if (hint == 0U || hint <= head || freed <= hint) return false;
    std::uint32_t prev = 0, next = 0, check = 0;
    if (!memory.Read32(hint + 0x8U, &prev) || !memory.Read32(hint + 0xCU, &next)) return false;
    if (prev == 0U || !memory.Read32(prev + 0xCU, &check) || check != hint) return false;
    if (next != 0U && (!memory.Read32(next + 0x8U, &check) || check != hint)) return false;
    *previous = prev;
    return true;
}

inline std::optional<std::uint32_t> RunMm3dHeapFreeWalk(oot3d::recomp::a32::GuestState& state,
                                                        oot3d::recomp::a32::MemoryBus& memory,
                                                        Mm3dHeapFreeWalkHints* hints = nullptr) {
    const std::uint32_t freed = state.r[2];
    std::uint32_t previous = state.r[0];
    std::uint32_t node = state.r[1];
    const std::uint32_t listSlot = state.r[3];
    std::uint64_t steps = 0;
    if (hints != nullptr) {
        ++hints->Walks;
        // Only a walk that starts at the head (previous = 0) may jump ahead.
        const auto found = previous == 0U ? hints->AfterNode.find(listSlot) : hints->AfterNode.end();
        std::uint32_t hintPrevious = 0;
        if (found != hints->AfterNode.end() &&
            Mm3dHeapFreeWalkHintValid(memory, node, freed, found->second, &hintPrevious)) {
            previous = hintPrevious;
            node = found->second;
            ++hints->HintedWalks;
        }
    }
    const auto finish = [&] {
        if (hints == nullptr) return;
        hints->Steps += steps;
        hints->MaximumSteps = std::max(hints->MaximumSteps, steps);
    };
    for (;;) {
        if (freed <= node) {  // bls: C clear or Z set
            state.r[0] = previous;
            state.r[1] = node;
            state.cpsr = Mm3dCompareFlags(state.cpsr, freed, node);
            if (hints != nullptr && previous != 0U) hints->AfterNode[listSlot] = previous;
            finish();
            return kMm3dHeapFreeWalkFound;
        }
        std::uint32_t next = 0;
        if (!memory.Read32(node + 0xCU, &next)) {
            // Nothing written yet: the guest loop runs from its entry and
            // faults where it did before.
            return std::nullopt;
        }
        ++steps;
        previous = node;
        node = next;
        if (node == 0U) {
            state.r[0] = previous;
            state.r[1] = 0U;
            state.cpsr = Mm3dCompareFlags(state.cpsr, 0U, 0U);
            if (hints != nullptr) hints->AfterNode[listSlot] = previous;
            finish();
            return kMm3dHeapFreeWalkEnd;
        }
    }
}

inline void RegisterMm3dHeapFreeWalk(Mm3dGuestPatches& patches) {
    Mm3dGuestPatchSite site;
    site.Pc = kMm3dHeapFreeWalkPc;
    site.Name = "MM3D heap free-list walk";
    // Continues at the walk's two exits, never at Pc + 4.
    site.HandlerReturnsNext = false;
    for (std::uint32_t i = 0; i < kMm3dHeapFreeWalkWords.size(); ++i)
        site.Guard.emplace_back(site.Pc + 4U * i, kMm3dHeapFreeWalkWords[i]);
    auto hints = std::make_shared<Mm3dHeapFreeWalkHints>();
    hints->Report = true;
    site.Handler = [hints](oot3d::recomp::a32::GuestState& state, oot3d::recomp::a32::MemoryBus& memory) {
        return RunMm3dHeapFreeWalk(state, memory, hints.get());
    };
    patches.Add(std::move(site));
    patches.AddTargets({kMm3dHeapFreeWalkFound, kMm3dHeapFreeWalkEnd});
}

} // namespace Oot3dNativeGame
