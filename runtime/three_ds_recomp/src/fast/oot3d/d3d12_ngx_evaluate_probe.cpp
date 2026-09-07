#include "d3d12_ngx_evaluate_probe.h"

#if defined(_WIN32) && defined(ENABLE_OOT3D_VULKAN)

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <iomanip>
#include <sstream>

namespace Fast::Oot3d {
namespace {

using Microsoft::WRL::ComPtr;

constexpr uint32_t kOutputWidth = 1280U;
constexpr uint32_t kOutputHeight = 720U;
constexpr DWORD kCompletionTimeoutMilliseconds = 10000U;

std::string FormatHresult(HRESULT value) {
    std::ostringstream text;
    text << "HRESULT 0x" << std::hex << std::setw(8)
         << std::setfill('0') << static_cast<uint32_t>(value);
    return text.str();
}

struct ProbeTexture {
    nri::Texture* Texture = nullptr;
    nri::Descriptor* Descriptor = nullptr;
    bool Storage = false;
};

void DestroyProbeTexture(const nri::CoreInterface& core,
                         ProbeTexture& texture) {
    if (texture.Descriptor != nullptr)
        core.DestroyDescriptor(texture.Descriptor);
    if (texture.Texture != nullptr)
        core.DestroyTexture(texture.Texture);
    texture = {};
}

bool CreateProbeTexture(
    nri::Device& device, const nri::CoreInterface& core,
    uint32_t width, uint32_t height, nri::Format format, bool storage,
    ProbeTexture& output, std::string& error) {
    nri::TextureDesc textureDesc{};
    textureDesc.type = nri::TextureType::TEXTURE_2D;
    textureDesc.usage = storage
        ? nri::TextureUsageBits::SHADER_RESOURCE_STORAGE
        : nri::TextureUsageBits::SHADER_RESOURCE;
    textureDesc.format = format;
    textureDesc.width = static_cast<nri::Dim_t>(width);
    textureDesc.height = static_cast<nri::Dim_t>(height);
    textureDesc.depth = 1;
    textureDesc.mipNum = 1;
    textureDesc.layerNum = 1;
    textureDesc.sampleNum = 1;
    if (core.CreateCommittedTexture(
            device, nri::MemoryLocation::DEVICE, 0.0F,
            textureDesc, output.Texture) != nri::Result::SUCCESS) {
        error = "NRI failed to create a D3D12 contract texture";
        return false;
    }

    nri::TextureViewDesc view{};
    view.texture = output.Texture;
    view.type = storage ? nri::TextureView::STORAGE_TEXTURE
                        : nri::TextureView::TEXTURE;
    view.format = format;
    view.mipNum = 1;
    view.layerNum = 1;
    if (core.CreateTextureView(view, output.Descriptor) !=
        nri::Result::SUCCESS) {
        error = "NRI failed to create a D3D12 contract texture view";
        DestroyProbeTexture(core, output);
        return false;
    }
    output.Storage = storage;
    return true;
}

bool SubmitAndWait(
    ID3D12Device& device, ID3D12CommandQueue& queue,
    ID3D12GraphicsCommandList& commandList, std::string& error) {
    ID3D12CommandList* commandLists[] = {&commandList};
    queue.ExecuteCommandLists(1U, commandLists);

    ComPtr<ID3D12Fence> fence;
    HRESULT hr = device.CreateFence(
        0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) {
        error = "D3D12 evaluate fence creation failed: " +
                FormatHresult(hr);
        return false;
    }
    constexpr uint64_t kFenceValue = 1U;
    hr = queue.Signal(fence.Get(), kFenceValue);
    if (FAILED(hr)) {
        error = "D3D12 evaluate fence signal failed: " +
                FormatHresult(hr);
        return false;
    }

    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) {
        error = "CreateEventW failed for the D3D12 evaluate fence";
        return false;
    }
    hr = fence->SetEventOnCompletion(kFenceValue, event);
    const DWORD wait = SUCCEEDED(hr)
        ? WaitForSingleObject(event, kCompletionTimeoutMilliseconds)
        : WAIT_FAILED;
    CloseHandle(event);
    if (FAILED(hr)) {
        error = "D3D12 evaluate fence wait setup failed: " +
                FormatHresult(hr);
        return false;
    }
    if (wait != WAIT_OBJECT_0) {
        error = "D3D12 NGX evaluate did not complete within 10 seconds";
        return false;
    }
    const HRESULT removed = device.GetDeviceRemovedReason();
    if (FAILED(removed)) {
        error = "D3D12 device was removed during NGX evaluate: " +
                FormatHresult(removed);
        return false;
    }
    return true;
}

} // namespace

