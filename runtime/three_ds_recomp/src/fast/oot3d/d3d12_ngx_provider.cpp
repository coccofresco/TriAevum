#include "fast/oot3d/d3d12_ngx_provider.h"
#include "d3d12_ngx_evaluate_probe.h"
#include "d3d12_ngx_frame_bridge.h"

#if defined(_WIN32) && defined(ENABLE_OOT3D_VULKAN)

#include <NRI.h>
#include <Extensions/NRIUpscaler.h>
#include <Extensions/NRIWrapperD3D12.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#ifdef ERROR
#undef ERROR
#endif

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string_view>

namespace Fast::Oot3d {
namespace {

using Microsoft::WRL::ComPtr;

struct NriMessageCapture {
    std::atomic<uint64_t> ErrorCount{ 0U };
    std::mutex Mutex;
    std::string LastError;

    void Reset() {
        ErrorCount.store(0U, std::memory_order_relaxed);
        std::scoped_lock lock(Mutex);
        LastError.clear();
    }

    [[nodiscard]] std::string Error() {
        std::scoped_lock lock(Mutex);
        return LastError;
    }
};

void NRI_CALL CaptureNriMessage(nri::Message messageType, const char* file, uint32_t line, const char* message,
                                void* userArg) {
    if (messageType != nri::Message::ERROR || userArg == nullptr)
        return;
    auto& capture = *static_cast<NriMessageCapture*>(userArg);
    capture.ErrorCount.fetch_add(1U, std::memory_order_relaxed);
    std::ostringstream detail;
    detail << (file != nullptr ? file : "NRI") << ':' << line << ": "
           << (message != nullptr ? message : "unspecified NRI error");
    std::scoped_lock lock(capture.Mutex);
    capture.LastError = detail.str();
}

std::string FormatHresult(HRESULT result) {
    std::ostringstream text;
    text << "HRESULT 0x" << std::hex << std::setw(8) << std::setfill('0') << static_cast<uint32_t>(result);
    return text.str();
}

std::string ToUtf8(const wchar_t* text) {
    if (text == nullptr || *text == L'\0')
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

bool MatchesVulkanLuid(const LUID& dxgiLuid, const uint8_t vulkanLuid[VK_LUID_SIZE]) {
    static_assert(sizeof(LUID) == VK_LUID_SIZE);
    return std::memcmp(&dxgiLuid, vulkanLuid, VK_LUID_SIZE) == 0;
}

} // namespace

struct D3d12NgxProvider::Impl {
    D3d12NgxProviderStatus Status{};
    ComPtr<IDXGIFactory1> Factory;
    ComPtr<IDXGIAdapter1> Adapter;
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> Queue;
    nri::Device* NriDevice = nullptr;
    nri::CoreInterface Core{};
    nri::UpscalerInterface UpscalerInterface{};
    nri::Upscaler* DlssProbe = nullptr;
    NriMessageCapture NriMessages;
    D3d12NgxEvaluateProbeResult EvaluateProbe;
    D3d12NgxFrameBridge FrameBridge;
};

D3d12NgxProvider::D3d12NgxProvider() : mImpl(std::make_unique<Impl>()) {
    mImpl->Status.Detail = "D3D12 NGX provider is not initialized";
}

D3d12NgxProvider::~D3d12NgxProvider() {
    Shutdown();
}

bool D3d12NgxProvider::Initialize(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t graphicsQueueFamily,
                                  uint32_t frameSlotCount, bool externalInteropExtensionsEnabled) {
    Shutdown();
    if (const char* enabled = std::getenv("OOT3D_NGX_D3D12_PROVIDER");
        enabled != nullptr && std::string_view(enabled) == "0") {
        mImpl->Status.Detail = "D3D12 NGX provider disabled by OOT3D_NGX_D3D12_PROVIDER";
        return false;
    }
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
        mImpl->Status.Detail = "Vulkan device is unavailable";
        return false;
    }

