#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "fast/backends/gfx_rendering_api.h"
#include "fast/renderer/spirv_cache.h"
#include "fast/backends/oot3d_vulkan_diagnostics.h"
#include "fast/backends/oot3d_vulkan_gpu_profiler.h"
#include "fast/backends/oot3d_vulkan_validation.h"
#include "fast/backends/vulkan_scanout_probe.h"
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/cacao_pass.h"
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
#include "fast/oot3d/d3d12_ngx_provider.h"
#endif
#include "fast/oot3d/effect_geometry_provider_plan.h"
#include "fast/oot3d/effect_graph.h"
#include "fast/oot3d/fidelityfx_sssr_pass.h"
#include "fast/oot3d/grass_geometry_registry.h"
#include "fast/oot3d/hiz_depth_pyramid_pass.h"
#include "fast/oot3d/hiz_reflection_pass.h"
#include "fast/oot3d/interactive_grass_pass.h"
#include "fast/oot3d/linear_scene_color_pass.h"
#include "fast/oot3d/motion_vector_pass.h"
#include "fast/oot3d/native_scene_view.h"
#include "fast/oot3d/title_render_backend.h"
#include "fast/oot3d/nri_directional_shadow_pass.h"
#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/nri_pica_display_copy_pass.h"
#include "fast/oot3d/nri_pica_memory_fill_clear_pass.h"
#include "fast/oot3d/nri_pica_scanout_pass.h"
#include "fast/oot3d/nri_pica_pipeline_bridge.h"
#include "fast/oot3d/nri_pica_render_target_init_pass.h"
#include "fast/oot3d/nri_pica_render_target_owner.h"
#include "fast/oot3d/nri_pica_texture_image_owner.h"
#include "fast/oot3d/nri_pica_texture_upload_pass.h"
#include "fast/oot3d/nri_swapchain.h"
#include "fast/oot3d/nri_upscaler_pass.h"
#include "fast/oot3d/pica_dynamic_rendering_scope.h"
#include "fast/oot3d/pica_aot_shader_pack.h"
#include "fast/oot3d/pica_composition_schedule.h"
#include "fast/oot3d/pica_extension_schedule.h"
#include "fast/oot3d/pica_pipeline_manifest.h"
#include "fast/oot3d/pica_scene_publication_adapter.h"
#include "fast/oot3d/reflection_ibl.h"
#include "fast/oot3d/reflection_ibl_pass.h"
#include "fast/oot3d/reflection_material_resolve_pass.h"
#include "fast/oot3d/reflection_provider.h"
#include "fast/oot3d/resource_state_tracker.h"
#include "fast/oot3d/pica_geometry_registry.h"
#include "fast/oot3d/pica_scene_frame.h"
#include "fast/oot3d/pica_shader_pipeline_cache.h"
#include "fast/oot3d/scene_surface_registry.h"
#include "fast/oot3d/scene_composite_pass.h"
#include "fast/oot3d/smaa_1x_pass.h"
#include "fast/oot3d/pica_rigid_motion.h"
#include "fast/oot3d/temporal_history_manager.h"
#include "fast/oot3d/temporal_aa_pass.h"
#include "fast/oot3d/temporal_jitter.h"
#include <vulkan/vulkan.h>
#include <imgui.h>

#include <array>
#include <atomic>
#include <compare>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fast {

class GfxWindowBackendSDL2;

struct VulkanShaderProgram {
    static constexpr uint32_t kAbsentAttribute = UINT32_MAX;

    uint64_t ShaderId0 = 0;
    uint64_t ShaderId1 = 0;
    VkShaderModule VertexShader = VK_NULL_HANDLE;
    VkShaderModule FragmentShader = VK_NULL_HANDLE;
    uint8_t NumInputs = 0;
    bool UsedTextures[2] = {};
    bool UsesTexture2 = false;
    bool HasAlpha = false;
    bool HasFog = false;
    bool HasGrayscale = false;
    int16_t NativeShaderId = 0;
    uint32_t VertexStride = 0;
    std::array<uint32_t, 3> TextureCoordinateOffsets = {
        kAbsentAttribute, kAbsentAttribute, kAbsentAttribute
    };
    uint32_t FogOffset = kAbsentAttribute;
    uint32_t GrayscaleOffset = kAbsentAttribute;
    std::array<uint32_t, 4> InputOffsets = {
        kAbsentAttribute, kAbsentAttribute, kAbsentAttribute, kAbsentAttribute
    };
    uint32_t ShadowCoordinateOffset = kAbsentAttribute;
};

