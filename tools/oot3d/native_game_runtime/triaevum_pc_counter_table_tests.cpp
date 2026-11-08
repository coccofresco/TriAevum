// G2-F5: the whole-AOT observable-exit accounting is exact and cheap. The
// table must count every exit per PC (also from several threads and past a
// full table), and its per-exit cost is measured against the previous
// mutex + std::map accounting (d0bdaf72) as the receipt: the table must not
// be slower than it.
#include "triaevum_pc_counter_table.h"

#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

using namespace Oot3dNativeGame;

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "triaevum_pc_counter_table_tests: %s\n", message);
        ++failures;
    }
}

// The accounting d0bdaf72 shipped: a global mutex and a std::map insert.
struct MutexMapCounters {
    std::mutex Mutex;
    std::map<uint32_t, uint64_t> ExitsByPc;
    void Add(uint32_t pc) {
        std::scoped_lock lock(Mutex);
        ++ExitsByPc[pc];
    }
};

// 19 exit sites, as a frame of OoT3D whole-AOT play sees them.
constexpr uint32_t kSites[19] = {0x00100A10, 0x00101F40, 0x00117C2C, 0x0011A004, 0x0012AB40,
                                 0x0012F7E8, 0x00134000, 0x0013C1A0, 0x00151270, 0x0015D3B4,
                                 0x00160008, 0x0016A0C0, 0x00178E2C, 0x0018000C, 0x0019B7F0,
                                 0x001A2C44, 0x001B0000, 0x001C3D10, 0x001FFFFC};

template <typename Counter>
double NanosecondsPerExit(Counter& counter, int frames) {
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < frames; ++frame)
        for (const uint32_t pc : kSites) counter.Add(pc);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return double(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) /
           double(frames * int(std::size(kSites)));
}
} // namespace

int main() {
    {
        PcCounterTable<64> table;
        table.Add(0x100);
        table.Add(0x100);
        table.Add(0);
        table.Add(0xFFFFFFFFU, 5);
        uint64_t total = 0;
        const auto snapshot = table.Snapshot(&total);
        Check(total == 8U, "total is not exact");
        Check(snapshot.size() == 3U && snapshot[0] == std::pair<uint32_t, uint64_t>{0U, 1U} &&
                  snapshot[1] == std::pair<uint32_t, uint64_t>{0x100U, 2U} &&
                  snapshot[2] == std::pair<uint32_t, uint64_t>{0xFFFFFFFFU, 5U},
              "per-PC counts are wrong (PC 0 and 0xFFFFFFFF included)");
    }
    {
        // Negative: more PCs than slots; the excess is counted, not lost.
        PcCounterTable<4> table;
        for (uint32_t pc = 1; pc <= 6; ++pc) table.Add(pc * 4U);
        uint64_t total = 0;
        const auto snapshot = table.Snapshot(&total);
        Check(snapshot.size() == 4U && table.Overflow() == 2U && total == 6U,
              "a full table lost or misplaced exits");
    }
    {
        // Several threads count the same sites exactly.
        PcCounterTable<256> table;
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&] {
                for (int i = 0; i < 10000; ++i)
                    for (const uint32_t pc : kSites) table.Add(pc);
            });
        for (auto& thread : threads) thread.join();
        uint64_t total = 0;
        const auto snapshot = table.Snapshot(&total);
        bool exact = snapshot.size() == std::size(kSites);
        for (const auto& [pc, count] : snapshot) exact = exact && count == 40000U;
        Check(exact && total == 40000U * std::size(kSites), "concurrent counts are not exact");
    }
    // Micro-benchmark (receipt): per-exit cost, table vs mutex + map.
    constexpr int kFrames = 200000;
    MutexMapCounters previous;
    PcCounterTable<256> table;
    (void)NanosecondsPerExit(previous, 1000); // warm both
    (void)NanosecondsPerExit(table, 1000);
    double previousNs = 1e30, tableNs = 1e30;
    for (int round = 0; round < 5; ++round) {
        previousNs = std::min(previousNs, NanosecondsPerExit(previous, kFrames));
        tableNs = std::min(tableNs, NanosecondsPerExit(table, kFrames));
    }
    std::printf("observable-exit accounting: mutex+map %.2f ns/exit, table %.2f ns/exit "
                "(%.2f vs %.2f us per 19-exit frame)\n",
                previousNs, tableNs, previousNs * 19.0 / 1000.0, tableNs * 19.0 / 1000.0);
    Check(tableNs <= previousNs, "the table is slower than the mutex + map accounting");
    if (failures != 0) return 1;
    std::printf("triaevum_pc_counter_table_tests: ok\n");
    return 0;
}