    VkPhysicalDeviceIDProperties id{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
    VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    properties.pNext = &id;
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
    if (id.deviceLUIDValid != VK_TRUE) {
        mImpl->Status.Detail = "Vulkan adapter does not expose a valid Windows LUID";
        return false;
    }

    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&mImpl->Factory));
    if (FAILED(hr)) {
        mImpl->Status.Detail = "CreateDXGIFactory1 failed: " + FormatHresult(hr);
        return false;
    }

    const auto considerAdapter = [&](IDXGIAdapter1* adapter) {
        DXGI_ADAPTER_DESC1 desc{};
        if (adapter == nullptr || FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
            !MatchesVulkanLuid(desc.AdapterLuid, id.deviceLUID)) {
            return false;
        }
        mImpl->Adapter = adapter;
        mImpl->Status.AdapterMatched = true;
        mImpl->Status.VendorId = desc.VendorId;
        mImpl->Status.DeviceId = desc.DeviceId;
        mImpl->Status.AdapterName = ToUtf8(desc.Description);
        return true;
    };

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(mImpl->Factory.As(&factory6))) {
        for (uint32_t index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            hr = factory6->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                      IID_PPV_ARGS(&adapter));
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(hr))
                continue;
            if (considerAdapter(adapter.Get()))
                break;
        }
    }
    if (!mImpl->Adapter) {
        for (uint32_t index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            hr = mImpl->Factory->EnumAdapters1(index, &adapter);
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(hr))
                continue;
            if (considerAdapter(adapter.Get()))
                break;
        }
    }
    if (!mImpl->Adapter) {
        mImpl->Status.Detail = "No DXGI adapter matches the Vulkan device LUID";
        return false;
    }

    hr = D3D12CreateDevice(mImpl->Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mImpl->Device));
    if (FAILED(hr)) {
        mImpl->Status.Detail = "D3D12CreateDevice failed for the Vulkan adapter: " + FormatHresult(hr);
        return false;
    }
    mImpl->Status.DeviceReady = true;

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    hr = mImpl->Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mImpl->Queue));
    if (FAILED(hr)) {
        mImpl->Status.Detail = "D3D12 direct command queue creation failed: " + FormatHresult(hr);
        return false;
    }

    ID3D12CommandQueue* nativeQueues[] = { mImpl->Queue.Get() };
    nri::QueueFamilyD3D12Desc queueFamily{};
    queueFamily.d3d12Queues = nativeQueues;
    queueFamily.queueNum = 1;
    queueFamily.queueType = nri::QueueType::GRAPHICS;
    nri::DeviceCreationD3D12Desc deviceDesc{};
    deviceDesc.d3d12Device = mImpl->Device.Get();
    deviceDesc.queueFamilies = &queueFamily;
    deviceDesc.queueFamilyNum = 1;
    deviceDesc.callbackInterface.MessageCallback = &CaptureNriMessage;
    deviceDesc.callbackInterface.userArg = &mImpl->NriMessages;
    deviceDesc.enableMemoryZeroInitialization = true;
    // The bridge will explicitly own cross-API transitions. Legacy barriers
    // avoid mixing an enhanced-barrier state model into that contract.
    deviceDesc.disableD3D12EnhancedBarriers = true;
    deviceDesc.disableNVAPIInitialization = true;
    if (nriCreateDeviceFromD3D12Device(deviceDesc, mImpl->NriDevice) != nri::Result::SUCCESS) {
        mImpl->Status.Detail = "nriCreateDeviceFromD3D12Device failed";
        return false;
    }
    mImpl->Status.NriReady = true;

    if (nriGetInterface(*mImpl->NriDevice, "CoreInterface", sizeof(mImpl->Core), &mImpl->Core) !=
            nri::Result::SUCCESS ||
        nriGetInterface(*mImpl->NriDevice, "UpscalerInterface", sizeof(mImpl->UpscalerInterface),
                        &mImpl->UpscalerInterface) != nri::Result::SUCCESS ||
        !mImpl->UpscalerInterface.IsUpscalerSupported(*mImpl->NriDevice, nri::UpscalerType::DLSR)) {
        mImpl->Status.Detail = "NRI D3D12 device does not expose an NGX DLSR provider";
        return false;
    }

    const uint64_t errorsBefore = mImpl->NriMessages.ErrorCount.load(std::memory_order_relaxed);
    const bool evaluateReady =
        RunD3d12NgxEvaluateProbe(*mImpl->Device.Get(), *mImpl->Queue.Get(), *mImpl->NriDevice, mImpl->Core,
                                 mImpl->UpscalerInterface, mImpl->DlssProbe, mImpl->EvaluateProbe);
    mImpl->Status.DlssContractResourcesReady = mImpl->EvaluateProbe.ContractResourcesReady;
    mImpl->Status.DlssFeatureReady = mImpl->DlssProbe != nullptr;
    mImpl->Status.ProbeInputWidth = mImpl->EvaluateProbe.InputWidth;
    mImpl->Status.ProbeInputHeight = mImpl->EvaluateProbe.InputHeight;
    mImpl->Status.ProbeOutputWidth = mImpl->EvaluateProbe.OutputWidth;
    mImpl->Status.ProbeOutputHeight = mImpl->EvaluateProbe.OutputHeight;
    const bool evaluateReportedError = mImpl->NriMessages.ErrorCount.load(std::memory_order_relaxed) > errorsBefore;
    mImpl->Status.DlssEvaluateReady = evaluateReady && !evaluateReportedError;
    if (!mImpl->Status.DlssEvaluateReady) {
        const std::string nriError = evaluateReportedError ? mImpl->NriMessages.Error() : std::string{};
        mImpl->Status.Detail =
            "standard D3D12 NGX evaluate failed: " + (!nriError.empty() ? nriError : mImpl->EvaluateProbe.Detail);
        if (mImpl->DlssProbe != nullptr) {
            mImpl->UpscalerInterface.DestroyUpscaler(mImpl->DlssProbe);
            mImpl->DlssProbe = nullptr;
            mImpl->Status.DlssFeatureReady = false;
        }
        return false;
    }
    if (!externalInteropExtensionsEnabled) {
        mImpl->Status.Detail = "standard D3D12 NGX evaluate is available, but Vulkan external "
                               "memory/fence interop is unavailable";
        return false;
    }

    const D3d12NgxFrameContract initialContract{
        mImpl->EvaluateProbe.InputWidth,
        mImpl->EvaluateProbe.InputHeight,
        mImpl->EvaluateProbe.OutputWidth,
        mImpl->EvaluateProbe.OutputHeight,
        frameSlotCount,
        UpscalerQuality::Quality,
        false,
    };
    const bool interopReady = mImpl->FrameBridge.Initialize(
        physicalDevice, device, graphicsQueueFamily, *mImpl->Device.Get(), *mImpl->Queue.Get(), *mImpl->NriDevice,
        mImpl->Core, mImpl->UpscalerInterface, initialContract);
    mImpl->Status.ExternalMemoryReady = mImpl->FrameBridge.ExternalMemoryReady();
    mImpl->Status.SharedFenceReady = mImpl->FrameBridge.SharedFenceReady();
    mImpl->Status.ZeroCopyInteropReady = mImpl->Status.ExternalMemoryReady && mImpl->Status.SharedFenceReady;
    mImpl->Status.FrameBridgeReady = mImpl->FrameBridge.Available();
    mImpl->Status.FrameDispatchRequested = FrameDispatchRequested();
    if (!interopReady || !mImpl->Status.ZeroCopyInteropReady) {
        mImpl->Status.Detail = "standard D3D12 NGX evaluate is available, but zero-copy "
                               "Vulkan/D3D12 frame interop failed: " +
                               mImpl->FrameBridge.UnavailableReason();
        return false;
    }
    mImpl->Status.Detail = "standard D3D12 NGX DLSR evaluate (color/depth/motion/reactive) "
                           "and persistent bidirectional Vulkan/D3D12 frame interop are ready";
    return true;
}

