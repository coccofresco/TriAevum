// External diagnostic only. No injection, guest callbacks or title rebuild.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

struct Handle {
    HANDLE Value = nullptr;
    ~Handle() { if (Value && Value != INVALID_HANDLE_VALUE) CloseHandle(Value); }
};
struct Thread {
    DWORD Id;
    HANDLE Value;
    ULONG64 Cycles = 0;
};
struct Suspension {
    HANDLE Thread;
    bool Active;
    explicit Suspension(HANDLE thread) : Thread(thread), Active(SuspendThread(thread) != DWORD(-1)) {}
    ~Suspension() { if (Active && ResumeThread(Thread) == DWORD(-1)) std::terminate(); }
};

int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "Usage: sampler PID SECONDS OUTPUT.json\n"; return 2; }
    const DWORD pid = std::stoul(argv[1]);
    const double seconds = std::stod(argv[2]);
    if (!pid || pid == GetCurrentProcessId() || seconds <= 0 || seconds > 120) return 2;
    Handle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid)};
    if (!process.Value) return 3;
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS |
                  SYMOPT_NO_PROMPTS | SYMOPT_IGNORE_NT_SYMPATH);
    if (!SymInitialize(process.Value, "", FALSE)) return 4;
    nlohmann::json modules = nlohmann::json::array();
    {
        Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)};
        MODULEENTRY32 module{}; module.dwSize = sizeof(module);
        if (Module32First(snapshot.Value, &module)) do {
            const auto base = reinterpret_cast<DWORD64>(module.modBaseAddr);
            const auto loaded = SymLoadModuleEx(process.Value, nullptr, module.szExePath,
                nullptr, base, module.modBaseSize, nullptr, 0);
            // Force local unwind metadata to load before suspending any thread.
            SymFunctionTableAccess64(process.Value, base + 0x1000);
            modules.push_back({{"path", module.szExePath}, {"base", base},
                               {"size", module.modBaseSize}, {"symbols_loaded", loaded != 0}});
        } while (Module32Next(snapshot.Value, &module));
    }
    std::vector<Thread> threads;
    {
        Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)};
        THREADENTRY32 thread{}; thread.dwSize = sizeof(thread);
        if (Thread32First(snapshot.Value, &thread)) do {
            if (thread.th32OwnerProcessID != pid) continue;
            HANDLE handle = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT |
                                       THREAD_SUSPEND_RESUME, FALSE, thread.th32ThreadID);
            if (handle) {
                Thread item{thread.th32ThreadID, handle};
                QueryThreadCycleTime(handle, &item.Cycles);
                threads.push_back(item);
            }
        } while (Thread32Next(snapshot.Value, &thread));
    }
    std::map<std::vector<DWORD64>, unsigned> stacks;
    std::map<DWORD, unsigned> threadSamples;
    unsigned failures = 0, truncated = 0, walkStops = 0, nonProgress = 0;
    double suspendedSeconds = 0;
    std::mt19937 random(0xA032);
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
    while (elapsed() < seconds && WaitForSingleObject(process.Value, 0) != WAIT_OBJECT_0) {
        Sleep(2 + random() % 5);
        Thread* selected = nullptr;
        ULONG64 largest = 0;
        for (auto& thread : threads) {
            ULONG64 now = 0;
            if (!QueryThreadCycleTime(thread.Value, &now)) continue;
            const auto delta = now - thread.Cycles;
            thread.Cycles = now;
            if (delta > largest) { largest = delta; selected = &thread; }
        }
        if (!selected) continue;
        std::vector<DWORD64> stack;
        stack.reserve(32);
        const auto pauseStart = std::chrono::steady_clock::now();
        {
            Suspension suspended(selected->Value);
            CONTEXT context{}; context.ContextFlags = CONTEXT_FULL;
            if (!suspended.Active || !GetThreadContext(selected->Value, &context)) { ++failures; continue; }
            stack.push_back(context.Rip);
            STACKFRAME64 frame{};
            frame.AddrPC = {context.Rip, 0, AddrModeFlat};
            frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
            frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
            DWORD64 priorSp = context.Rsp;
            for (unsigned depth = 0; depth < 32; ++depth) {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process.Value, selected->Value,
                    &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
                    ++walkStops; break;
                }
                if (!frame.AddrPC.Offset) break;
                if (frame.AddrPC.Offset == stack.back() && depth == 0) continue;
                if (frame.AddrStack.Offset <= priorSp) { ++nonProgress; break; }
                priorSp = frame.AddrStack.Offset;
                stack.push_back(frame.AddrPC.Offset);
                if (stack.size() == 32) { ++truncated; break; }
            }
        }
        suspendedSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - pauseStart).count();
        ++stacks[stack]; ++threadSamples[selected->Id];
    }
    for (auto& thread : threads) CloseHandle(thread.Value);
    SymCleanup(process.Value);
    nlohmann::json records = nlohmann::json::array();
    unsigned count = 0;
    for (const auto& [stack, samples] : stacks) {
        records.push_back({{"pcs", stack}, {"count", samples}}); count += samples;
    }
    nlohmann::json result{{"method", "intrusive remote StackWalk64; sampled locations, not exclusive CPU time"},
        {"seconds", elapsed()}, {"suspended_seconds", suspendedSeconds},
        {"samples", count}, {"context_failures", failures}, {"stackwalk_stops", walkStops},
        {"nonprogress_stops", nonProgress},
        {"depth_limit_walks", truncated}, {"modules", modules}, {"stacks", records},
        {"threads", threadSamples}};
    std::ofstream out(argv[3]); out << result.dump(2);
    if (!out) return 5;
    std::cout << "Stack samples: " << count << ", suspended seconds: " << suspendedSeconds << '\n';
    return count ? 0 : 6;
}
