#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {

class NativeA32Memory;

struct NativeA32DspPhysicalRegion {
    uint32_t PhysicalAddress = 0;
    uint32_t VirtualAddress = 0;
    size_t Size = 0;
};

struct NativeDspMemoryAccess {
    std::function<bool(uint32_t, std::span<uint8_t>)> Read;
    std::function<bool(uint32_t, std::span<const uint8_t>)> Write;
    std::function<const uint8_t*(uint32_t)> ResolvePhysical;
};

class NativeA32DspHle {
  public:
    static constexpr uint32_t NativeSampleRate = 32728;
    static constexpr uint32_t SamplesPerFrame = 160;

    explicit NativeA32DspHle(
        std::vector<NativeA32DspPhysicalRegion> physicalRegions);
    ~NativeA32DspHle();

    NativeA32DspHle(const NativeA32DspHle&) = delete;
    NativeA32DspHle& operator=(const NativeA32DspHle&) = delete;

    bool ProcessFrame(NativeA32Memory& memory,
                      std::vector<int16_t>& interleavedStereo,
                      std::string* error = nullptr);
    bool ProcessFrame(const NativeDspMemoryAccess& memory,
                      std::vector<int16_t>& interleavedStereo,
                      std::string* error = nullptr);

    nlohmann::json CaptureState() const;
    bool RestoreState(const nlohmann::json& state,
                      std::string* error = nullptr);

  private:
    std::span<const NativeA32DspPhysicalRegion> PhysicalRegions() const;
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Oot3dNativeGame