void D3d12NgxProvider::Shutdown() {
    if (!mImpl)
        return;
    mImpl->FrameBridge.Shutdown();
    if (mImpl->DlssProbe != nullptr && mImpl->UpscalerInterface.DestroyUpscaler != nullptr) {
        mImpl->UpscalerInterface.DestroyUpscaler(mImpl->DlssProbe);
    }
    mImpl->DlssProbe = nullptr;
    if (mImpl->NriDevice != nullptr)
        nriDestroyDevice(mImpl->NriDevice);
    mImpl->NriDevice = nullptr;
    mImpl->Core = {};
    mImpl->UpscalerInterface = {};
    mImpl->NriMessages.Reset();
    mImpl->EvaluateProbe = {};
    mImpl->Queue.Reset();
    mImpl->Device.Reset();
    mImpl->Adapter.Reset();
    mImpl->Factory.Reset();
    mImpl->Status = {};
    mImpl->Status.Detail = "D3D12 NGX provider is not initialized";
}

bool D3d12NgxProvider::Available() const {
    return mImpl && mImpl->Status.DlssFeatureReady && mImpl->Status.DlssEvaluateReady &&
           mImpl->Status.ZeroCopyInteropReady && mImpl->FrameBridge.Available();
}

bool D3d12NgxProvider::FrameDispatchRequested() const {
    const char* value = std::getenv("OOT3D_NGX_D3D12_PER_FRAME");
    return value != nullptr &&
           (std::string_view(value) == "1" || std::string_view(value) == "d3d12" || std::string_view(value) == "D3D12");
}