class GfxRenderingAPIVulkan final : public GfxRenderingAPI,
                                    public Oot3d::TitleRenderBackend {
  public:
    explicit GfxRenderingAPIVulkan(GfxWindowBackendSDL2* windowBackend);
    ~GfxRenderingAPIVulkan() override;

    const char* GetName() override;
    int GetMaxTextureSize() override;
    GfxClipParameters GetClipParameters() override;
    void UnloadShader(ShaderProgram* oldPrg) override;
    void LoadShader(ShaderProgram* newPrg) override;
    void ClearShaderCache() override;
    ShaderProgram* CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) override;
    ShaderProgram* LookupShader(uint64_t shaderId0, uint64_t shaderId1) override;
    void ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) override;
    uint32_t NewTexture() override;
    void SelectTexture(int tile, uint32_t textureId) override;
    void UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) override;
    void SetSamplerParameters(int sampler, bool linearFilter, uint32_t cms, uint32_t cmt) override;
    bool UploadTextureMipLevel(uint32_t level, const uint8_t* rgba32Buf, uint32_t width,
                               uint32_t height) override;
    bool SetNativeSamplerParameters(int sampler, const GfxNativeSamplerState& state) override;
    void SetDepthTestAndMask(bool depthTest, bool zUpd) override;
    void SetZmodeDecal(bool decal) override;
    void SetViewport(int x, int y, int width, int height) override;
    void SetScissor(int x, int y, int width, int height) override;
    void SetUseAlpha(bool useAlpha) override;
    bool SetNativeBlendState(const GfxNativeBlendState& state) override;
    bool SetNativeCullMode(GfxNativeCullMode mode) override;
    bool SupportsOot3dPicaFogLut() const override;
    bool SetOot3dPicaFogShaderParameters(const uint32_t* lutWords, size_t lutWordCount,
                                         bool fogFlip, uint64_t stateKey) override;
    bool SetOot3dPicaAlphaTestShaderParameters(bool enabled, uint32_t function,
                                               uint8_t reference) override;
    bool SupportsOot3dPicaTexture2() const override;
    bool PublishPicaCompositionSequence(
        const ::Fast::Renderer3ds::PicaCompositionSequenceView& sequence,
        std::string* error = nullptr) override;
    bool SubmitPicaDraw(const GfxNativePicaDrawView& draw,
                        std::string* error = nullptr) override;
    bool SubmitPicaDisplayTransfer(
        const GfxNativePicaDisplayTransferView& transfer,
        std::string* error = nullptr) override;
    bool ClearPicaRenderTarget(
        uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress,
        std::string* error = nullptr) override;
    bool PrepareOverlay(
        std::string* error = nullptr) override;
    bool SubmitPicaMemoryFill(
        const GfxNativePicaMemoryFillView& fill,
        std::string* error = nullptr) override;
    bool CapturePicaTextureCache(
        std::vector<GfxNativePicaTextureCacheEntrySnapshot>& snapshots,
        std::string* error = nullptr) override;
    bool RestorePicaTextureCache(
        std::span<const GfxNativePicaTextureCacheEntrySnapshot> snapshots,
        std::string* error = nullptr) override;
    bool CapturePicaColorTargets(
        std::vector<GfxNativePicaRenderTargetColorSnapshot>& snapshots,
        std::string* error = nullptr) override;
    bool RestorePicaColorTargets(
        std::span<const GfxNativePicaRenderTargetColorSnapshot> snapshots,
        std::string* error = nullptr) override;
    bool CapturePicaPresentationState(
        GfxNativePicaPresentationStateSnapshot& snapshot,
        std::string* error = nullptr) override;
    bool RestorePicaPresentationState(
        const GfxNativePicaPresentationStateSnapshot& snapshot,
        std::string* error = nullptr) override;
    bool QueuePicaCompletion(
        uint64_t completionId, std::string* error = nullptr) override;
    std::vector<uint64_t> TakePicaCompletions() override;
    bool ResetPicaState(
        std::string* error = nullptr) override;
    bool ApplyPresentationSettings(
        const Oot3d::TitlePresentationSettingsView& settings,
        std::string* error = nullptr) override;
    bool PublishSceneView(
        const Oot3d::TitleSceneViewSubmission& view) override;
    bool ResetTitleState(std::string* error = nullptr) override;
    bool PublishPicaFrameTemporalSample(
        const ::Fast::Renderer3ds::PicaFrameTemporalSample& sample) override;
    bool SetOot3dNativeTransform(const float* rowMajorMatrix) override;
    bool DrawTrianglesCached(uint64_t cacheId, uint64_t contentVersion, const float* bufVbo,
                             size_t bufVboLen, size_t bufVboNumTris) override;
    void DrawTriangles(float bufVbo[], size_t bufVboLen, size_t bufVboNumTris) override;
    void Init() override;
    void Shutdown() override;
    void OnResize() override;
    void StartFrame() override;
    void EndFrame() override;
    void FinishRender() override;
    int CreateFramebuffer() override;
    void UpdateFramebufferParameters(int fbId, uint32_t width, uint32_t height, uint32_t msaaLevel,
                                     bool openglInvertY, bool renderTarget, bool hasDepthBuffer,
                                     bool canExtractDepth) override;
    bool UpdateFramebufferParametersWithColorFormat(
        int fbId, uint32_t width, uint32_t height, uint32_t msaaLevel,
        bool openglInvertY, bool renderTarget, bool hasDepthBuffer,
        bool canExtractDepth, GfxFramebufferColorFormat colorFormat) override;
    bool SupportsOot3dShadow2dR32uiPipeline() const override;
    bool BindOot3dShadow2dTexture(int fbId, uint32_t textureUnit) override;
    bool SetOot3dShadow2dShaderParameters(uint32_t textureBias, bool orthographic,
                                         bool invert) override;
    bool StartOot3dShadow2dDepthEncodePass(int fbId, uint32_t clearValue) override;
    void EndOot3dShadow2dDepthEncodePass() override;
    bool DrawOot3dShadow2dDepthEncodedTriangles(float bufVbo[], size_t bufVboLen,
                                                size_t bufVboNumTris) override;
    void StartDrawToFramebuffer(int fbId, float noiseScale) override;
    void CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1,
                         int dstX0, int dstY0, int dstX1, int dstY1) override;
    void ClearFramebuffer(bool color, bool depth) override;
    void ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) override;
    void ResolveMSAAColorBuffer(int fbIdTarget, int fbIdSrc) override;
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int fbId, const std::set<std::pair<float, float>>& coordinates) override;
    void* GetFramebufferTextureId(int fbId) override;
    void SelectTextureFb(int fbId) override;
    void DeleteTexture(uint32_t texId) override;
    void SetTextureFilter(FilteringMode mode) override;
    FilteringMode GetTextureFilter() override;
    void SetSrgbMode() override;
    ImTextureID GetTextureById(int id) override;
    void SetCurrentPrimDepth(float depth) override;
    bool InitImGuiBackend();
    void ShutdownImGuiBackend();
    void NewImGuiFrame();
    void RenderImGuiDrawData(ImDrawData* drawData);

  private:
    struct QueueFamilies {
        std::optional<uint32_t> Graphics;
        std::optional<uint32_t> Present;
        bool Complete() const;
    };

    struct SwapchainSupport {
        VkSurfaceCapabilitiesKHR Capabilities{};
        std::vector<VkSurfaceFormatKHR> Formats;
        std::vector<VkPresentModeKHR> PresentModes;
    };

    struct TextureRecord {
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t MipLevels = 1;
        uint32_t UploadedMipLevels = 0;
        std::vector<uint8_t> Rgba8;
        VkImage Image = VK_NULL_HANDLE;
        VkImage NriImage = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        VkSampler Sampler = VK_NULL_HANDLE;
        VkImageLayout ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        GfxNativeSamplerState SamplerState{};
        GfxNativeSamplerState AppliedSamplerState{};
        bool SamplerStateApplied = false;
        bool NriOwnedImage = false;
        bool Uploaded = false;
        uint64_t CustomReplacementHash = 0;
        bool CustomReplacementPending = false;
        bool CustomReplacementReady = false;
    };

    struct BufferAllocation {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Size = 0;
        void* Mapped = nullptr;
    };

    struct OffscreenFramebufferRecord {
        uint32_t Width = 0;
        uint32_t Height = 0;
        GfxFramebufferColorFormat ColorFormat = GfxFramebufferColorFormat::Rgba8;
        VkImage ColorImage = VK_NULL_HANDLE;
        VkDeviceMemory ColorMemory = VK_NULL_HANDLE;
        VkImageView ColorView = VK_NULL_HANDLE;
        VkSampler ColorSampler = VK_NULL_HANDLE;
        VkImage DepthImage = VK_NULL_HANDLE;
        VkDeviceMemory DepthMemory = VK_NULL_HANDLE;
        VkImageView DepthView = VK_NULL_HANDLE;
        VkFramebuffer Framebuffer = VK_NULL_HANDLE;
        bool HasDepthBuffer = false;
    };

    static constexpr uint32_t kFramesInFlight = 2;

    struct TextureDescriptorKey {
        std::array<VkSampler, 4> Samplers{};
        std::array<VkImageView, 4> ImageViews{};

        auto operator<=>(const TextureDescriptorKey&) const = default;
    };

    struct FrameResources {
        BufferAllocation VertexBuffer;
        BufferAllocation UniformBuffer;
        VkDeviceSize VertexBytesUsed = 0;
        VkDeviceSize UniformBytesUsed = 0;
        VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
        std::map<TextureDescriptorKey, VkDescriptorSet> TextureDescriptorSets;
        std::vector<uint64_t> NativePicaCompletionIds;
    };

    struct CachedVertexBuffer {
        std::array<BufferAllocation, kFramesInFlight> Buffers{};
        std::array<uint64_t, kFramesInFlight> ContentVersions{};
        size_t FloatCount = 0;
        size_t TriangleCount = 0;
        uint32_t LastUsedFrame = 0;
    };

    struct NativePicaShaderProgram {
        VkShaderModule VertexShader = VK_NULL_HANDLE;
        VkShaderModule FragmentShader = VK_NULL_HANDLE;
        std::vector<uint32_t> NriVertexSpirv;
        std::vector<uint32_t> NriFragmentSpirv;
        Oot3d::PicaAotShaderSourceIdentity VertexSource;
        Oot3d::PicaAotShaderSourceIdentity FragmentSource;
        Oot3d::PicaAotShaderSourceIdentity NriFragmentSource;
        Oot3d::PicaGraphicsPipelineShaderOutputs FragmentOutputs;
        bool NriDescriptorContract = false;
    };

    struct NativePicaGeometryBuffer {
        std::array<BufferAllocation, kFramesInFlight> Buffers{};
        std::array<uint64_t, kFramesInFlight> ContentVersions{};
        std::array<uint64_t, kFramesInFlight> StructuralSignatures{};
        uint64_t LastUsedFrame = 0;
    };

    struct NativePicaTextureKey {
        uint64_t ContentHash = 0;
        uint64_t ReplacementGeneration = 0;
        uint32_t PhysicalAddress = 0;
        uint16_t Width = 0;
        uint16_t Height = 0;
        uint8_t Format = 0;
        uint8_t Type = 0;
        uint8_t WrapS = 0;
        uint8_t WrapT = 0;
        bool MinLinear = false;
        bool MagLinear = false;
        bool MipLinear = false;
        int16_t LodBiasRaw = 0;
        uint8_t MinMipLevel = 0;
        uint8_t MaxMipLevel = 0;
        bool CustomReplacement = false;

        auto operator<=>(const NativePicaTextureKey&) const = default;
    };

    struct NativePicaLightingLutTexture {
        TextureRecord Texture;
        uint64_t LastUsedFrame = 0;
    };

    struct NativePicaRenderTargetKey {
        uint64_t RenderTargetNamespace = 0;
        uint32_t ColorPhysicalAddress = 0;
        uint32_t DepthPhysicalAddress = 0;
        uint16_t Width = 0;
        uint16_t Height = 0;
        uint8_t ColorFormat = 0;
        uint8_t DepthFormat = 0;
        uint16_t RenderScalePermille = 1000;

        auto operator<=>(const NativePicaRenderTargetKey&) const = default;
    };

    struct NativePicaRenderTarget {
        NativePicaRenderTargetKey Key;
        uint32_t Width = 0;
        uint32_t Height = 0;
        Oot3d::PicaAttachmentRequirements Attachments;
        VkImage ColorImage = VK_NULL_HANDLE;
        VkDeviceMemory ColorMemory = VK_NULL_HANDLE;
        VkImageView ColorView = VK_NULL_HANDLE;
        VkImage NormalGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory NormalGuideMemory = VK_NULL_HANDLE;
        VkImageView NormalGuideView = VK_NULL_HANDLE;
        VkImage MaterialGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory MaterialGuideMemory = VK_NULL_HANDLE;
        VkImageView MaterialGuideView = VK_NULL_HANDLE;
        VkImage RigidMotionGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory RigidMotionGuideMemory = VK_NULL_HANDLE;
        VkImageView RigidMotionGuideView = VK_NULL_HANDLE;
        VkImage AmbientGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory AmbientGuideMemory = VK_NULL_HANDLE;
        VkImageView AmbientGuideView = VK_NULL_HANDLE;
        VkImage FogGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory FogGuideMemory = VK_NULL_HANDLE;
        VkImageView FogGuideView = VK_NULL_HANDLE;
        VkImage OutlineGeometryGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory OutlineGeometryGuideMemory = VK_NULL_HANDLE;
        VkImageView OutlineGeometryGuideView = VK_NULL_HANDLE;
        VkImage ShadowImage = VK_NULL_HANDLE;
        VkDeviceMemory ShadowMemory = VK_NULL_HANDLE;
        VkImageView ShadowView = VK_NULL_HANDLE;
        VkImage DepthImage = VK_NULL_HANDLE;
        VkDeviceMemory DepthMemory = VK_NULL_HANDLE;
        VkImageView DepthView = VK_NULL_HANDLE;
        uint64_t ColorSurfaceGeneration = 0;
        uint64_t DepthSurfaceGeneration = 0;
        VkImage MsaaColorImage = VK_NULL_HANDLE;
        VkDeviceMemory MsaaColorMemory = VK_NULL_HANDLE;
        VkImageView MsaaColorView = VK_NULL_HANDLE;
        VkImage MsaaNormalGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory MsaaNormalGuideMemory = VK_NULL_HANDLE;
        VkImageView MsaaNormalGuideView = VK_NULL_HANDLE;
        VkImage MsaaMaterialGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory MsaaMaterialGuideMemory = VK_NULL_HANDLE;
        VkImageView MsaaMaterialGuideView = VK_NULL_HANDLE;
        VkImage MsaaRigidMotionGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory MsaaRigidMotionGuideMemory = VK_NULL_HANDLE;
        VkImageView MsaaRigidMotionGuideView = VK_NULL_HANDLE;
        VkImage MsaaAmbientGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory MsaaAmbientGuideMemory = VK_NULL_HANDLE;
        VkImageView MsaaAmbientGuideView = VK_NULL_HANDLE;
        VkImage MsaaFogGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory MsaaFogGuideMemory = VK_NULL_HANDLE;
        VkImageView MsaaFogGuideView = VK_NULL_HANDLE;
        VkImage MsaaOutlineGeometryGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory MsaaOutlineGeometryGuideMemory = VK_NULL_HANDLE;
        VkImageView MsaaOutlineGeometryGuideView = VK_NULL_HANDLE;
        VkImage MsaaDepthImage = VK_NULL_HANDLE;
        VkDeviceMemory MsaaDepthMemory = VK_NULL_HANDLE;
        VkImageView MsaaDepthView = VK_NULL_HANDLE;
        // Variants share native images. Attachments describes allocated storage;
        // mFramePicaAttachmentRequirements selects the active draw bindings.
        VkFramebuffer Framebuffer = VK_NULL_HANDLE;
        VkFramebuffer CanonicalFramebuffer = VK_NULL_HANDLE;
        bool AuxiliaryNeedsClear = false;
        bool WBuffering = false;
        bool NriOwned = false;
        Oot3d::PicaReflectionEnvironmentAccumulator
            ReflectionEnvironment;
    };

    struct NativePicaDisplayImage {
        uint32_t Width = 0;
        uint32_t Height = 0;
        VkImage Image = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        bool Initialized = false;
        bool NriOwned = false;
        ::Oot3d::Renderer::PicaCompositionDomain CompositionDomain =
            ::Oot3d::Renderer::PicaCompositionDomain::Unknown;
        uint64_t CompositionSequenceId = 0U;
        bool SceneResolved = false;
    };

    struct PresentRequest {
        VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
        VkSemaphore WaitSemaphore = VK_NULL_HANDLE;
        uint32_t ImageIndex = 0;
        uint32_t FrameIndex = 0;
        uint64_t Serial = 0;
    };

    struct PipelineKey {
        uint64_t ShaderId0 = 0;
        uint64_t ShaderId1 = 0;
        uint32_t VertexStride = 0;
        std::array<uint32_t, 3> TextureCoordinateOffsets{};
        uint32_t ColorOffset = 0;
        uint8_t ColorComponents = 0;
        bool UsesTexture0 = false;
        bool UsesVertexColor = false;
        bool DepthTest = false;
        bool DepthWrite = false;
        bool Decal = false;
        GfxNativeCullMode CullMode = GfxNativeCullMode::KeepAll;
        bool BlendEnabled = false;
        GfxNativeBlendEquation EquationRgb = GfxNativeBlendEquation::Add;
        GfxNativeBlendEquation EquationAlpha = GfxNativeBlendEquation::Add;
        GfxNativeBlendFactor SourceRgb = GfxNativeBlendFactor::One;
        GfxNativeBlendFactor DestRgb = GfxNativeBlendFactor::Zero;
        GfxNativeBlendFactor SourceAlpha = GfxNativeBlendFactor::One;
        GfxNativeBlendFactor DestAlpha = GfxNativeBlendFactor::Zero;

        auto operator<=>(const PipelineKey&) const = default;
    };

    struct alignas(16) VulkanDrawUniforms {
        std::array<int32_t, 4> TextureWidth{};
        std::array<int32_t, 4> TextureHeight{};
        std::array<int32_t, 4> TextureFiltering{};
        uint32_t FrameCount = 0;
        float NoiseScale = 1.0f;
        float PrimDepth = 0.0f;
        int32_t FogFlip = 0;
        int32_t AlphaTestEnabled = 0;
        int32_t AlphaTestFunction = 1;
        int32_t AlphaTestReference = 0;
        int32_t ShadowTextureBias = 0;
        int32_t ShadowOrthographic = 1;
        int32_t ShadowInvert = 0;
        int32_t Reserved = 0;
        std::array<uint32_t, 128> FogLut{};
    };

    struct TransformPushConstants {
        std::array<float, 16> ModelViewProjection = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
    };

    void CreateInstance();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreatePipelineCache();
    void StorePipelineCache();
    void CreateCommandResources();
    VkCommandBuffer SplitFrameForExternalCompute(
        VkSemaphore timelineSemaphore, uint64_t vulkanSignalValue,
        uint64_t externalCompletionValue);
    void CreateSyncObjects();
    void StartPresentWorker();
    void StopPresentWorker();
    void WaitForFramePresent(uint32_t frameIndex);
    void WaitForAllPresents();
    void PresentWorkerMain();
    void CreateShaderResources();
    void CreateNativePicaShaderResources();
    void DestroyNativePicaShaderResources();
    void DestroyNativePicaGeometryResources();
    void RetireNativePicaGeometry(uint64_t identity);
    void ReleaseRetiredNativePicaGeometryBuffers(uint32_t frameIndex);
    void CreateNativePicaScanoutPipeline();
    void CreateNativePicaRenderPass();
    void ApplyNativePicaSampleCount(VkSampleCountFlagBits sampleCount);
    NativePicaRenderTarget& GetOrCreateNativePicaRenderTarget(
        const GfxNativePicaDrawView& draw,
        uint32_t restoredWidth = 0U, uint32_t restoredHeight = 0U);
    void DestroyNativePicaRenderTarget(NativePicaRenderTarget& target);
    void PrepareNativePicaTargetAttachments(NativePicaRenderTarget& target);
    VkFramebuffer NativePicaFramebuffer(NativePicaRenderTarget& target, bool canonical);
    NativePicaDisplayImage& GetOrCreateNativePicaDisplayImage(
        uint64_t renderTargetNamespace, uint32_t physicalAddress,
        uint32_t width, uint32_t height);
    void DestroyNativePicaDisplayImage(NativePicaDisplayImage& image);
    std::vector<uint8_t> CaptureNativePicaImageBytes(
        VkImage image, uint32_t width, uint32_t height,
        uint32_t bytesPerPixel,
        VkImageLayout stableLayout, VkPipelineStageFlags stableStage,
        VkAccessFlags stableAccess, uint32_t mipLevel = 0U);
    void RestoreNativePicaImageBytes(
        VkImage image, uint32_t width, uint32_t height,
        uint32_t bytesPerPixel, std::span<const uint8_t> bytes,
        VkImageLayout initialLayout,
        VkPipelineStageFlags initialStage, VkAccessFlags initialAccess,
        VkImageLayout stableLayout, VkPipelineStageFlags stableStage,
        VkAccessFlags stableAccess);
    void CaptureNativePicaDepthStencilImage(
        VkImage image, uint32_t width, uint32_t height,
        std::vector<float>& depthValues,
        std::vector<uint8_t>& stencilValues);
    void RestoreNativePicaDepthStencilImage(
        VkImage image, uint32_t width, uint32_t height,
        std::span<const float> depthValues,
        std::span<const uint8_t> stencilValues);
    void BeginNativePicaRenderPass(NativePicaRenderTarget& target);
    void EndNativePicaRenderPass();
    bool TryRenderInteractiveGrass(
        NativePicaRenderTarget& target,
        const Oot3d::GraphicsSettings& settings,
        Oot3d::EffectGeometryProviderInvocationKind invocationKind);
    bool TryRenderDirectionalShadowMap(
        NativePicaRenderTarget& target,
        const Oot3d::GraphicsSettings& settings,
        const Oot3d::PicaCompositionStageAnchor& anchor);
    void ReleaseEffectGraphImageClients();
    void ApplyInternalResolutionScale(float scale);
    void ResetNativePicaRenderTargets(bool preserveDisplayImages = false);
    void ForgetNativePicaEffectNriTextures();
    void ApplyPendingNativePicaMemoryFills(NativePicaRenderTarget& target);
    void CreateFrameResources();
    void CreateFallbackTexture();
    void CreateOot3dShadow2dRenderPass();
    void CreateOot3dShadow2dDepthEncodePipeline();
    void DestroyOffscreenFramebuffer(OffscreenFramebufferRecord& framebuffer);
    bool CreateOot3dShadow2dFramebuffer(OffscreenFramebufferRecord& framebuffer,
                                       uint32_t width, uint32_t height,
                                       bool hasDepthBuffer);
    void CreateSwapchainResources();
    void DestroySwapchainResources();
    void DestroyPresentationPipelines();
    void DestroyGraphicsPipelines();
    void DestroyTexture(TextureRecord& texture);
    void DestroyBuffer(BufferAllocation& buffer);
    void RecreateSwapchain();
    bool SwapchainSurfaceChanged() const;
    QueueFamilies FindQueueFamilies(VkPhysicalDevice device) const;
    SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice device) const;
    bool DeviceSupportsSwapchain(VkPhysicalDevice device) const;
    VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
    VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    VkFormat FindDepthFormat() const;
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    BufferAllocation CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties, bool persistentlyMapped);
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height,
                           uint32_t mipLevel);
    void TransitionImageLayout(VkImage image, uint32_t baseMipLevel, uint32_t levelCount,
                               VkImageLayout oldLayout, VkImageLayout newLayout);
    VkCommandBuffer BeginImmediateCommands();
    void EndImmediateCommands(VkCommandBuffer commandBuffer);
    void CreateTextureImage(TextureRecord& texture, const uint8_t* rgba32Buf,
                            uint32_t width, uint32_t height);
    void UploadTextureLevel(TextureRecord& texture, uint32_t level, const uint8_t* rgba32Buf,
                            uint32_t width, uint32_t height);
    void RecreateSampler(TextureRecord& texture);
    VkDescriptorSet AllocateTextureDescriptorSet();
    VkPipeline GetOrCreatePipeline(const VulkanShaderProgram& shader);
    PipelineKey BuildPipelineKey(const VulkanShaderProgram& shader) const;
    VkShaderModule CompileShaderModule(
        const std::string& source, bool vertexShader,
        const char* sourceName,
        std::vector<uint32_t>* spirvOutput = nullptr);
    std::vector<uint32_t> CompileShaderSpirv(
        const std::string& source, bool vertexShader,
        const char* sourceName);
    void ConfigureNativePicaAotShaders();
    void FinishNativePicaAotShaders();
    void PrewarmNativePicaPipelines();
    std::vector<uint32_t> ResolveNativePicaShaderSpirv(
        std::string_view source, Oot3d::PicaAotShaderStage stage,
        bool vertexShader, const char* sourceName);
    VkShaderModule CreateShaderModuleFromSpirv(
        std::span<const uint32_t> spirv);
    VkPipeline GetOrCreateNativePicaPipeline(
        const GfxNativePicaDrawView& draw,
        const NativePicaShaderProgram& shader, bool writesReactiveMask,
        Oot3d::PicaShaderDomain domain,
        Oot3d::PicaShaderInstrumentationFeature requestedFeatures,
        Oot3d::PicaShaderInstrumentationFeature appliedFeatures,
        bool recordInventory = true, bool outlineOcclusionOnly = false);
    void CreateNativePicaTextureImage(TextureRecord& texture,
                                      std::span<const uint8_t> pixels,
                                      uint32_t width, uint32_t height,
                                      uint32_t mipLevels, VkFormat format);
    TextureRecord* GetOrCreateNativePicaTexture(
        const GfxNativePicaTextureView& texture, std::string* error,
        uint64_t replacementGeneration,
        const uint64_t* precomputedContentHash = nullptr);
    TextureRecord* GetOrCreateNativePicaLightingLut(
        const ::Oot3d::Renderer::PicaLightingLutView& lightingLut,
        std::string* error);
    VkDescriptorSet AllocateNativePicaDescriptorSet();
    VkDescriptorSet AllocateNativePicaScanoutDescriptorSet();
    std::string BuildVertexShaderSource(const VulkanShaderProgram& shader) const;
    std::string BuildFragmentShaderSource(uint64_t shaderId0, uint64_t shaderId1) const;
    static VulkanShaderProgram BuildShaderProgram(uint64_t shaderId0, uint64_t shaderId1);
    void CreateDepthResources();
    void BeginRenderPassIfNeeded();
    void ApplyDynamicViewportAndScissor();
    void DrawTrianglesFromBuffer(VkBuffer vertexBuffer, VkDeviceSize vertexOffset,
                                 size_t bufVboLen, size_t bufVboNumTris);

    GfxWindowBackendSDL2* mWindowBackend = nullptr;
    VkInstance mInstance = VK_NULL_HANDLE;
    VkSurfaceKHR mSurface = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkPipelineCache mPipelineCache = VK_NULL_HANDLE;
    uint32_t mGraphicsQueueFamily = 0;
    uint32_t mPresentQueueFamily = 0;
    VkQueue mGraphicsQueue = VK_NULL_HANDLE;
    VkQueue mPresentQueue = VK_NULL_HANDLE;
    bool mAsyncPresentSupported = false;
    uint32_t mVulkanAdapterCount = 0;
    uint32_t mVulkanAdapterIndex = 0;
    uint32_t mVulkanAdapterVendorId = 0;
    uint32_t mVulkanAdapterDeviceId = 0;
    bool mSynchronization2Enabled = false;
    bool mDynamicRenderingEnabled = false;
    bool mNisVulkanFeaturesEnabled = false;
    bool mFsrVulkanFeaturesEnabled = false;
    bool mD3d12InteropExtensionsEnabled = false;
    bool mNriSwapchainExtensionsEnabled = false;
    bool mNriSwapchainQueueEligible = false;
    bool mNgxVulkanRequirementsSatisfied = false;
    std::string mNgxVulkanUnavailableReason;
    VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
    VkFormat mSwapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D mSwapchainExtent{};
    VkSurfaceCapabilitiesKHR mSwapchainSurfaceCapabilities{};
    VkSurfaceFormatKHR mSwapchainSurfaceFormat{};
    std::vector<VkImage> mSwapchainImages;
    std::vector<VkImageView> mSwapchainImageViews;
    VkFormat mDepthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> mDepthImages;
    std::vector<VkDeviceMemory> mDepthMemories;
    std::vector<VkImageView> mDepthImageViews;
    VkRenderPass mRenderPass = VK_NULL_HANDLE;
    VkRenderPass mOverlayRenderPass = VK_NULL_HANDLE;
    VkRenderPass mNativePicaRenderPass = VK_NULL_HANDLE;
    VkRenderPass mNativePicaCanonicalRenderPass = VK_NULL_HANDLE;
    VkRenderPass mOot3dShadow2dRenderPass = VK_NULL_HANDLE;
    VkPipeline mOot3dShadow2dDepthEncodePipeline = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> mSwapchainFramebuffers;
    VkCommandPool mCommandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> mCommandBuffers{};
    std::array<VkCommandBuffer, kFramesInFlight>
        mContinuationCommandBuffers{};
    bool mFrameExternalComputeSplit = false;
    VkSemaphore mFrameExternalWaitSemaphore = VK_NULL_HANDLE;
    uint64_t mFrameExternalWaitValue = 0U;
    std::array<VkSemaphore, kFramesInFlight> mImageAvailableSemaphores{};
    std::vector<VkSemaphore> mRenderFinishedSemaphores;
    std::array<VkFence, kFramesInFlight> mInFlightFences{};
    std::array<FrameResources, kFramesInFlight> mFrameResources{};
    VulkanScanoutProbe mScanoutProbe;
    std::thread mPresentThread;
    std::mutex mSwapchainCallMutex;
    std::mutex mPresentMutex;
    std::condition_variable mPresentRequestCondition;
    std::condition_variable mPresentCompleteCondition;
    std::deque<PresentRequest> mPresentRequests;
    std::array<uint64_t, kFramesInFlight> mFramePresentSerials{};
    std::array<uint64_t, kFramesInFlight> mCompletedPresentSerials{};
    uint64_t mNextPresentSerial = 1;
    bool mPresentWorkerStop = false;
    std::atomic_bool mPresentSwapchainDirty = false;
    std::atomic_bool mSwapchainSuboptimal = false;
    std::atomic<int32_t> mPresentError = VK_SUCCESS;
    std::vector<VkFence> mImagesInFlight;
    VkDescriptorSetLayout mTextureDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
    std::map<PipelineKey, VkPipeline> mPipelines;
    VkDescriptorSetLayout mNativePicaDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout mNativePicaPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout mNativePicaScanoutDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout mNativePicaScanoutPipelineLayout = VK_NULL_HANDLE;
    VkPipeline mNativePicaScanoutPipeline = VK_NULL_HANDLE;
    VkPipeline mNativePicaScanoutOverlayPipeline = VK_NULL_HANDLE;
    VkSampler mNativePicaScanoutSampler = VK_NULL_HANDLE;
    std::map<std::pair<uint64_t, uint64_t>, NativePicaShaderProgram>
        mCanonicalNativePicaShaders;
    std::map<std::pair<uint64_t, uint64_t>, NativePicaShaderProgram>
        mInstrumentedNativePicaShaders;
    Oot3d::PicaGeometryRegistry mPicaGeometryRegistry;
    Oot3d::PicaCompositionSchedule mNativePicaCompositionSchedule;
    Oot3d::PicaSceneFrame mPicaSceneFrame;
    Oot3d::NativeSceneView mNativeSceneView;
    Oot3d::PicaScenePublicationAdapter mPicaScenePublications;
    Oot3d::GrassGeometryRegistry mGrassGeometryRegistry;
    std::unordered_map<uint64_t, NativePicaGeometryBuffer> mNativePicaGeometryBuffers;
    std::array<std::vector<BufferAllocation>, kFramesInFlight> mRetiredNativePicaGeometryBuffers;
    Oot3d::PicaShaderPipelineCache mPicaShaderPipelineCache;
    Oot3d::PicaAotShaderPack mPicaAotShaderPack;
    Renderer::SpirvCache mCompiledShaderCache;
    bool mCompiledShaderCacheSummaryLogged = false;
    Oot3d::PicaEffectiveShaderInventory mPicaEffectiveShaderInventory;
    Oot3d::PicaGraphicsPipelineInventory mPicaPipelineInventory;
    Oot3d::PicaGraphicsPipelineManifest mPicaPipelineManifest;
    std::set<uint64_t> mPicaPipelinePrewarmedProfiles;
    std::set<std::pair<Oot3d::PicaAotShaderStage, uint64_t>>
        mPicaAotShaderMissesLogged;
    uint64_t mPicaAotShaderHits = 0;
    uint64_t mPicaAotShaderMisses = 0;
    bool mPicaAotShaderStrict = false;
    bool mPicaAotShaderSummaryLogged = false;
    bool mPicaPipelinePrewarmSummaryLogged = false;
    bool mPicaPipelinePrewarmEnabled = false;
    uint64_t mPicaPipelinePrewarmCreated = 0U;
    uint64_t mPicaPipelinePrewarmReused = 0U;
    uint64_t mPicaPipelinePrewarmSkipped = 0U;
    Oot3d::GraphicsSettings mFrameGraphicsSettings;
    uint64_t mFrameGraphicsSettingsRevision = 0;
    uint64_t mAzaharConfiguredSettingsRevision = 0;
    std::optional<Oot3d::PicaAttachmentFeatureRequests>
        mPicaExtensionRequests;
    Oot3d::CompiledEffectGraph mPicaExtensionGraph;
    Oot3d::EffectGeometryProviderPlan mInteractiveGrassProviderPlan;
    Oot3d::EffectGeometryProviderExecutionLedger
        mInteractiveGrassProviderExecution;
    Oot3d::EffectPassBarrierPlan mDirectionalShadowMapBarrierPlan;
    ::Fast::Renderer::ExtensionPassSchedulePlan
        mDirectionalShadowSchedulePlan;
    uint64_t mFrameAzaharTextureGeneration = 0;
    uint64_t mCustomTextureUploadBytesThisFrame = 0;
    std::map<std::vector<uint8_t>, VkPipeline> mNativePicaPipelines;
    std::map<NativePicaTextureKey, TextureRecord> mNativePicaTextures;
    std::map<uint64_t, NativePicaLightingLutTexture>
        mNativePicaLightingLuts;
    std::map<NativePicaRenderTargetKey, NativePicaRenderTarget>
        mNativePicaRenderTargets;
    std::map<std::pair<uint64_t, uint32_t>, NativePicaDisplayImage>
        mNativePicaDisplayImages;
    std::map<std::pair<uint64_t, uint32_t>, NativePicaRenderTargetKey>
        mNativePicaDisplayDepthTargets;
    std::set<uint32_t> mInvalidatedNativePicaRenderTargetAddresses;
    NativePicaRenderTarget* mActiveNativePicaRenderTarget = nullptr;
    std::optional<GfxNativePicaDisplayTransferView>
        mLastPresentedNativePicaDisplayTransfer;
    std::vector<GfxNativePicaMemoryFillView> mPendingNativePicaMemoryFills;
    bool mPicaMemoryFillSmokeInjected = false;
    std::vector<uint64_t> mCompletedNativePicaIds;
    VkDeviceSize mStorageBufferAlignment = 16;
    VkDeviceSize mUniformBufferAlignment = 16;
    uint32_t mCurrentFrame = 0;
    uint32_t mCurrentImage = 0;
    uint32_t mFrameCounter = 0;
    uint32_t mDrawCallCountThisFrame = 0;
    uint32_t mRequestedWidth = 0;
    uint32_t mRequestedHeight = 0;
    int mCurrentFramebuffer = 0;
    float mCurrentNoiseScale = 1.0f;
    bool mInitialized = false;
    bool mSwapchainDirty = false;
    bool mFrameActive = false;
    bool mRenderPassActive = false;
    bool mOverlayRenderPassActive = false;
    bool mNativePicaRenderPassActive = false;
    bool mVsyncEnabled = true;
    float mInternalResolutionScale = 1.0F;
    bool mPresentationSettingsApplied = false;
    uint8_t mAppliedWindowMode = 0;
    uint32_t mAppliedOutputWidth = 0;
    uint32_t mAppliedOutputHeight = 0;
    bool mAppliedVsync = true;
    bool mNativePicaPresentedThisFrame = false;
    uint32_t mSpatialAaMode = 0;
    VkSampleCountFlags mNativePicaSupportedSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits mNativePicaSampleCount = VK_SAMPLE_COUNT_1_BIT;
    VkResolveModeFlagBits mNativePicaDepthResolveMode = VK_RESOLVE_MODE_NONE;
    bool mFrameSubmitted = false;
    bool mImGuiInitialized = false;
    VkViewport mViewport{};
    VkRect2D mScissor{};
    bool mViewportSet = false;
    bool mScissorSet = false;
    FilteringMode mFilterMode = FILTER_THREE_POINT;
    uint32_t mNextTextureId = 1;
    uint32_t mCurrentTextureUnit = 0;
    std::array<uint32_t, 8> mSelectedTextures{};
    std::unordered_map<uint32_t, TextureRecord> mTextures;
    std::unordered_map<uint64_t, CachedVertexBuffer> mOot3dCachedVertexBuffers;
    std::unordered_map<int, OffscreenFramebufferRecord> mOffscreenFramebuffers;
    TextureRecord mFallbackTexture;
    std::vector<VkSampler> mRetiredSamplers;
    GfxNativeBlendState mBlendState{};
    GfxNativeCullMode mCullMode = GfxNativeCullMode::KeepAll;
    VulkanDrawUniforms mDrawUniforms{};
    TransformPushConstants mTransformPushConstants{};
    uint64_t mFogStateKey = 0;
    bool mFogStateValid = false;
    std::map<std::pair<uint64_t, uint64_t>, VulkanShaderProgram> mShaders;
    VulkanShaderProgram* mCurrentShader = nullptr;
    int mFramebufferCount = 1;
    int mBoundOot3dShadow2dFramebufferId = -1;
    int mActiveOot3dShadow2dFramebufferId = -1;
    bool mOot3dShadow2dPassActive = false;
    VkViewport mViewportBeforeOot3dShadow2d{};
    VkRect2D mScissorBeforeOot3dShadow2d{};
    bool mViewportSetBeforeOot3dShadow2d = false;
    bool mScissorSetBeforeOot3dShadow2d = false;
    int mFramebufferBeforeOot3dShadow2d = 0;
    Oot3dVulkanDiagnostics mDiagnostics{
        Oot3dVulkanDiagnosticsConfig::FromEnvironment()
    };
    Oot3d::RendererValidationTelemetry mValidationTelemetry;
    Oot3dVulkanValidation mVulkanValidation;

    struct PreviousPicaVertexUniforms {
        uint64_t FrameId = 0;
        std::vector<uint8_t> Bytes;
        std::array<float, 2> JitterNdc{};
    };
    Oot3dVulkanGpuProfiler mGpuProfiler;
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
    Oot3d::D3d12NgxProvider mD3d12NgxProvider;
