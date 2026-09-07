#include "triaevum_title_whole_aot_abi.h"
#include "oot3d_native_a32_memory.h"
#include <windows.h>
#include <memory>
#include <iostream>

using namespace Oot3dNativeGame;
namespace a32 = oot3d::recomp::a32;

struct CallbackState {
    const Oot3dWholeAotProgramV2* Program;
    unsigned Calls = 0;
    bool Active = false;
    bool Exit = false;
};

void OnBlock(uint32_t, a32::GuestState& guest, a32::MemoryBus&, void* user) {
    auto& state = *static_cast<CallbackState*>(user);
    ++state.Calls;
    state.Active = state.Program->ExecutionActive();
    if (state.Exit) {
        guest.r[0] = 99;
        state.Program->ObservableExit(0x3330, &guest);
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const auto module = LoadLibraryExW(argv[1], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) return 3;
    const auto query = reinterpret_cast<const Oot3dWholeAotProgramV2* (*)(uint32_t) noexcept>(
        GetProcAddress(module, "triaevum_title_whole_aot_query"));
    if (!query || query(1) != nullptr) return 4;
    const auto program = query(kOot3dWholeAotPluginAbiV2);
    if (!program || program->StructSize != sizeof(*program) || program->EntryPointCount != 1 ||
        program->EntryPoints[0] != 0x1000 || !program->Execute || !program->ExecutionActive ||
        !program->ObservableExit || program->ExecutionActive()) return 5;
    auto memory = std::make_unique<NativeA32Memory>();
    NativeA32MemoryRegionConfig region;
    region.Name = "synthetic-data";
    region.BaseAddress = 0x2000;
    region.Size = 4096;
    region.Writable = true;
    if (!memory->MapRegion(region)) return 6;
    a32::GuestState guest;
    guest.r[1] = 0x2000;
    guest.r[14] = 0x4000;
    a32::ExecutionResult result;
    Oot3dWholeAotStats stats;
    uint32_t consumed = 0;
    const uint32_t block = 0x1000;
    CallbackState callbacks{program};
    auto execute = [&] {
        return program->Execute(0x1000, guest, *memory, &result, &stats, nullptr,
            8, &consumed, OnBlock, &callbacks, &block, 1, nullptr, false, 0x4000);
    };
    if (!execute()) return 7;
    uint32_t stored = 0;
    if (guest.r[0] != 3 || guest.r[2] != 3 || !memory->Read32(0x2000, &stored) || stored != 3 ||
        result.pc != 0x4000 || !callbacks.Active || callbacks.Calls != 1 ||
        consumed == 0 || stats.Calls != 1 || program->ExecutionActive()) return 8;
    callbacks.Exit = true;
    if (!execute() || guest.r[0] != 99 || result.pc != 0x3330 ||
        callbacks.Calls != 2 || program->ExecutionActive()) return 9;
    std::cout << "{\"arithmetic\":true,\"memory\":true,\"callback\":true,"
                 "\"tls\":true,\"observable_exit\":true,\"abi_rejection\":true}\n";
    return 0;
}