bool D3d12NgxProvider::ConfiguredForFrameContract(const D3d12NgxFrameContract& contract) const {
    return mImpl && mImpl->FrameBridge.ConfiguredFor(contract);
}

bool D3d12NgxProvider::ConfigureFrameContract(const D3d12NgxFrameContract& contract) {
    if (!mImpl)
        return false;
    // The startup feature exists only to prove the official contract before
    // swapchain creation. The real feature must be created lazily so external
    // post-swapchain NGX hooks observe both CreateFeature and EvaluateFeature.
    if (mImpl->DlssProbe != nullptr) {
        mImpl->UpscalerInterface.DestroyUpscaler(mImpl->DlssProbe);
        mImpl->DlssProbe = nullptr;
    }
    const bool configured = mImpl->FrameBridge.Configure(contract);
    mImpl->Status.FrameBridgeReady = configured;
    mImpl->Status.FrameDispatchRequested = FrameDispatchRequested();
    if (!configured) {
        mImpl->Status.Detail = mImpl->FrameBridge.UnavailableReason();
    }
    return configured;
}

bool D3d12NgxProvider::PrepareFrame(VkCommandBuffer vulkanCommandBuffer, uint32_t frameSlot,
                                    const D3d12NgxFrameInputs& inputs, D3d12NgxFrameSynchronization& synchronization) {
    return mImpl && mImpl->FrameBridge.PrepareFrame(vulkanCommandBuffer, frameSlot, inputs, synchronization);
}

bool D3d12NgxProvider::QueuePreparedFrame(const D3d12NgxFrameSynchronization& synchronization) {
    if (!mImpl)
        return false;
    const bool queued = mImpl->FrameBridge.QueuePreparedFrame(synchronization);
    if (queued) {
        ++mImpl->Status.FrameDispatchCount;
    } else {
        mImpl->Status.Detail = mImpl->FrameBridge.UnavailableReason();
    }
    return queued;
}

bool D3d12NgxProvider::RecordOutputAcquire(VkCommandBuffer vulkanCommandBuffer,
                                           const D3d12NgxFrameSynchronization& synchronization) {
    return mImpl && mImpl->FrameBridge.RecordOutputAcquire(vulkanCommandBuffer, synchronization);
}

VkImage D3d12NgxProvider::FrameOutputImage(uint32_t frameSlot) const {
    return mImpl ? mImpl->FrameBridge.OutputImage(frameSlot) : VK_NULL_HANDLE;
}

VkImageView D3d12NgxProvider::FrameOutputView(uint32_t frameSlot) const {
    return mImpl ? mImpl->FrameBridge.OutputView(frameSlot) : VK_NULL_HANDLE;
}

const D3d12NgxProviderStatus& D3d12NgxProvider::Status() const {
    return mImpl->Status;
}

const std::string& D3d12NgxProvider::UnavailableReason() const {
    return mImpl->Status.Detail;
}

} // namespace Fast::Oot3d

#endif