bool RunD3d12NgxEvaluateProbe(
    ID3D12Device& device, ID3D12CommandQueue& queue,
    nri::Device& nriDevice, const nri::CoreInterface& core,
    const nri::UpscalerInterface& upscalerInterface,
    nri::Upscaler*& upscaler,
    D3d12NgxEvaluateProbeResult& result) {
    result = {};
    if (upscaler != nullptr) {
        result.Detail = "D3D12 NGX evaluate probe received a live feature";
        return false;
    }

    nri::Queue* nriQueue = nullptr;
    nri::CommandAllocator* allocator = nullptr;
    nri::CommandBuffer* commandBuffer = nullptr;
    std::array<ProbeTexture, 5> textures{};
    auto cleanup = [&]() {
        for (auto& texture : textures)
            DestroyProbeTexture(core, texture);
        if (commandBuffer != nullptr)
            core.DestroyCommandBuffer(commandBuffer);
        if (allocator != nullptr)
            core.DestroyCommandAllocator(allocator);
    };

    if (core.GetQueue(nriDevice, nri::QueueType::GRAPHICS, 0,
                      nriQueue) != nri::Result::SUCCESS ||
        nriQueue == nullptr ||
        core.CreateCommandAllocator(*nriQueue, allocator) !=
            nri::Result::SUCCESS ||
        core.CreateCommandBuffer(*allocator, commandBuffer) !=
            nri::Result::SUCCESS ||
        core.BeginCommandBuffer(*commandBuffer, nullptr) !=
            nri::Result::SUCCESS) {
        result.Detail = "NRI failed to open the D3D12 evaluate command buffer";
        cleanup();
        return false;
    }

    nri::UpscalerDesc upscalerDesc{};
    upscalerDesc.upscaleResolution = {
        static_cast<nri::Dim_t>(kOutputWidth),
        static_cast<nri::Dim_t>(kOutputHeight)};
    upscalerDesc.type = nri::UpscalerType::DLSR;
    upscalerDesc.mode = nri::UpscalerMode::QUALITY;
    upscalerDesc.flags = nri::UpscalerBits::HDR |
                         nri::UpscalerBits::USE_REACTIVE |
                         nri::UpscalerBits::MV_JITTERED;
    upscalerDesc.commandBuffer = commandBuffer;
    if (upscalerInterface.CreateUpscaler(
            nriDevice, upscalerDesc, upscaler) !=
        nri::Result::SUCCESS || upscaler == nullptr) {
        result.Detail = "Official NGX D3D12 DLSR feature creation failed";
        core.EndCommandBuffer(*commandBuffer);
        cleanup();
        upscaler = nullptr;
        return false;
    }

    nri::UpscalerProps properties{};
    upscalerInterface.GetUpscalerProps(*upscaler, properties);
    result.InputWidth = properties.renderResolution.w;
    result.InputHeight = properties.renderResolution.h;
    result.OutputWidth = properties.upscaleResolution.w;
    result.OutputHeight = properties.upscaleResolution.h;
    if (result.InputWidth == 0U || result.InputHeight == 0U ||
        result.OutputWidth == 0U || result.OutputHeight == 0U) {
        result.Detail = "NGX returned an invalid DLSR resolution contract";
        core.EndCommandBuffer(*commandBuffer);
        cleanup();
        upscalerInterface.DestroyUpscaler(upscaler);
        upscaler = nullptr;
        return false;
    }

    std::string resourceError;
    const bool resourcesReady =
        CreateProbeTexture(nriDevice, core, result.InputWidth,
                           result.InputHeight, nri::Format::RGBA16_SFLOAT,
                           false, textures[0], resourceError) &&
        CreateProbeTexture(nriDevice, core, result.InputWidth,
                           result.InputHeight, nri::Format::R32_SFLOAT,
                           false, textures[1], resourceError) &&
        CreateProbeTexture(nriDevice, core, result.InputWidth,
                           result.InputHeight, nri::Format::RG16_SFLOAT,
                           false, textures[2], resourceError) &&
        CreateProbeTexture(nriDevice, core, result.InputWidth,
                           result.InputHeight, nri::Format::R8_UNORM,
                           false, textures[3], resourceError) &&
        CreateProbeTexture(nriDevice, core, result.OutputWidth,
                           result.OutputHeight, nri::Format::RGBA16_SFLOAT,
                           true, textures[4], resourceError);
    if (!resourcesReady) {
        result.Detail = resourceError;
        core.EndCommandBuffer(*commandBuffer);
        cleanup();
        upscalerInterface.DestroyUpscaler(upscaler);
        upscaler = nullptr;
        return false;
    }
    result.ContractResourcesReady = true;

    std::array<nri::TextureBarrierDesc, 5> transitions{};
    for (size_t index = 0; index < textures.size(); ++index) {
        auto& transition = transitions[index];
        transition.texture = textures[index].Texture;
        transition.before.access = nri::AccessBits::NONE;
        transition.before.layout = nri::Layout::GENERAL;
        transition.before.stages = nri::StageBits::NONE;
        transition.after.access = textures[index].Storage
            ? nri::AccessBits::SHADER_RESOURCE_STORAGE
            : nri::AccessBits::SHADER_RESOURCE;
        transition.after.layout = textures[index].Storage
            ? nri::Layout::SHADER_RESOURCE_STORAGE
            : nri::Layout::SHADER_RESOURCE;
        transition.after.stages = nri::StageBits::COMPUTE_SHADER;
        transition.mipNum = 1;
        transition.layerNum = 1;
        transition.planes = nri::PlaneBits::COLOR;
    }
    nri::BarrierDesc barrier{};
    barrier.textures = transitions.data();
    barrier.textureNum = static_cast<uint32_t>(transitions.size());
    core.CmdBarrier(*commandBuffer, barrier);

    nri::DispatchUpscaleDesc dispatch{};
    dispatch.input = {textures[0].Texture, textures[0].Descriptor};
    dispatch.guides.upscaler.depth = {
        textures[1].Texture, textures[1].Descriptor};
    dispatch.guides.upscaler.mv = {
        textures[2].Texture, textures[2].Descriptor};
    dispatch.guides.upscaler.reactive = {
        textures[3].Texture, textures[3].Descriptor};
    dispatch.output = {textures[4].Texture, textures[4].Descriptor};
    dispatch.currentResolution = {
        static_cast<nri::Dim_t>(result.InputWidth),
        static_cast<nri::Dim_t>(result.InputHeight)};
    dispatch.cameraJitter = {0.0F, 0.0F};
    dispatch.mvScale = {
        static_cast<float>(result.InputWidth),
        static_cast<float>(result.InputHeight)};
    dispatch.flags = nri::DispatchUpscaleBits::RESET_HISTORY;
    upscalerInterface.CmdDispatchUpscale(
        *commandBuffer, *upscaler, dispatch);
    result.DispatchRecorded = true;

    if (core.EndCommandBuffer(*commandBuffer) != nri::Result::SUCCESS) {
        result.Detail = "NRI failed to close the D3D12 evaluate command buffer";
        cleanup();
        upscalerInterface.DestroyUpscaler(upscaler);
        upscaler = nullptr;
        return false;
    }
    auto* nativeCommandList = static_cast<ID3D12GraphicsCommandList*>(
        core.GetCommandBufferNativeObject(commandBuffer));
    if (nativeCommandList == nullptr ||
        !SubmitAndWait(device, queue, *nativeCommandList, result.Detail)) {
        cleanup();
        upscalerInterface.DestroyUpscaler(upscaler);
        upscaler = nullptr;
        return false;
    }

    result.DispatchCompleted = true;
    result.Detail = "standard D3D12 NGX DLSR evaluate completed";
    cleanup();
    return true;
}

} // namespace Fast::Oot3d

#endif
