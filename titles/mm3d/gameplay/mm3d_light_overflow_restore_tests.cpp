// Light-arena overflow across a savestate restore, on the real guest memory
// (NativeA32Memory CaptureState/RestoreState, the savestate's memory payload)
// and the real mapping helper the MM3D title session uses.
#include "mm3d_light_arena.h"
#include "oot3d_native_a32_memory.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <source_location>
#include <string>

using namespace Oot3dNativeGame;
using A = Mm3dLightArena;

static void Check(bool ok, const char* what, std::source_location at = std::source_location::current()) {
    if (!ok) { std::cerr << "mm3d_light_overflow_restore_tests: " << what << " (line " << at.line() << ")\n"; std::exit(1); }
}

static NativeA32Memory Restored(const nlohmann::json& state) {
    NativeA32Memory memory;
    std::string error;
    Check(memory.RestoreState(state, &error), "restore failed");
    return memory;
}

int main(int argc, char** argv) {
    const bool negative = argc > 1 && std::strcmp(argv[1], "--negative") == 0;
    NativeA32Memory live;
    std::string error;
    // A guest region elsewhere so the state is not empty before the mapping.
    Check(live.MapRegion({"guest", 0x00100000U, 0x1000U, true, false, {}}, &error), "guest map");
    const auto beforeOverflow = live.CaptureState();

    Check(!EnsureMm3dLightOverflowMapping(live, false), "create=false must not map");
    Check(!live.IsMapped(A::MappingBase, A::MappingSize), "create=false mapped");
    Check(EnsureMm3dLightOverflowMapping(live, true), "first overflow mapping");
    // An overflow record in bank 1 (the previous-frame bank may still be referenced).
    const uint32_t record = A::Overflow(1).Top() - 0x100U;
    Check(live.Write32(record, 0xC0FFEE01U), "record write");
    const auto withOverflow = live.CaptureState();

    // 1. Restore a state that holds the mapping: region and record come back
    //    and the helper revalidates without remapping.
    auto restored = Restored(withOverflow);
    Check(EnsureMm3dLightOverflowMapping(restored, false), "restored mapping not revalidated");
    uint32_t word = 0;
    Check(restored.Read32(record, &word) && word == 0xC0FFEE01U, "overflow record lost on restore");
    std::cout << "restore-with-mapping: revalidated, record preserved\n";

    // 2. Restore a state captured before the mapping existed: the mapping is
    //    gone with it, a stale host flag must not claim it, and only an
    //    allocation (create=true) maps it again, freshly headed.
    auto older = Restored(beforeOverflow);
    const bool staleAccepted = EnsureMm3dLightOverflowMapping(older, false);
    if (negative) {
        // Recorded negative: demand the stale acceptance a host-flag design
        // would give. The real helper refuses it, so this must exit 1.
        Check(staleAccepted, "NEGATIVE: restored pre-overflow state has no mapping (expected failure)");
    }
    Check(!staleAccepted, "pre-overflow restore accepted a mapping");
    Check(EnsureMm3dLightOverflowMapping(older, true), "remap after older restore");
    Check(older.Read32(record, &word) && word == 0U, "remapped overflow not fresh");
    std::cout << "restore-before-mapping: refused until re-created, fresh\n";

    // 3. A state whose identity header is corrupt is refused, never adopted.
    auto corrupt = withOverflow;
    for (auto& region : corrupt["regions"])
        if (region.at("base_address").get<uint32_t>() == A::MappingBase)
            region["bytes"].get_binary()[0] ^= 0xFFU;
    auto bad = Restored(corrupt);
    Check(!EnsureMm3dLightOverflowMapping(bad, true), "corrupt identity header accepted");

    // 4. An executable or differently sized region at the base is refused.
    auto exec = withOverflow;
    for (auto& region : exec["regions"])
        if (region.at("base_address").get<uint32_t>() == A::MappingBase) region["executable"] = true;
    auto badExec = Restored(exec);
    Check(!EnsureMm3dLightOverflowMapping(badExec, true), "executable overflow region accepted");
    NativeA32Memory partial;
    Check(partial.MapRegion({"other", A::MappingBase, 0x1000U, true, false, {}}, &error), "partial map");
    Check(!EnsureMm3dLightOverflowMapping(partial, true), "partial foreign region appropriated");
    std::cout << "corrupt/executable/partial: refused\n";
    std::cout << "mm3d_light_overflow_restore_tests: OK\n";
    return 0;
}
