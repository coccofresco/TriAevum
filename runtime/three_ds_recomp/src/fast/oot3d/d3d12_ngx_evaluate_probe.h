#pragma once

#if defined(_WIN32) && defined(ENABLE_OOT3D_VULKAN)

#include <NRI.h>
#include <Extensions/NRIUpscaler.h>

#include <cstdint>
#include <string>

struct ID3D12Device;
struct ID3D12CommandQueue;

namespace Fast::Oot3d {

struct D3d12NgxEvaluateProbeResult {
    bool ContractResourcesReady = false;
    bool DispatchRecorded = false;
    bool DispatchCompleted = false;
    uint32_t InputWidth = 0;
    uint32_t InputHeight = 0;
    uint32_t OutputWidth = 0;
    uint32_t OutputHeight = 0;
    std::string Detail;
};

// Records and executes one standard D3D12 DLSR evaluation. This verifies the
// contract consumed by NGX integrations; it does not use private NR features.
bool RunD3d12NgxEvaluateProbe(
    ID3D12Device& device, ID3D12CommandQueue& queue,
    nri::Device& nriDevice, const nri::CoreInterface& core,
    const nri::UpscalerInterface& upscalerInterface,
    nri::Upscaler*& upscaler,
    D3d12NgxEvaluateProbeResult& result);

} // namespace Fast::Oot3d

#endif