#endif
    Oot3d::NriInteropContext mNriInterop;
    Oot3d::CacaoPass mCacaoPass;
    Oot3d::FidelityFxSssrPass mFidelityFxSssrPass;
    Oot3d::HiZDepthPyramidPass mHiZDepthPyramidPass;
    Oot3d::HiZReflectionPass mHiZReflectionPass;
    Oot3d::InteractiveGrassPass mInteractiveGrassPass;
    Oot3d::LinearSceneColorPass mLinearSceneColorPass;
    Oot3d::MotionVectorPass mMotionVectorPass;
    Oot3d::NriDirectionalShadowPass mNriDirectionalShadowPass;
    Oot3d::NriEffectGraphTransientImageArena
        mNriEffectGraphTransientImageArena;
    Oot3d::NriPicaDisplayCopyPass mNriPicaDisplayCopyPass;
    Oot3d::NriPicaMemoryFillClearPass mNriPicaMemoryFillClearPass;
    Oot3d::NriPicaScanoutPass mNriPicaScanoutPass;
    Oot3d::NriPicaPipelineBridge mNriPicaPipelineBridge;
    Oot3d::NriPicaRenderTargetInitPass mNriPicaRenderTargetInitPass;
    Oot3d::NriPicaRenderTargetOwner mNriPicaRenderTargetOwner;
    Oot3d::NriPicaTextureImageOwner mNriPicaDisplayImageOwner;
    Oot3d::NriPicaTextureImageOwner mNriPicaTextureImageOwner;
    Oot3d::NriPicaTextureUploadPass mNriPicaTextureUploadPass;
    Oot3d::NriSwapchain mNriSwapchain;
    Oot3d::PicaDynamicRenderingScope mPicaDynamicRenderingScope;
    Oot3d::NriUpscalerPass mNriUpscalerPass;
    Oot3d::TemporalHistoryManager mTemporalHistory;
    Oot3d::PicaRigidMotionTracker mRigidMotionTracker;
    Oot3d::SceneCompositePass mSceneCompositePass;
    Oot3d::Smaa1xPass mSmaa1xPass;
    Oot3d::ReflectionIblPass mReflectionIblPass;
    Oot3d::ReflectionMaterialResolvePass
        mReflectionMaterialResolvePass;
    Oot3d::TemporalAaPass mTemporalAaPass;
    uint64_t mCacaoRenderTargetNamespace = 0;
    uint32_t mCacaoDisplayPhysicalAddress = 0;
    bool mCacaoOutputValid = false;
    bool mCacaoExecutedThisFrame = false;
    bool mHiZExecutedThisFrame = false;
    uint64_t mHiZRenderTargetNamespace = 0;
    uint32_t mHiZDisplayPhysicalAddress = 0;
    bool mHiZOutputValid = false;
    std::optional<Oot3d::SceneSurfaceKey> mHiZReflectionSurface;
    bool mReflectionExecutedThisFrame = false;
    Oot3d::ReflectionProvider mReflectionProviderThisFrame =
        Oot3d::ReflectionProvider::Off;
    Oot3d::SceneColorEncoding mReflectionOutputEncodingThisFrame =
        Oot3d::SceneColorEncoding::Unknown;
    std::optional<Oot3d::SceneSurfaceKey> mMotionSurface;
    std::optional<Oot3d::SceneSurfaceKey> mLinearColorSurface;
    bool mLinearColorExecutedThisFrame = false;
    bool mMotionExecutedThisFrame = false;
    bool mUpscalerExecutedThisFrame = false;
    bool mUpscalerUsedD3d12ThisFrame = false;
    Oot3d::UpscalerProvider mUpscalerProviderThisFrame =
        Oot3d::UpscalerProvider::Nis;
    Oot3d::SceneColorEncoding mUpscalerOutputEncodingThisFrame =
        Oot3d::SceneColorEncoding::Unknown;
    bool mUpscalerEffectsCompositedThisFrame = false;
    uint64_t mUpscalerRenderTargetNamespace = 0;
    uint32_t mUpscalerDisplayPhysicalAddress = 0;
    uint32_t mUpscalerOutputWidth = 0;
    uint32_t mUpscalerOutputHeight = 0;
    std::optional<Oot3d::SceneSurfaceKey> mTaaSurface;
    std::optional<Oot3d::SceneSurfaceKey> mCompositeSurface;
    bool mCompositeExecutedThisFrame = false;
    Oot3d::SceneColorEncoding mCompositeOutputEncodingThisFrame =
        Oot3d::SceneColorEncoding::Unknown;
    bool mTaaOutputValid = false;
    bool mTaaExecutedThisFrame = false;
    Oot3d::SceneColorEncoding mTaaOutputEncodingThisFrame =
        Oot3d::SceneColorEncoding::Unknown;
    bool mTaaEffectsCompositedThisFrame = false;
    bool mSmaaExecutedThisFrame = false;
    bool mSmaaEffectsCompositedThisFrame = false;
    Oot3d::SceneColorEncoding mSmaaOutputEncodingThisFrame =
        Oot3d::SceneColorEncoding::Unknown;
    uint64_t mSmaaRenderTargetNamespace = 0;
    uint32_t mSmaaDisplayPhysicalAddress = 0;
    uint32_t mSmaaWidth = 0;
    uint32_t mSmaaHeight = 0;
    bool mTemporalMotionEnabled = false;
    bool mTemporalJitterEnabled = false;
    bool mNativeFidelityProfile = false;
    Oot3d::PicaAttachmentRequirements mFramePicaAttachmentRequirements;
    std::array<float, 2> mTemporalJitterPixels{};
    bool mGrassExecutedThisFrame = false;
    std::set<NativePicaRenderTargetKey>
        mGrassRenderedTargetsThisFrame;
    std::set<NativePicaRenderTargetKey>
        mGrassInsertionAttemptedTargetsThisFrame;
    std::set<NativePicaRenderTargetKey>
        mGrassWorldTargetsThisFrame;
    std::set<std::pair<uint64_t, NativePicaRenderTargetKey>>
        mDirectionalShadowInsertionAttemptedTargetsThisFrame;
    std::unordered_map<uint64_t, uint32_t>
        mGrassSurfaceOccurrencesThisFrame;
    std::unordered_map<uint64_t, uint32_t>
        mRigidMotionOccurrencesThisFrame;
    std::unordered_map<uint64_t, PreviousPicaVertexUniforms>
        mPreviousPicaVertexUniforms;
    uint32_t mNativePicaDepthWritingDrawsThisFrame = 0;
    Oot3d::SceneSurfaceRegistry mSceneSurfaces;
    Oot3d::ResourceStateTracker mResourceStates;
};

} // namespace Fast

#endif
