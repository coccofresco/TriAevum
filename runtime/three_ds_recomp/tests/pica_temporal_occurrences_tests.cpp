#include "fast/renderer3ds/pica_temporal_occurrences.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

using Fast::Renderer3ds::PicaTemporalOccurrences;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    constexpr uint64_t mesh = 0x1234, scene = 7, redraw = 9;
    PicaTemporalOccurrences occurrences;
    // Frame 1 has only the displayed scene: two instances share one resource.
    const auto first = occurrences.Next(mesh, scene);
    const auto second = occurrences.Next(mesh, scene);
    Require(first == 0 && second == 1, "instances must receive distinct slots");
    occurrences.clear();
    // Frame 2 inserts another target's redraw before and between scene draws.
    (void)occurrences.Next(mesh, redraw);
    Require(occurrences.Next(mesh, scene) == first, "redraw shifted first scene history");
    (void)occurrences.Next(mesh, redraw);
    Require(occurrences.Next(mesh, scene) == second, "redraw shifted second scene history");
    occurrences.clear();
    // Frame 3 removes that redraw. Both history slots must still match.
    Require(occurrences.Next(mesh, scene) == first, "removed redraw shifted first history");
    Require(occurrences.Next(mesh, scene) == second, "removed redraw shifted second history");

    // Every interleaving of four draws from each of two namespaces preserves
    // each stream's local ordinal. Insertion/order of unrelated work is free.
    for (uint32_t schedule = 0; schedule < 256; ++schedule) {
        uint32_t ones = 0;
        for (uint32_t i = 0; i < 8; ++i) ones += (schedule >> i) & 1U;
        if (ones != 4) continue;
        occurrences.clear();
        std::array<uint32_t, 2> expected{};
        for (uint32_t i = 0; i < 8; ++i) {
            const auto stream = (schedule >> i) & 1U;
            Require(occurrences.Next(mesh, stream) == expected[stream]++, "interleaved stream changed ordinal");
        }
    }
    occurrences.clear();
    Require(occurrences.Next(0, 0) == 0, "zero IDs are valid inputs");
    Require(occurrences.Next(1, 0) == 0, "different geometry shares a counter");
    Require(occurrences.Next(0, 1) == 0, "different namespace shares a counter");
    Require(occurrences.Next(0, 0) == 1, "original counter was lost");
    occurrences.clear();
    Require(occurrences.Next(0, 0) == 0, "frame reset failed");

    // Demonstrate why the former backend counter failed the same scenario.
    std::unordered_map<uint64_t, uint32_t> oldOccurrences;
    (void)oldOccurrences[mesh]++; // unrelated redraw
    Require(oldOccurrences[mesh]++ != first, "legacy regression scenario is ineffective");
    std::cout << "PICA temporal occurrence tests passed (70 interleavings, insertion/removal, reset)\n";
}
