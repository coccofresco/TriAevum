#include "oot3d_native_a32_dsp_hle.h"

#include "audio_core/hle/mixers.h"
#include "audio_core/hle/source.h"

#include <array>
#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kDspRegion0Address = 0x1FF50000U;
constexpr uint32_t kDspRegion1Address = 0x1FF70000U;

void SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

size_t CurrentRegionIndex(const AudioCore::HLE::SharedMemory& region0,
                          const AudioCore::HLE::SharedMemory& region1) {
    const uint16_t frame0 = region0.frame_counter;
    const uint16_t frame1 = region1.frame_counter;
    if (frame0 == 0xFFFFU && frame1 != 0xFFFEU) {
        return 1;
    }
    if (frame1 == 0xFFFFU && frame0 != 0xFFFEU) {
        return 0;
    }
    return frame0 > frame1 ? 0U : 1U;
}

} // namespace

struct NativeA32DspHle::Impl {
    explicit Impl(std::vector<NativeA32DspPhysicalRegion> regions)
        : PhysicalRegions(std::move(regions)) {
        Sources.reserve(AudioCore::HLE::num_sources);
        for (size_t index = 0; index < AudioCore::HLE::num_sources; ++index) {
            Sources.emplace_back(index);
            Sources.back().SetMemoryResolver([this](uint32_t address) {
                return CurrentMemory != nullptr && CurrentMemory->ResolvePhysical
                           ? CurrentMemory->ResolvePhysical(address)
                           : nullptr;
            });
        }
    }

    std::vector<NativeA32DspPhysicalRegion> PhysicalRegions;
    std::vector<AudioCore::HLE::Source> Sources;
    AudioCore::HLE::Mixers Mixers;
    const NativeDspMemoryAccess* CurrentMemory = nullptr;
};

NativeA32DspHle::NativeA32DspHle(
    std::vector<NativeA32DspPhysicalRegion> physicalRegions)
    : mImpl(std::make_unique<Impl>(std::move(physicalRegions))) {}

NativeA32DspHle::~NativeA32DspHle() = default;

std::span<const NativeA32DspPhysicalRegion>
NativeA32DspHle::PhysicalRegions() const {
    return mImpl->PhysicalRegions;
}

bool NativeA32DspHle::ProcessFrame(
    const NativeDspMemoryAccess& memory,
    std::vector<int16_t>& interleavedStereo, std::string* error) {
    if (!memory.Read || !memory.Write || !memory.ResolvePhysical) {
        SetError(error, "native DSP memory contract is incomplete");
        return false;
    }
    AudioCore::HLE::SharedMemory region0{};
    AudioCore::HLE::SharedMemory region1{};
    auto region0Bytes = std::span<uint8_t>(
        reinterpret_cast<uint8_t*>(&region0), sizeof(region0));
    auto region1Bytes = std::span<uint8_t>(
        reinterpret_cast<uint8_t*>(&region1), sizeof(region1));
    if (!memory.Read(kDspRegion0Address, region0Bytes) ||
        !memory.Read(kDspRegion1Address, region1Bytes)) {
        SetError(error, "native DSP shared memory is not mapped");
        return false;
    }

    mImpl->CurrentMemory = &memory;

    const size_t readIndex = CurrentRegionIndex(region0, region1);
    auto& read = readIndex == 0U ? region0 : region1;
    auto& write = readIndex == 0U ? region1 : region0;
    std::array<AudioCore::QuadFrame32, 3> intermediateMixes{};
    for (size_t sourceIndex = 0; sourceIndex < mImpl->Sources.size();
         ++sourceIndex) {
        write.source_statuses.status[sourceIndex] =
            mImpl->Sources[sourceIndex].Tick(
                read.source_configurations.config[sourceIndex],
                read.adpcm_coefficients.coeff[sourceIndex]);
        for (size_t mix = 0; mix < intermediateMixes.size(); ++mix) {
            mImpl->Sources[sourceIndex].MixInto(intermediateMixes[mix], mix);
        }
    }

    write.dsp_status = mImpl->Mixers.Tick(
        read.dsp_configuration, read.intermediate_mix_samples,
        write.intermediate_mix_samples, intermediateMixes);
    const auto output = mImpl->Mixers.GetOutput();
    interleavedStereo.reserve(interleavedStereo.size() + output.size() * 2U);
    for (size_t sample = 0; sample < output.size(); ++sample) {
        for (size_t channel = 0; channel < output[sample].size(); ++channel) {
            write.final_samples.pcm16[sample][channel] =
                output[sample][channel];
            interleavedStereo.push_back(output[sample][channel]);
        }
    }

    const auto readBytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(&read), sizeof(read));
    const auto writeBytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(&write), sizeof(write));
    const uint32_t readAddress =
        readIndex == 0U ? kDspRegion0Address : kDspRegion1Address;
    const uint32_t writeAddress =
        readIndex == 0U ? kDspRegion1Address : kDspRegion0Address;
    if (!memory.Write(readAddress, readBytes) ||
        !memory.Write(writeAddress, writeBytes)) {
        SetError(error, "native DSP shared memory is not writable");
        return false;
    }
    return true;
}

