#include "fast/oot3d/nri_pica_pipeline_bridge.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"

#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif

#include <cstdlib>
#include <string_view>

namespace Fast::Oot3d {

class NriPicaPipelineBridge::InteropAdapter final
    : public Renderer3ds::NriPicaInterop {
  public:
    explicit InteropAdapter(NriInteropContext& context)
        : mContext(context) {}

    [[nodiscard]] bool Available() const override {
        return mContext.Available();
    }
    [[nodiscard]] const std::string& UnavailableReason() const override {
        return mContext.UnavailableReason();
    }

#ifdef ENABLE_OOT3D_NRI
    bool WrapBuffer(VkBuffer buffer, uint64_t size,
                    uint8_t* mappedMemory) override {
        return mContext.WrapBuffer(buffer, size, mappedMemory);
    }
    bool WrapTexture(VkImage image, VkFormat format,
                     VkImageType type, VkImageUsageFlags usage,
                     uint32_t width, uint32_t height,
                     uint32_t mipLevels) override {
        return mContext.WrapTexture(image, format, type, usage, width,
                                    height, mipLevels);
    }
    [[nodiscard]] nri::Device* Device() override {
        return NriInteropAccess::Device(mContext);
    }
    [[nodiscard]] nri::CoreInterface* Core() override {
        return NriInteropAccess::Core(mContext);
    }
    [[nodiscard]] nri::CommandBuffer* CommandBuffer(
        uint32_t frameIndex) override {
        return NriInteropAccess::CommandBuffer(mContext, frameIndex);
    }
    [[nodiscard]] nri::Descriptor* TextureView(
        VkImage image, bool storage) override {
        return NriInteropAccess::TextureView(mContext, image, storage);
    }
    [[nodiscard]] nri::Buffer* Buffer(VkBuffer buffer) override {
        return NriInteropAccess::Buffer(mContext, buffer);
    }
    [[nodiscard]] nri::Pipeline* WrapGraphicsPipeline(
        VkPipeline pipeline) override {
        return NriInteropAccess::WrapGraphicsPipeline(mContext, pipeline);
    }
    void DestroyPipelineWrapper(nri::Pipeline* pipeline) override {
        NriInteropAccess::DestroyPipelineWrapper(mContext, pipeline);
    }
    bool CmdSetPipeline(uint32_t frameIndex,
                        nri::Pipeline* pipeline) override {
        return NriInteropAccess::CmdSetPipeline(
            mContext, frameIndex, pipeline);
    }
#endif

  private:
    NriInteropContext& mContext;
};

NriPicaPipelineBridge::NriPicaPipelineBridge() = default;
NriPicaPipelineBridge::~NriPicaPipelineBridge() { Shutdown(); }

bool NriPicaPipelineBridge::Initialize(NriInteropContext& interop) {
    Shutdown();
    mInteropAdapter = std::make_unique<InteropAdapter>(interop);
    const char* ownedDraws =
        std::getenv("OOT3D_GRAPHICS_NRI_PICA_DRAWS");
    const char* ownedUploads =
        std::getenv("OOT3D_GRAPHICS_NRI_PICA_UPLOADS");
    const Renderer3ds::NriPicaExecutionConfig config{
        2U,
        512U,
        ownedDraws == nullptr || std::string_view(ownedDraws) != "0",
        ownedUploads != nullptr && std::string_view(ownedUploads) == "1",
    };
    if (!mBridge.Initialize(*mInteropAdapter, config)) {
        mInteropAdapter.reset();
        return false;
    }
    return true;
}

bool NriPicaPipelineBridge::InitializePipelineCache(std::span<const uint8_t> data) {
    return mBridge.InitializePipelineCache(data);
}
std::vector<uint8_t> NriPicaPipelineBridge::GetPipelineCacheData() const {
    return mBridge.GetPipelineCacheData();
}
Renderer3ds::NriPicaPipelineStatistics NriPicaPipelineBridge::PipelineStatistics() const {
    return mBridge.PipelineStatistics();
}
bool NriPicaPipelineBridge::CreateOwnedPipeline(
    VkPipeline fallbackPipeline,
    const NriPicaGraphicsPipelineDesc& desc) {
    return mBridge.CreateOwnedPipeline(fallbackPipeline, desc);
}
bool NriPicaPipelineBridge::BindOwnedDraw(
    const NriPicaOwnedDrawDesc& desc) {
    return mBridge.BindOwnedDraw(desc);
}
bool NriPicaPipelineBridge::DrawBoundGeometry(const NriPicaOwnedDrawDesc& desc) {
    return mBridge.DrawBoundGeometry(desc);
}
bool NriPicaPipelineBridge::Bind(
    uint32_t frameIndex, VkPipeline pipeline) {
    return mBridge.Bind(frameIndex, pipeline);
}
void NriPicaPipelineBridge::Forget(VkPipeline pipeline) {
    mBridge.Forget(pipeline);
}
void NriPicaPipelineBridge::Reset() { mBridge.Reset(); }
void NriPicaPipelineBridge::Shutdown() {
    mBridge.Shutdown();
    mInteropAdapter.reset();
}
bool NriPicaPipelineBridge::Available() const {
    return mBridge.Available();
}
bool NriPicaPipelineBridge::DescriptorLayoutOwnedByNri() const {
    return mBridge.DescriptorLayoutOwnedByNri();
}
bool NriPicaPipelineBridge::DescriptorsOwnedByNri() const {
    return mBridge.DescriptorsOwnedByNri();
}
bool NriPicaPipelineBridge::OwnedDrawsEnabled() const {
    return mBridge.OwnedDrawsEnabled();
}
bool NriPicaPipelineBridge::LastDrawUploadsOwnedByNri() const {
    return mBridge.LastDrawUploadsOwnedByNri();
}
uint64_t NriPicaPipelineBridge::LastDrawUploadedBytes() const {
    return mBridge.LastDrawUploadedBytes();
}
bool NriPicaPipelineBridge::OwnedPipelineReady(
    VkPipeline fallbackPipeline) const {
    return mBridge.OwnedPipelineReady(fallbackPipeline);
}
size_t NriPicaPipelineBridge::OwnedPipelineCount() const {
    return mBridge.OwnedPipelineCount();
}
size_t NriPicaPipelineBridge::WrappedPipelineCount() const {
    return mBridge.WrappedPipelineCount();
}
const std::string& NriPicaPipelineBridge::UnavailableReason() const {
    return mBridge.UnavailableReason();
}

} // namespace Fast::Oot3d

#endif