nlohmann::json NativeA32DspHle::CaptureState() const {
    nlohmann::json regions = nlohmann::json::array();
    for (const auto& region : mImpl->PhysicalRegions) {
        regions.push_back({
            {"physical_address", region.PhysicalAddress},
            {"virtual_address", region.VirtualAddress},
            {"size", region.Size},
        });
    }
    nlohmann::json sources = nlohmann::json::array();
    for (const auto& source : mImpl->Sources) {
        sources.push_back(source.CaptureState());
    }
    return {
        {"schema", "oot3d_native_dsp_hle_v1"},
        {"physical_regions", std::move(regions)},
        {"sources", std::move(sources)},
        {"mixers", mImpl->Mixers.CaptureState()},
    };
}

bool NativeA32DspHle::RestoreState(const nlohmann::json& state,
                                   std::string* error) {
    try {
        if (!state.is_object() ||
            state.value("schema", std::string{}) !=
                "oot3d_native_dsp_hle_v1") {
            SetError(error, "invalid native DSP HLE state schema");
            return false;
        }
        const auto& regions = state.at("physical_regions");
        const auto& sources = state.at("sources");
        if (!regions.is_array() ||
            regions.size() != mImpl->PhysicalRegions.size() ||
            !sources.is_array() ||
            sources.size() != AudioCore::HLE::num_sources) {
            SetError(error, "native DSP HLE state layout mismatch");
            return false;
        }
        for (size_t index = 0; index < regions.size(); ++index) {
            const auto& encoded = regions[index];
            const auto& expected = mImpl->PhysicalRegions[index];
            if (encoded.at("physical_address").get<uint32_t>() !=
                    expected.PhysicalAddress ||
                encoded.at("virtual_address").get<uint32_t>() !=
                    expected.VirtualAddress ||
                encoded.at("size").get<size_t>() != expected.Size) {
                SetError(error,
                         "native DSP HLE physical-memory layout mismatch");
                return false;
            }
        }

        std::vector<AudioCore::HLE::Source> restoredSources;
        restoredSources.reserve(AudioCore::HLE::num_sources);
        for (size_t index = 0; index < AudioCore::HLE::num_sources; ++index) {
            restoredSources.emplace_back(index);
            restoredSources.back().SetMemoryResolver([impl = mImpl.get()](
                                                          uint32_t address) {
                return impl->CurrentMemory != nullptr &&
                               impl->CurrentMemory->ResolvePhysical
                           ? impl->CurrentMemory->ResolvePhysical(address)
                           : nullptr;
            });
            if (!restoredSources.back().RestoreState(sources[index], error)) {
                return false;
            }
        }
        AudioCore::HLE::Mixers restoredMixers;
        if (!restoredMixers.RestoreState(state.at("mixers"), error)) {
            return false;
        }

        mImpl->Sources.swap(restoredSources);
        mImpl->Mixers = std::move(restoredMixers);
        mImpl->CurrentMemory = nullptr;
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = std::string("invalid native DSP HLE state: ") +
                     exception.what();
        }
        return false;
    }
}

} // namespace Oot3dNativeGame
