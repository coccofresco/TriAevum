#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "fast/backends/gfx_rendering_api.h"
#include "fast/renderer/spirv_cache.h"
#include "fast/renderer3ds/pica_shader_module_identity.h"
#include "fast/renderer3ds/pica_temporal_history.h"
#include "fast/renderer3ds/pica_motion_provenance.h"
#include "fast/renderer3ds/pica_program_preparation.h"
#include "fast/renderer3ds/pica_preparation_queue.h"
#include "fast/renderer3ds/pipeline_cache_flusher.h"
#include "fast/renderer3ds/pica_texture_copy_plan.h"
#include "fast/renderer3ds/pica_shadow_geometry.h"
#include "fast/renderer3ds/pica_ui_canvas.h"
#include "fast/renderer3ds/pica_pbr_material.h"
#include "fast/renderer3ds/pica_pbr_seams.h"
#include "fast/renderer3ds/pica_surface_lighting.h"
#include "fast/renderer3ds/pica_surface_calibration_trace.h"
#include "fast/renderer3ds/gpu_memory_residency.h"
#include "fast/renderer3ds/vulkan_memory_budget.h"
#include "fast/renderer3ds/vulkan_breadcrumbs.h"
#include "fast/renderer3ds/vulkan_device_fault.h"
#include "fast/backends/oot3d_vulkan_diagnostics.h"
#include "fast/backends/oot3d_vulkan_gpu_profiler.h"
#include "fast/backends/oot3d_vulkan_validation.h"
#include "fast/backends/vulkan_scanout_probe.h"
#include "fast/extension/render_resolution_policy.h"
#include "fast/extension/pica_attachment_formats.h"
#include "fast/extension/nri_interop_context.h"
#include "fast/extension/brixelizer_gi_pass.h"
#include "fast/extension/brixelizer_scene.h"
#include "fast/extension/graphics_settings_runtime.h"
#include "fast/extension/presentation_picture.h"
#include "fast/extension/render_resolution_policy.h"
#include "fast/extension/screen_space_gi_provider.h"
#include "fast/extension/sample_view.h"
#include "fast/renderer3ds/pica_gi_geometry.h"
#include "fast/extension/ambient_occlusion_pass.h"
#include "fast/extension/cacao_pass.h"
#include "fast/extension/mxao_pass.h"
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
#include "fast/extension/d3d12_ngx_provider.h"
#endif
#include "fast/extension/effect_geometry_provider_plan.h"
#include "fast/extension/effect_graph.h"
#include "fast/extension/fidelityfx_sssr_pass.h"
#include "fast/extension/grass_geometry_registry.h"
#include "fast/extension/grass_draw_memo.h"
#include "fast/extension/hiz_depth_pyramid_pass.h"
#include "fast/extension/hiz_reflection_pass.h"
#include "fast/extension/interactive_grass_pass.h"
#include "fast/extension/world_debug_line_pass.h"
#include "fast/extension/linear_scene_color.h"
#include "fast/extension/linear_scene_color_pass.h"
#include "fast/extension/motion_vector_pass.h"
#include "fast/extension/native_scene_view.h"
#include "fast/extension/title_render_backend.h"
#include "fast/extension/nri_directional_shadow_pass.h"
#include "fast/extension/nri_effect_graph_transient_image_arena.h"
#include "fast/extension/nri_pica_display_copy_pass.h"
#include "fast/extension/nri_pica_memory_fill_clear_pass.h"
#include "fast/extension/nri_pica_scanout_pass.h"
#include "fast/extension/nri_pica_pipeline_bridge.h"
#include "fast/extension/nri_pica_render_target_init_pass.h"
#include "fast/extension/nri_pica_render_target_owner.h"
#include "fast/extension/nri_pica_texture_image_owner.h"
#include "fast/extension/nri_pica_texture_upload_pass.h"
#include "fast/extension/nri_swapchain.h"
#include "fast/extension/nri_upscaler_pass.h"
#include "fast/extension/upscaler_guide_pass.h"
#include "fast/extension/native_ui_overlay_pass.h"
#include "fast/extension/pica_dynamic_rendering_scope.h"
#include "fast/extension/pica_aot_shader_pack.h"
#include "fast/extension/pica_composition_schedule.h"
#include "fast/extension/pica_extension_schedule.h"
#include "fast/extension/pica_pipeline_manifest.h"
#include "fast/extension/pica_scene_publication_adapter.h"
#include "fast/extension/reflection_ibl.h"
#include "fast/extension/reflection_ibl_pass.h"
#include "fast/extension/reflection_material_resolve_pass.h"
#include "fast/extension/reflection_provider.h"
#include "fast/extension/resource_state_tracker.h"
#include "fast/extension/pica_geometry_registry.h"
#include "fast/extension/pica_scene_frame.h"
#include "fast/extension/pica_shader_pipeline_cache.h"
#include "fast/extension/pica_variant_request_record.h"
#include "fast/extension/scene_surface_registry.h"
#include "fast/extension/scene_composite_pass.h"
#include "fast/extension/smaa_1x_pass.h"
#include "fast/extension/pica_rigid_motion.h"
#include "fast/extension/temporal_history_manager.h"
#include "fast/extension/temporal_aa_pass.h"
#include "fast/extension/temporal_jitter.h"
#include <vulkan/vulkan.h>
#include <imgui.h>

#include <array>
#include <atomic>
#include <compare>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <tuple>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Oot3d::Renderer { struct AzaharPbrMaterial; }

namespace Fast {

class GfxWindowBackendSDL2;

// Native PICA texture geometry, shared between the backend's texture creation
// and the raw-copy register (defined in gfx_vulkan_pica.cpp).
uint32_t NativePicaMipLevelCount(const GfxNativePicaTextureView& texture);
std::optional<size_t> NativePicaEncodedMipSize(uint8_t format, uint32_t width,
                                               uint32_t height);

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
                                    public Extension::TitleRenderBackend,
                                    public Renderer3ds::PicaProgramPreparationBackend {
  public:
    explicit GfxRenderingAPIVulkan(GfxWindowBackendSDL2* windowBackend);
    ~GfxRenderingAPIVulkan() override;
    void PrepareNativePicaPrograms(std::span<const Renderer3ds::PicaVertexArtifact> vertices) override;
    [[nodiscard]] bool UsesAlwaysAvailablePrograms() const override;
    void RegisterTitlePassPrograms(std::span<const Renderer::PassShaderArtifact> programs) override;
    [[nodiscard]] Renderer3ds::PicaPreparationStatus PreparationStatus() const override;
    void SetNativeTopScreenExtendsToOutput(bool extends) noexcept override {
        mNativeTopScreenExtendsToOutput = extends;
    }

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
    [[nodiscard]] bool WantsPicaSequenceDrawPreparation(
        const ::Fast::Renderer3ds::PicaCompositionDrawReference& draw) const override;
    bool PreparePicaSequenceDraw(const GfxNativePicaDrawView& draw,
                                 std::string* error = nullptr) override;
    // Grass masks read the output alpha of the overlays they capture during
    // preparation, translucent ones included.
    [[nodiscard]] bool PicaSequenceDrawPreparationNeedsFragmentState() const override {
        return mInteractiveGrassProviderPlan.Valid();
    }
    // A scene draw lying on one of its binding's water bodies (flat, at the
    // body's height, over it): the body index. World bounds come from the
    // draw's cached triangle geometry; unknown geometry is not water.
    struct NativePicaWaterSurface {
        std::optional<uint32_t> Body;
        ::Fast::Renderer3ds::ShadowBounds WorldBounds;
        // Its bounds reach a body's surface: it may hold water fragments.
        bool ReachesWater = false;
        // A catalog water texture: all of the draw is water (a waterfall, a
        // river's rapids); flat and reaching no body, its own plane (water
        // the title publishes no body for: a moat, a well).
        bool WholeDraw = false;
        std::optional<::Fast::Renderer3ds::PicaSceneWaterBody> Plane;
        // Its catalog roles (PicaWaterTextureRole).
        uint8_t Roles = 0;
        // A whole water draw's world triangles by slope (the water map's
        // pieces) and their placement key.
        std::shared_ptr<const ::Fast::Renderer3ds::PicaWaterTriangles> Pieces;
        uint64_t PiecesKey = 0;
    };
    // Water triangles per rigid placement (PicaGiInstance::RigidSignature).
    std::unordered_map<uint64_t, std::shared_ptr<const ::Fast::Renderer3ds::PicaWaterTriangles>> mWaterTriangles;
    // The drawn water's map, rebuilt when the set of pieces on screen changes.
    ::Fast::Renderer3ds::PicaWaterMap mWaterMap;
    uint64_t mWaterMapSignature = 0;
    uint64_t mWaterMapVersion = 0;
    // Ripples of the actors moving through the water (their collision
    // cylinders), advanced once per frame on the water ripple clock.
    ::Fast::Renderer3ds::PicaWaterRippleField mWaterRipples;
    uint64_t mWaterRipplesFrame = 0;
    const std::array<::Fast::Renderer3ds::PicaWaterRing, ::Fast::Renderer3ds::kPicaWaterRings>& AdvanceWaterRipples(
        const ::Fast::Renderer3ds::PicaSceneWaterBodies& bodies, float seconds);
    std::array<::Fast::Renderer3ds::PicaWaterRing, ::Fast::Renderer3ds::kPicaWaterRings> mWaterRings{};
    NativePicaWaterSurface ResolveNativePicaWaterSurface(const GfxNativePicaDrawView& draw);
    void TraceSurfaceCalibrationDraw(const GfxNativePicaDrawView& draw,
        const Extension::PicaShaderPipelineResult& variant,
        const std::optional<Renderer3ds::PicaSurfaceLightingUniforms>& lighting,
        bool candidate, bool exempt, bool authoredAppearance, bool isolated, bool perspective, bool uber);
    void FinishSurfaceCalibrationTrace();
    // The title's water textures (TRIAEVUM_WATER_TEXTURE_CATALOG, generated
    // by the installer from the user's RomFS), loaded on first use.
    const ::Fast::Renderer3ds::PicaWaterTextureCatalog* WaterTextureCatalog();
    std::optional<::Fast::Renderer3ds::PicaWaterTextureCatalog> mWaterTextureCatalog;
    bool mWaterTextureCatalogLoaded = false;
    // Water ripple time origin (cosmetic animation, presentation clock).
    std::chrono::steady_clock::time_point mWaterEpoch = std::chrono::steady_clock::now();
    // Texture scrolling of water draws, per geometry (native time).
    std::unordered_map<uint64_t, ::Fast::Renderer3ds::PicaWaterFlowTrack> mWaterFlow;
    // This frame traces reflections for water only (Reflections Off).
    bool mWaterOnlyReflections = false;
    // The frame whose scene draws last published water a water-only trace
    // finds (the title's bodies or catalog water planes).
    uint64_t mReflectableWaterFrame = UINT64_MAX;
    bool SubmitPicaDisplayTransfer(
        const GfxNativePicaDisplayTransferView& transfer,
        std::string* error = nullptr) override;
    // Declares the producers referenced by the frontend ledger and captured
    // plans. Reclamation also waits for deferred guest ownership and the last
    // GPU frame that sampled the producer; dropping a plan pin alone is unsafe.
    bool SetPinnedPicaTextureCopies(
        std::span<const ::Fast::Renderer3ds::PicaTextureCopyReference> copies,
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
        const Extension::TitlePresentationSettingsView& settings,
        std::string* error = nullptr) override;
    // Readable from the ImGui thread while the renderer runs elsewhere.
    bool ImGuiBackendReady() const noexcept {
        return mImGuiInitialized.load(std::memory_order_acquire);
    }
    void SetPresentationSettingsHeld(bool held) override {
        mPresentationSettingsHeld = held;
    }
    bool PublishSceneView(
        const Extension::TitleSceneViewSubmission& view) override;
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
    bool HasActiveFrame() const override { return mFrameActive; }
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
    bool ReadFramebufferRgb8ToCPU(uint32_t width, uint32_t height, uint8_t* rgb) override;
    // The presented swapchain image as 8-bit RGBA (or BGRA); empty when none.
    std::vector<uint8_t> ReadSwapchainRgba8(uint32_t& width, uint32_t& height, bool& bgra);
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
        // Ownership is explicit, never inferred from ImageLayout: the image is
        // another owner's (a live Shadow2D render target, or an immutable raw
        // copy source owned by the GPU copy register) and this record must not
        // destroy it.
        bool ImageBorrowed = false;
        // A storage alias of a live render target. Its content and lifetime
        // belong to that target, so it is serialized with the target rather
        // than with the texture cache.
        bool LiveRenderTargetAlias = false;
        bool Uploaded = false;
        // Native-sized RGBA8 image holding the CPU-owned base of a derived
        // raw-copy sample, owned by this record and sampled only until the base
        // fill dispatch that expands it into the sampled image has executed.
        VkImage BaseImage = VK_NULL_HANDLE;
        std::vector<uint64_t> CopyProducerIdentities;
        // The sampled content is a raw-copy production whose scene effects were
        // already resolved when it was captured, so a draw declared full-frame
        // screen content that samples it writes colour already carrying them.
        // Carried on the record, so a cache hit and a restored record keep it.
        bool SourceResolvedColour = false;
        // The owned (producer-covered) rectangle of the sampled texture in
        // declared native texels, so a draw's sampled UV footprint can be tested
        // against the content that is actually resolved.
        uint32_t SourceOwnedX0 = 0;
        uint32_t SourceOwnedY0 = 0;
        uint32_t SourceOwnedX1 = 0;
        uint32_t SourceOwnedY1 = 0;
        uint32_t SourceDeclaredWidth = 0;
        uint32_t SourceDeclaredHeight = 0;
        uint64_t CustomReplacementHash = 0;
        uint64_t CustomReplacementPixelHash = 0;
        bool CustomReplacementApplied = false;
        bool CustomReplacementPending = false;
        bool CustomReplacementReady = false;
        // The last frame a draw resolved this record: the texture cache
        // releases records no draw samples (ReleaseIdleNativePicaTextures).
        uint64_t LastUsedFrame = 0;
    };

    struct NativePicaMaterialBundle {
        std::shared_ptr<const ::Oot3d::Renderer::AzaharPbrMaterial> Material;
        // All material maps and the optional color replacement publish together.
        std::array<TextureRecord, Renderer3ds::kPicaPbrTextureCount + 1> Images;
        std::array<uint32_t, Renderer3ds::kPicaPbrTextureCount + 1> UploadedRows{};
        uint8_t ReusedImages = 0;
    };
    struct NativePicaMaterialEntry {
        NativePicaMaterialBundle Active;
        NativePicaMaterialBundle Pending;
        uint64_t FailedGeneration = 0;
        uint64_t LastUsedFrame = 0;
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
        std::optional<VkDeviceSize> SurfaceLightingUniformOffset;
        Renderer3ds::PicaSurfaceLightingUniforms SurfaceLightingUniforms;
        VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
        std::map<TextureDescriptorKey, VkDescriptorSet> TextureDescriptorSets;
        std::vector<uint64_t> NativePicaCompletionIds;
        std::vector<TextureRecord> RetiredTextures;
        std::vector<BufferAllocation> RetiredUniformBuffers;
        // Vertex arenas replaced mid-frame by EnsureFrameVertexCapacity;
        // recorded draws still read them until the frame's fence completes.
        std::vector<BufferAllocation> RetiredVertexBuffers;
    };

    struct CachedVertexBuffer {
        std::array<BufferAllocation, kFramesInFlight> Buffers{};
        std::array<uint64_t, kFramesInFlight> ContentVersions{};
        size_t FloatCount = 0;
        size_t TriangleCount = 0;
        uint32_t LastUsedFrame = 0;
    };

    using NativePicaShaderModuleKey = Renderer3ds::PicaShaderModuleIdentity<
        Extension::PicaGraphicsPipelineShaderOutputs>;
    // Source to SPIR-V of one specialized program on a preparation thread.
    // Inputs are owned copies; the render thread adopts the results.
    struct NativePicaProgramPreparation {
        std::string VertexSource;
        std::string FragmentSource;
        std::string Stem;
        bool ResolveVertex = false;
        bool Nri = false;
        uint32_t DescriptorSchemaVersion = 0;
        uint64_t CanonicalPipelineId = 0;
        uint64_t FragmentShaderKey = 0;
        uint64_t SettingsRevision = 0;
        std::vector<uint32_t> VertexSpirv;
        std::vector<uint32_t> NriFragmentSpirv;
        std::string NriFragmentSource;
        bool NriApplied = false;
        std::string NriError;
        std::string CompileError;
        std::shared_ptr<Renderer3ds::PicaPreparationTask> Task;
    };

    struct NativePicaShaderProgram {
        VkShaderModule VertexShader = VK_NULL_HANDLE;
        VkShaderModule FragmentShader = VK_NULL_HANDLE;
        std::vector<uint32_t> NriVertexSpirv;
        std::vector<uint32_t> NriFragmentSpirv;
        Extension::PicaAotShaderSourceIdentity VertexSource;
        Extension::PicaAotShaderSourceIdentity FragmentSource;
        Extension::PicaAotShaderSourceIdentity NriFragmentSource;
        Extension::PicaGraphicsPipelineShaderOutputs FragmentOutputs;
        bool NriDescriptorContract = false;
        // Background preparation not yet adopted (specialized programs).
        std::shared_ptr<NativePicaProgramPreparation> Preparation;
        // An always-available program exists only as built-in artifacts.
        bool Available = true;
        uint8_t UnavailableReason = 0;
        // The superset effect program: binding 15 carries its decisions.
        bool EffectBlock = false;
        // Guide locations its specialized program declares (EffectBlock).
        uint8_t DeclaredOutputs = 0xFFU;
    };

    enum class NativePicaPreparationMode : uint8_t {
        // Every program and pipeline is created inline by the draw needing it.
        Synchronous,
        // Specialized programs are prepared on preparation threads; a draw
        // finding its own unfinished completes it (deterministic output).
        Complete,
        // A draw renders with its always-available program until its
        // specialized program is ready; it never waits for compilation.
        Hybrid,
        // Verification: always-available programs wherever they exist.
        Uber,
    };

    enum class NativePicaPipelineAcquisition : uint8_t {
        Blocking,
        // Owned pipeline created by a preparation thread (Pending meanwhile).
        Background,
        // Fast link of prepared library parts (always-available programs).
        Libraries,
    };

    enum class NativePicaUberUnavailable : uint8_t {
        NoParametricProgram,
        VertexNotBuiltIn,
        FragmentNotBuiltIn,
        NoNriContract,
        PipelineFailed,
        EffectNotCovered,
        Count,
    };

    struct NativePicaPreparationCounters {
        uint64_t SpecializedDraws = 0;
        uint64_t UberDraws = 0;
        uint64_t CompletedDraws = 0;
        uint64_t ProgramTasks = 0;
        std::array<uint64_t, static_cast<size_t>(NativePicaUberUnavailable::Count)> UberUnavailable{};
        double CompleteMilliseconds = 0.0;
        double MaximumCompleteMilliseconds = 0.0;
        uint64_t UberPrograms = 0;
        // Render-thread time acquiring always-available pipelines (a link,
        // or a wait for a part still being prepared).
        double UberAcquireMilliseconds = 0.0;
        double MaximumUberAcquireMilliseconds = 0.0;
        // Always-available draws per effect branch taken (coverage evidence).
        uint64_t UberSurfaceDraws = 0, UberPbrDraws = 0, UberReliefDraws = 0, UberToonDraws = 0;
    };

    struct NativePicaGeometryBuffer {
        std::array<BufferAllocation, kFramesInFlight> Buffers{};
        std::array<uint64_t, kFramesInFlight> ContentVersions{};
        std::array<uint64_t, kFramesInFlight> StructuralSignatures{};
        Renderer3ds::PicaShadowCasterCache ShadowCaster;
        Renderer3ds::PicaGiGeometryCache GiGeometry;
        Renderer3ds::PicaPbrSeamCache Seams;
        uint64_t LastUsedFrame = 0;
    };

    // Where this frame's GPU copy of one resolved geometry lives.
    struct NativePicaGeometryPlacement {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceSize Size = 0U;
        uint8_t* Mapped = nullptr;
        VkDeviceSize BaseOffset = 0U;
        bool Persistent = false;
    };

    struct NativePicaTextureKey {
        // Content identity. Zero means the identity comes from
        // GpuCopyIdentity instead: the samples come from an ordered raw copy
        // produced on the GPU, so there is no CPU payload to hash. Zero with a
        // zero GpuCopyIdentity is the ordinary hash identity. Ownership is
        // never inferred from a missing hash.
        uint64_t ContentHash = 0;
        // Identity of the immutable GPU producer this record samples (see
        // GpuTextureCopySurface). It is assigned once per captured copy and
        // stays stable for that producer's life, so repeated lookups of the
        // same copy reuse one texture record instead of allocating a version
        // per lookup.
        uint64_t GpuCopyIdentity = 0;
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
        Extension::PicaAttachmentRequirements Attachments;
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
        // Global-illumination albedo guide: grown on demand, never part of
        // the target's creation-time storage.
        VkImage DiffuseAlbedoGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory DiffuseAlbedoGuideMemory = VK_NULL_HANDLE;
        VkImageView DiffuseAlbedoGuideView = VK_NULL_HANDLE;
        VkImage MsaaDiffuseAlbedoGuideImage = VK_NULL_HANDLE;
        VkDeviceMemory MsaaDiffuseAlbedoGuideMemory = VK_NULL_HANDLE;
        VkImageView MsaaDiffuseAlbedoGuideView = VK_NULL_HANDLE;
        VkImage ShadowImage = VK_NULL_HANDLE;
        VkDeviceMemory ShadowMemory = VK_NULL_HANDLE;
        VkImageView ShadowView = VK_NULL_HANDLE;
        VkImage DepthImage = VK_NULL_HANDLE;
        VkDeviceMemory DepthMemory = VK_NULL_HANDLE;
        VkImageView DepthView = VK_NULL_HANDLE;
        uint64_t ColorSurfaceGeneration = 0;
        uint64_t DepthSurfaceGeneration = 0;
        // Content provenance: the serial of the last write to this target, and
        // the serial of the write that produced content already carrying its
        // resolved scene effects. They are equal exactly while no other write
        // has happened since, and any draw, clear or fill of this target bumps
        // the first.
        uint64_t ContentWriteSerial = 0;
        uint64_t SceneEffectsResolvedSerial = 0;
        // The content serial whose scene, before native UI, the boundary
        // before UI kept for an upscaler; native UI writes carry it like the
        // resolved serial, any other write ends it.
        uint64_t SceneBeforeUiSerial = 0;
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
        // ActivePicaAttachmentRequirements() selects the active draw bindings.
        VkFramebuffer Framebuffer = VK_NULL_HANDLE;
        VkFramebuffer CanonicalFramebuffer = VK_NULL_HANDLE;
        // Camera of the opaque world geometry drawn into this target during
        // each presented frame (see SamplePerspective).
        Extension::SampleViewAccumulator SampleView;
        // The projection those draws apply (see SamplePerspective).
        Extension::SampleProjectionAccumulator SampleProjection;
        // Gameplay water reaching this target's scene draws, and the frame
        // it was last seen (reflections find water surfaces from it).
        std::shared_ptr<const Renderer3ds::PicaSceneWaterBodies> WaterBodies;
        uint64_t WaterBodiesFrame = 0;
        // Water planes of catalog water without a body, this frame.
        Renderer3ds::PicaSceneWaterBodies WaterPlanes;
        uint64_t WaterPlanesFrame = 0;
        // Whole water draws' pieces this frame (the water map).
        std::vector<std::shared_ptr<const Renderer3ds::PicaWaterTriangles>> WaterPieces;
        std::vector<uint64_t> WaterPieceKeys;
        uint64_t WaterPiecesFrame = 0;
        bool AuxiliaryNeedsClear = false;
        bool WBuffering = false;
        bool NriOwned = false;
        Extension::PicaReflectionEnvironmentAccumulator
            ReflectionEnvironment;
    };

    struct NativePicaDisplayImage {
        uint32_t Width = 0;
        uint32_t Height = 0;
        // The guest output extent of the transfer that produced this image,
        // the extent a savestate stores it at (zero until a transfer wrote it).
        uint32_t GuestWidth = 0;
        uint32_t GuestHeight = 0;
        VkImage Image = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        bool Initialized = false;
        bool NriOwned = false;
        ::Oot3d::Renderer::PicaCompositionDomain CompositionDomain =
            ::Oot3d::Renderer::PicaCompositionDomain::Unknown;
        uint64_t CompositionSequenceId = 0U;
        bool SceneResolved = false;
        // True when this image's colour already carries the resolved scene
        // effects for its boundary, so a later display of it must not re-run
        // the scene stages.
        bool SceneEffectsResolved = false;
        // The same transfer of the scene before native UI was made this frame
        // (mNativePicaSceneBeforeUiDisplays).
        bool SceneBeforeUi = false;
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
    void SubmitFrameCommandBuffer(VkCommandBuffer command, VkFence fence,
                                  VkSemaphore signal = VK_NULL_HANDLE,
                                  uint64_t signalValue = 0U);
    // Device-loss diagnostics: for VK_ERROR_DEVICE_LOST, logs the driver's
    // fault description and the breadcrumb trail once, as warnings and on
    // stderr, before the caller propagates its error. Other results are inert.
    void ReportDeviceLoss(VkResult result, const char* operation);
    // Member code calls this rather than the unit-local helper of the same
    // name: device loss is reported, then the same error is thrown. Const
    // members only query the physical device and surface; they throw directly.
    void CheckVk(VkResult result, const char* operation);
    void CheckVk(VkResult result, const char* operation) const;
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
    // The attachment layout of the sequence being executed. Native-memory
    // execution (the canonical frame and raw-copy dependencies) is never
    // presented and runs no scene effect: it renders colour, depth and
    // stencil only, so its targets carry no guide storage.
    [[nodiscard]] Extension::PicaAttachmentRequirements ActivePicaAttachmentRequirements() const noexcept;
    // The scene camera of the presented sample: the guest projection with the
    // world-to-view the target's geometry was rendered with this frame, or
    // the latest guest camera when the target recorded none.
    [[nodiscard]] std::optional<Extension::PerspectiveViewState> SamplePerspective(
        const NativePicaRenderTarget* target) const;
    void PrepareNativePicaBaseGuides(NativePicaRenderTarget& target);
    void PrepareNativePicaDiffuseAlbedoGuide(NativePicaRenderTarget& target, bool clear);
    void AllocateNativePicaAuxiliaryImage(NativePicaRenderTarget& target, VkFormat format,
                                          VkSampleCountFlagBits samples, VkImage& image,
                                          VkDeviceMemory& memory, VkImageView& view);
    VkFramebuffer NativePicaFramebuffer(NativePicaRenderTarget& target, bool canonical);
    NativePicaDisplayImage& GetOrCreateNativePicaDisplayImage(
        uint64_t renderTargetNamespace, uint32_t physicalAddress,
        uint32_t width, uint32_t height);
    NativePicaDisplayImage& GetOrCreateNativePicaDisplayImage(
        std::map<std::pair<uint64_t, uint32_t>, NativePicaDisplayImage>& images,
        uint64_t renderTargetNamespace, uint32_t physicalAddress,
        uint32_t width, uint32_t height);
    void DestroyNativePicaDisplayImage(NativePicaDisplayImage& image);
    // Destroys the display images replaced while this frame slot recorded,
    // once its fence has completed.
    void ReleaseRetiredNativePicaDisplayImages(uint32_t frameIndex);
    void DestroyNativePicaSceneBeforeUiImages();
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
    // Fills one colour image with a constant, leaving it in its stable layout.
    void ClearNativePicaColorImage(
        VkImage image, const VkClearColorValue& value,
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
    // The output extent the top-screen target is resolved against: the
    // presentation picture (NativePicaOutputExtent), or its native-aspect fit
    // for a title that does not widen.
    [[nodiscard]] Extension::RenderExtent NativeTopScreenOutputExtent() const noexcept {
        const auto output = NativePicaOutputExtent();
        return mNativeTopScreenExtendsToOutput ? output : Extension::FitNativeTopScreenAspect(output);
    }
    void ExecuteNativePicaBeforeDrawAnchors(
        NativePicaRenderTarget& target, const GfxNativePicaDrawView& draw,
        double& shadowMilliseconds, double& grassMilliseconds);
    bool TryRenderInteractiveGrass(
        NativePicaRenderTarget& target,
        const Extension::GraphicsSettings& settings,
        Extension::EffectGeometryProviderInvocationKind invocationKind);
    // The title's world debug lines (light gizmos) over the target's scene.
    void TryRenderWorldDebugLines(NativePicaRenderTarget& target);
    bool TryRenderDirectionalShadowMap(
        NativePicaRenderTarget& target,
        const Extension::GraphicsSettings& settings,
        const Extension::PicaCompositionStageAnchor& anchor);
    bool TryRenderPointShadowMaps(
        NativePicaRenderTarget& target,
        const Extension::GraphicsSettings& settings,
        const Extension::PicaCompositionStageAnchor& anchor);
    void ReleaseEffectGraphImageClients();
    void ApplyInternalResolutionScale(float scale);
    // The output the native PICA targets follow: the presentation picture of
    // the acquired swapchain, or the whole swapchain before a frame resolved
    // one.
    [[nodiscard]] Extension::RenderExtent NativePicaOutputExtent() const noexcept {
        return mPresentationPicture.Valid()
            ? Extension::RenderExtent{mPresentationPicture.Width,
                                  mPresentationPicture.Height}
            : Extension::RenderExtent{mSwapchainExtent.width,
                                  mSwapchainExtent.height};
    }
    void ResetNativePicaRenderTargets(bool preserveDisplayImages = false);
    void ForgetNativePicaEffectNriTextures();
    void ApplyPendingNativePicaMemoryFills(NativePicaRenderTarget& target);
    // Paints the bars around a fitted full-screen picture black, in the open
    // native render scope, before the picture is drawn.
    void PaintNativePicaPictureBars(const Renderer3ds::PicaCanvasSurround& bars);
    void CreateFrameResources();
    void EnsureFrameUniformCapacity(VkDeviceSize bytes, VkDeviceSize alignment);
    // Grows the current frame's vertex/upload arena so `bytes` fit at the
    // next 16-byte offset; returns that offset.
    VkDeviceSize EnsureFrameVertexCapacity(VkDeviceSize bytes);
    static VkDeviceSize InitialFrameVertexArenaBytes();
    // Cacheable geometry uses its per-identity buffer, uploaded once per
    // content version and frame slot; other geometry is copied into the
    // frame's transient arena.
    NativePicaGeometryPlacement PlaceNativePicaGeometry(
        const Extension::PicaGeometryResolveResult& resolution,
        Oot3dVulkanNativePicaCpuTimings* cpuTimings);
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
#if defined(__ANDROID__)
    // Android surface loss (gfx_vulkan_android_surface.cpp): the Activity's
    // window goes away in the background and comes back as a new one. A lost
    // or absent surface is a pause, not an error: no frame is begun until
    // EnsureAndroidSurface has rebuilt the surface and the swapchain.
    bool EnsureAndroidSurface();
    void DropAndroidSurface(const char* reason);
    void NoteAndroidSurfaceLost(const char* where) noexcept;
#endif
    QueueFamilies FindQueueFamilies(VkPhysicalDevice device) const;
    SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice device) const;
    bool DeviceSupportsSwapchain(VkPhysicalDevice device) const;
    VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
    VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    VkFormat FindDepthFormat() const;
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    uint32_t FindReadbackMemoryType(uint32_t typeFilter) const;
    BufferAllocation CreateReadbackBuffer(VkDeviceSize size, bool* coherent);
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
    void DestroyNativePicaPipelines();
    // Thread-safe: preparation threads resolve specialized programs.
    std::vector<uint32_t> ResolveNativePicaShaderSpirv(
        std::string_view source, Extension::PicaAotShaderStage stage,
        bool vertexShader, const char* sourceName);
    void PrepareNativePicaProgram(NativePicaProgramPreparation& preparation);
    // Render thread. Adopts finished background work; with complete set, runs
    // or waits for unfinished work first. Returns whether the program is ready.
    bool AdoptNativePicaProgram(NativePicaShaderProgram& shader, bool complete);
    NativePicaShaderProgram* ResolveUberNativePicaProgram(
        const Extension::PicaShaderPipelineResult& variant, const GfxNativePicaDrawView& draw,
        NativePicaUberUnavailable& reason);
    VkShaderModule CreateShaderModuleFromSpirv(
        std::span<const uint32_t> spirv);
    void EnsureNativePicaVulkanShaderModules(
        const GfxNativePicaDrawView& draw, NativePicaShaderProgram& shader);
    Renderer3ds::PicaDevicePipelineRecord& GetOrCreateNativePicaPipeline(
        const GfxNativePicaDrawView& draw,
        NativePicaShaderProgram& shader, bool writesReactiveMask,
        Extension::PicaShaderDomain domain,
        Extension::PicaShaderInstrumentationFeature requestedFeatures,
        Extension::PicaShaderInstrumentationFeature appliedFeatures,
        bool recordInventory = true, bool outlineOcclusionOnly = false,
        bool requireVulkan = false,
        NativePicaPipelineAcquisition acquisition = NativePicaPipelineAcquisition::Blocking,
        bool* pending = nullptr);
    // `pixels` holds every level, or only the base when `lowerLevels` holds
    // the others (a custom replacement's chain, built apart from its base).
    void CreateNativePicaTextureImage(TextureRecord& texture,
                                      std::span<const uint8_t> pixels,
                                      uint32_t width, uint32_t height,
                                      uint32_t mipLevels, VkFormat format,
                                      bool storageView = false,
                                      std::span<const uint8_t> lowerLevels = {});
    void UploadNativePicaTextureRows(TextureRecord& texture,
                                    std::span<const uint8_t> pixels,
                                    uint32_t width, uint32_t height,
                                    uint32_t level, uint32_t mipLevels,
                                    VkFormat format, uint32_t rowOffset,
                                    uint32_t rowCount);
    void RetireNativePicaTexture(TextureRecord& texture);
    // Retires every texture keyed by another texture-pack generation: lookups
    // use only the frame's generation, so those records are unreachable.
    void RetireNativePicaTexturesOfOtherGenerations(uint64_t generation);
    void RetireNativePicaMaterial(NativePicaMaterialBundle& material);
    NativePicaMaterialBundle* GetOrCreateNativePicaMaterial(
        const std::shared_ptr<const ::Oot3d::Renderer::AzaharPbrMaterial>& material);
    // Allocate (and view) a native PICA texture image without uploading.
    void CreateNativePicaTextureStorage(TextureRecord& texture, uint32_t width,
                                        uint32_t height, uint32_t mipLevels,
                                        VkFormat format, bool storageView);
    // A record that samples a raw copy: no CPU payload is uploaded, the image is
    // filled on the GPU by the copy's own passes.
    TextureRecord* CreateGpuCopiedTextureRecord(
        const NativePicaTextureKey& textureKey,
        const GfxNativePicaTextureView& texture, uint32_t width,
        uint32_t height);
    // Resolves the sampled record and marks it used this frame.
    TextureRecord* GetOrCreateNativePicaTexture(
        const GfxNativePicaTextureView& texture, std::string* error,
        uint64_t replacementGeneration, uint64_t renderTargetNamespace,
        const uint64_t* precomputedContentHash = nullptr);
    TextureRecord* ResolveNativePicaTexture(
        const GfxNativePicaTextureView& texture, std::string* error,
        uint64_t replacementGeneration, uint64_t renderTargetNamespace,
        const uint64_t* precomputedContentHash);
    // Releases cached textures and lighting LUTs no draw sampled lately
    // (Renderer3ds::GpuMemoryResidencyPolicy), sooner while the device-local
    // heap is short of its budget. Records that another owner's lifetime
    // governs (raw-copy samples, live render-target aliases) are left to it.
    void ReleaseIdleNativePicaTextures();
    TextureRecord* GetOrCreateNativePicaLightingLut(
        const ::Oot3d::Renderer::PicaLightingLutView& lightingLut,
        std::string* error);
    VkDescriptorSet AllocateNativePicaDescriptorSet();
    VkDescriptorSet AllocateNativePicaScanoutDescriptorSet();
    std::string BuildVertexShaderSource(const VulkanShaderProgram& shader) const;
    std::string BuildFragmentShaderSource(uint64_t shaderId0, uint64_t shaderId1) const;
    static VulkanShaderProgram BuildShaderProgram(uint64_t shaderId0, uint64_t shaderId1);
    void CreateDepthResources();
    void CreateNoPresentTargets(VkFormat format, VkExtent2D extent);
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
    // The device negotiated graphics pipeline libraries (always-available
    // programs link prepared parts); with TRIAEVUM_NRI_PIPELINE_LIBRARIES=1
    // every native pipeline uses them.
    bool mGraphicsPipelineLibrariesEnabled = false;
    bool mAllNativePipelinesUseLibraries = false;
    // VK_EXT_extended_dynamic_state, enabled whenever offered (libraries or
    // not), so the library policy no longer switches NRI's pipeline path.
    bool mExtendedDynamicStateEnabled = false;
    NativePicaPreparationMode mNativePicaPreparationMode = NativePicaPreparationMode::Synchronous;
    bool mNativePicaProgramsPrepared = false;
    // Startup preparation progress (PrepareNativePicaPrograms parts).
    std::atomic<size_t> mStartupParts{0};
    std::atomic<size_t> mStartupPreparedParts{0};
    // The DLSS features of the last run (renderer cache): created on the first
    // active frame when the settings still select them, so neither the first
    // upscaled frame nor a later native boundary creates one.
    struct DlssFeatureRecord {
        std::vector<Extension::NriDlssFeatureKey> Keys;
        uint32_t SwapchainWidth = 0;
        uint32_t SwapchainHeight = 0;
        bool operator==(const DlssFeatureRecord&) const = default;
    };
    std::optional<DlssFeatureRecord> mDlssFeatureRecord;
    uint64_t mDlssFeatureRecordGeneration = 0;
    std::atomic<bool> mDlssPreparationPending{false};
    void LoadDlssFeatureRecord();
    void PrepareRecordedDlssFeature();
    void StoreDlssFeatureRecord();
    NativePicaPreparationCounters mNativePicaPreparationCounters;
    bool mNativePicaPreparationSummaryLogged = false;
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
    bool mNativeTopScreenExtendsToOutput = true;
    // The presentation picture inside the acquired swapchain: the aspect
    // ratio's letterbox, centred (presentation_picture.h). Native PICA
    // targets follow it and scanout places the display in it.
    Extension::PresentationPicture mPresentationPicture{};
    VkSurfaceCapabilitiesKHR mSwapchainSurfaceCapabilities{};
    VkSurfaceFormatKHR mSwapchainSurfaceFormat{};
    std::vector<VkImage> mSwapchainImages;
    std::vector<VkImageView> mSwapchainImageViews;
    VkFormat mDepthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> mDepthImages;
    std::vector<VkDeviceMemory> mDepthMemories;
    // Developer throughput benchmarks only (TRIAEVUM_BENCHMARK_NO_PRESENT=1):
    // frames render into private images of the surface format and extent and
    // are never presented, so display refresh cannot pace GPU completion.
    bool mBenchmarkNoPresent = false;
    std::vector<VkDeviceMemory> mNoPresentMemories;
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
    bool mFrameAcquirePending = false;
    bool mFrameExternalComputeSplit = false;
    VkSemaphore mFrameExternalWaitSemaphore = VK_NULL_HANDLE;
    uint64_t mFrameExternalWaitValue = 0U;
    std::array<VkSemaphore, kFramesInFlight> mImageAvailableSemaphores{};
    std::vector<VkSemaphore> mRenderFinishedSemaphores;
    std::array<VkFence, kFramesInFlight> mInFlightFences{};
    // Raw TextureCopy submissions are ordered offscreen work: they wait on their
    // own fence instead of idling the whole graphics queue, so unrelated
    // submissions keep executing while the copy's bytes are read back.
    VkFence mRawCopyFence = VK_NULL_HANDLE;
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
#if defined(__ANDROID__)
    // Set by acquire/present (any thread) on VK_ERROR_SURFACE_LOST_KHR.
    std::atomic<bool> mAndroidSurfaceLost{false};
    // The ANativeWindow mSurface was created for; SDL replaces it after the
    // Activity's surface was destroyed and created again.
    void* mAndroidNativeWindow = nullptr;
#endif
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
    std::map<NativePicaShaderModuleKey, NativePicaShaderProgram>
        mCanonicalNativePicaShaders;
    std::map<NativePicaShaderModuleKey, NativePicaShaderProgram>
        mInstrumentedNativePicaShaders;
    // Keyed by the specialized variant and the base of its pair.
    std::map<std::pair<NativePicaShaderModuleKey, std::tuple<uint64_t, uint64_t, uint64_t>>,
             NativePicaShaderProgram>
        mUberNativePicaShaders;
    // The superset program of one parametric base and vertex interface:
    // composed and identified once, shared by every variant of the base.
    struct UberSupersetArtifact {
        std::span<const uint32_t> Fragment;
        Extension::PicaAotShaderSourceIdentity NriSource{};
        bool NriApplied = false;
    };
    std::map<std::tuple<uint64_t, uint64_t, uint64_t, uint8_t>, UberSupersetArtifact>
        mUberSupersetArtifacts;
    Extension::PicaGeometryRegistry mPicaGeometryRegistry;
    bool mPbrSeamsAvailable = false;
    Extension::PicaCompositionSchedule mNativePicaCompositionSchedule;
    Extension::PicaSceneFrame mPicaSceneFrame;
    Extension::NativeSceneView mNativeSceneView;
    // Directional-shadow casters of the published presentation sequence,
    // prepared before it executes: the map reads them at the opaque boundary,
    // and the ordered submission of each draw reuses its prepared bounds.
    Extension::PicaSceneFrame mPreparedShadowCasterFrame;
    Extension::NativeSceneView mPreparedShadowCasterView;
    std::unordered_map<uint64_t, Renderer3ds::VirtualShadowCaster> mPreparedShadowCasters;
    // Accepted caster poses per instance: native geometry identity and the
    // temporal owner part that draws it (see PicaShadowCasterAnchor).
    std::unordered_map<uint64_t, Renderer3ds::PicaShadowCasterAnchor> mShadowCasterAnchors;
    Renderer3ds::PicaShadowCasterAnchor* ShadowCasterAnchor(uint64_t geometryIdentity,
                                                            const Renderer3ds::PicaTemporalIdentity& identity,
                                                            uint64_t submissionId);
    uint64_t mNextShadowCasterAnchorSweep = 0;
    Extension::PicaScenePublicationAdapter mPicaScenePublications;
    Extension::GrassGeometryRegistry mGrassGeometryRegistry;
    Extension::GrassDrawMemo mGrassDrawMemo;
    std::unordered_map<uint64_t, NativePicaGeometryBuffer> mNativePicaGeometryBuffers;
    // Distance field of the native opaque geometry (global illumination).
    Extension::BrixelizerScene mBrixelizerScene;
    // Diffuse GI traced through mBrixelizerScene (declared after it: its
    // contexts use the field's backend and are destroyed first).
    Extension::BrixelizerGiPass mBrixelizerGiPass;
    bool mBrixelizerInitializationAttempted = false;
    bool mSceneDistanceFieldUpdatedThisFrame = false;
    // Title world identity (TitleSceneViewSubmission::WorldIdentity); a change
    // resets the scene distance field and GI before the next field update.
    uint64_t mTitleWorldIdentity = 0;
    bool mTitleWorldChanged = false;
    uint64_t mReportedGiInstances = 0;
    uint64_t mReportedGiResets = 0;
    uint64_t mReportedGiCreationFailures = 0;
    // Screen-space diffuse GI (GlobalIllumination.Mode ScreenSpace): pipelines
    // created off the frame, one pass instance and history per surface.
    Extension::ScreenSpaceGiProvider mScreenSpaceGi;
    uint64_t mScreenSpaceGiWorldIdentity = 0;
    std::array<float, 4> mScreenSpaceGiSourceWeights{};
    Extension::ScreenSpaceGiStats mReportedScreenSpaceGi;
    // The GI provider state last published to the settings runtime (F1).
    Extension::GlobalIlluminationProviderStatus mPublishedGlobalIlluminationStatus;
    void PublishGlobalIlluminationStatus(Extension::GlobalIlluminationMode mode);
    // A planned upscaler wrote no output and the display showed its input
    // unscaled: reported once per provider and cause, and a provider that is
    // no longer available is withdrawn from F1 (its capability).
    void WithdrawFailedUpscaler(Extension::UpscalerProvider provider);
    std::string mReportedUpscalerFallback;
    std::array<std::vector<BufferAllocation>, kFramesInFlight> mRetiredNativePicaGeometryBuffers;
    Extension::PicaShaderPipelineCache mPicaShaderPipelineCache;
    Extension::PicaAotShaderPack mPicaAotShaderPack;
    Renderer::SpirvCache mCompiledShaderCache;
    bool mCompiledShaderCacheSummaryLogged = false;
    Extension::PicaEffectiveShaderInventory mPicaEffectiveShaderInventory;
    // OOT3D_PICA_VARIANT_REQUESTS: every distinct variant request, for offline replay.
    Extension::PicaVariantRequestRecorder mPicaVariantRequestRecorder;
    Extension::PicaGraphicsPipelineInventory mPicaPipelineInventory;
    Extension::PicaGraphicsPipelineManifest mPicaPipelineManifest;
    std::set<uint64_t> mPicaPipelinePrewarmedProfiles;
    std::mutex mPicaAotShaderLogMutex;
    std::set<std::pair<Extension::PicaAotShaderStage, uint64_t>>
        mPicaAotShaderMissesLogged;
    // Distinct pack modules served, for release qualification (counts per
    // identity, not per request).
    std::set<std::pair<Extension::PicaAotShaderStage, uint64_t>> mPicaAotShaderHitIdentities;
    std::atomic<uint64_t> mPicaAotShaderHits = 0;
    std::atomic<uint64_t> mNativeFragmentArtifactHits = 0;
    std::atomic<uint64_t> mNativeVertexArtifactHits = 0;
    std::atomic<uint64_t> mPicaAotShaderMisses = 0;
    bool mPicaAotShaderStrict = false;
    bool mPicaAotShaderSummaryLogged = false;
    bool mPicaPipelinePrewarmSummaryLogged = false;
    bool mPicaPipelinePrewarmEnabled = false;
    uint64_t mPicaPipelinePrewarmCreated = 0U;
    uint64_t mPicaPipelinePrewarmReused = 0U;
    uint64_t mPicaPipelinePrewarmSkipped = 0U;
    Extension::GraphicsSettings mFrameGraphicsSettings;
    Renderer3ds::PicaEnvironmentLighting mFrameEnvironment;
    std::optional<Renderer3ds::PicaSurfaceLightingView> mSurfaceLightingView;
    uint64_t mFrameGraphicsSettingsRevision = 0;
    uint64_t mAzaharConfiguredSettingsRevision = 0;
    uint64_t mFrameMaterialRevision = 0;
    std::optional<Extension::PicaAttachmentFeatureRequests>
        mPicaExtensionRequests;
    Extension::CompiledEffectGraph mPicaExtensionGraph;
    Extension::EffectGeometryProviderPlan mInteractiveGrassProviderPlan;
    Extension::EffectGeometryProviderExecutionLedger
        mInteractiveGrassProviderExecution;
    Extension::EffectPassBarrierPlan mDirectionalShadowMapBarrierPlan;
    ::Fast::Renderer::ExtensionPassSchedulePlan
        mDirectionalShadowSchedulePlan;
    Extension::EffectPassBarrierPlan mPointShadowMapBarrierPlan;
    ::Fast::Renderer::ExtensionPassSchedulePlan
        mPointShadowSchedulePlan;
    uint64_t mFrameAzaharTextureGeneration = 0;
    // The generation whose textures alone remain in mNativePicaTextures; reset
    // when a texture restore may bring back records of another generation.
    std::optional<uint64_t> mNativePicaTexturesGeneration;
    uint64_t mCustomTextureUploadBytesThisFrame = 0;
    std::map<std::vector<uint8_t>, Renderer3ds::PicaDevicePipelineRecord> mNativePicaPipelines;
    uint64_t mNextPicaPipelineId = 1;
    uint64_t mNativePicaVulkanPipelineCreations = 0;
    uint64_t mNativePicaVulkanShaderPairCreations = 0;
    std::map<NativePicaTextureKey, TextureRecord> mNativePicaTextures;
    std::map<uint64_t, NativePicaMaterialEntry> mNativePicaMaterials;
    // The declared sampled source of a raw copy, resolved at the copy's order
    // boundary by the scene compositor: either the renderer-composed scene at
    // the native effect anchor (so the copy inherits the graph's effects instead
    // of bypassing them) or, when no scene effect applies at that boundary, the
    // identity import of the native colour target. The copy's guest bytes always
    // stay the native encode of the colour target, because guest memory is
    // native state.
    struct GpuTextureCopySourceImport {
        VkImage Image = VK_NULL_HANDLE;
        // The sampled source must be RGBA8: the capture pass wraps it with this
        // format and the copy validates it instead of trusting it.
        VkFormat Format = VK_FORMAT_R8G8B8A8_UNORM;
        uint32_t Width = 0;
        uint32_t Height = 0;
        bool ShaderRead = false;
        // The declared encoded domain of the sampled source and whether it
        // already carries this boundary's resolved scene effects. Typed rather
        // than inferred, so nothing decodes the source or applies a scene effect
        // a second time. The identity native target is display-encoded with no
        // effect applied.
        Extension::SceneColorEncoding Encoding = Extension::NativePicaSceneColorEncoding();
        bool SceneEffectsResolved = false;
    };
    // Immutable GPU copies of raw-transfer sources, captured at the copy's
    // order boundary at the source's actual internal extent. This register owns
    // those images: texture records borrow them, never re-create them and never
    // read them back. It replaces the writeback register, which re-implemented
    // the copy's output on the CPU and let the sampler infer ownership from a
    // missing content hash.
    struct GpuTextureCopySurface {
        uint64_t Identity = 0;
        uint64_t RenderTargetNamespace = 0;
        uint64_t CompletionId = 0;
        uint64_t AfterDrawSubmissionId = 0;
        // The declared native tiled surface the copy wrote.
        uint32_t DestinationAddress = 0;
        uint32_t DestinationBytes = 0;
        uint32_t TextureWidth = 0;
        uint32_t TextureHeight = 0;
        uint8_t TextureFormat = 0;
        // The source surface the copy read, in native tiled terms. A consumer
        // maps its own owned byte ranges back through Spans onto this surface.
        uint32_t SourceSurfaceAddress = 0;
        uint32_t SourceWidth = 0;
        uint32_t SourceHeight = 0;
        uint8_t SourceFormat = 0;
        std::vector<Renderer3ds::PicaRawCopySpan> Spans;
        // The source's internal extent: the extent the copy is captured at.
        uint32_t ImageWidth = 0;
        uint32_t ImageHeight = 0;
        // Declared provenance of the sampled source, carried from the composer
        // through to the sampler so nothing re-applies a resolved effect.
        Extension::SceneColorEncoding Encoding = Extension::NativePicaSceneColorEncoding();
        bool SceneEffectsResolved = false;
        VkImage Image = VK_NULL_HANDLE;
        VkImageLayout Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // The frame the copy was captured in, so a sweep never retires a
        // producer the current frame may still reference.
        uint64_t CapturedFrame = 0;
        // The frame a derivation last sampled this copy in, so a sweep never
        // retires an image a command buffer recorded this frame still reads.
        uint64_t LastReferencedFrame = 0;
        // Serialized capture waiting for the next active frame to be uploaded
        // into a fresh image. Only set by a restore; emptied once materialized.
        std::vector<uint8_t> PendingPixels;
        // Native guest bytes must not include optional scene effects. When the
        // sampled capture is canonical, NativeImage aliases Image implicitly.
        VkImage NativeImage = VK_NULL_HANDLE;
        std::vector<uint8_t> MaterializedNativeBytes;
        // Memory ranges hold this pin independently from sampled-plan pins.
        std::shared_ptr<const uint64_t> GuestOwner;
    };
    // The boundary canvas the scene composer materializes and owns.
    Extension::NriOwnedTexture2D mNativePicaSceneCopyCanvas;
    uint32_t mNativePicaSceneCopyCanvasWidth = 0;
    uint32_t mNativePicaSceneCopyCanvasHeight = 0;
    // Retire only at a completed frame fence, never while recorded sampling or
    // deferred guest bytes can still refer to the immutable producer.
    void RetireGpuTextureCopyProducers(uint64_t completedFrame);
    void FlushGpuTextureCopyPrefix();
    bool ReadGpuTextureCopyBytes(uint64_t identity, uint32_t address,
                                 std::span<uint8_t> bytes);
    std::array<uint64_t, kFramesInFlight> mGpuCopySubmittedFrames{};
    std::map<uint64_t, GpuTextureCopySurface> mGpuTextureCopySurfaces;
    // (render target namespace, completion id, order boundary) -> surface
    // identity. The boundary is part of the key because re-executing one copy
    // produces a new immutable version, and a replay of an older plan must
    // resolve to the version its own boundary produced.
    std::map<std::tuple<uint64_t, uint64_t, uint64_t>, uint64_t>
        mGpuTextureCopyProducers;
    // Explicit frontend pins supersede conservative latest-destination retention.
    // Both policies still require completed GPU frames and no deferred guest bytes.
    std::set<std::pair<uint64_t, uint64_t>> mGpuTextureCopyPins;
    bool mGpuTextureCopyPinsDeclared = false;
    std::optional<uint64_t> mCanonicalRenderTargetNamespace;
    // Producer versions travel in the texture-cache snapshot vector, not in a
    // parallel one: an entry with copy-producer metadata carries the full
    // captured RGBA image, and restoring it rebuilds the immutable version so a
    // plan's references resolve again. Upload any staged restored payloads into
    // fresh images before resolving references in an active frame.
    void MaterializeRestoredGpuTextureCopyProducers();
    // Raw copies already consumed (completion id, guest visibility published).
    // Publication may own deferred bytes rather than a RAM memcpy. Repeated
    // execution must neither replace that promise nor signal PPF twice.
    // Bounded by distinct destinations.
    std::map<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, bool>>
        mHandledNativePicaTextureCopies;
    uint64_t mGpuTextureCopySerial = 0;
    // Counts include explicit deferred materializations. Phase timers describe
    // only the eager fallback, not fence waits or deferred CPU visibility.
    // Collected always, printed once at shutdown.
    struct RawCopyTiming {
        uint64_t Copies = 0;
        uint64_t Spans = 0;
        uint64_t InvalidatedTextures = 0;
        uint64_t ReadbackBytes = 0;
        uint64_t WritebackBytes = 0;
        uint64_t DeferredCopies = 0;
        uint64_t Materializations = 0;
        uint64_t PrefixWaits = 0;
        uint64_t PeakLiveProducers = 0;
        double ReadbackSeconds = 0.0;
        double EncodeSeconds = 0.0;
        double InvalidateSeconds = 0.0;
        double SpanSeconds = 0.0;
        double CommitSeconds = 0.0;
    };
    RawCopyTiming mRawCopyTiming;
    bool mRawCopyReadbackCoherent = true;
    // Eager fallback and encode-oracle diagnostics pack native tiled bytes into
    // an R8_UINT image, then host staging. Ordinary deferred copies do not
    // allocate these resources.
    struct RawCopyEncodeResources {
        VkImage Image = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        VkDeviceSize Capacity = 0;
        uint32_t Stride = 0;
        uint32_t Height = 0;
    };
    RawCopyEncodeResources mRawCopyEncodeImage;
    BufferAllocation mRawCopyEncodedBuffer;
    VkSampler mRawCopyEncodeSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout mRawCopyEncodeDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool mRawCopyEncodeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet mRawCopyEncodeDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout mRawCopyEncodePipelineLayout = VK_NULL_HANDLE;
    VkPipeline mRawCopyEncodePipeline = VK_NULL_HANDLE;
    bool mRawCopyEncodeVerify = false;
    uint64_t mRawCopyEncodeVerifyBytes = 0;
    uint64_t mRawCopyEncodeVerifyMismatches = 0;
    // Truthful attribution of the sampled raw-copy path: which draw resolutions
    // are served by an immutable internal-resolution GPU copy, and exactly why
    // the others are not. Collected always, printed once at shutdown.
    struct GpuTextureCopySamplerCost {
        uint64_t Resolutions = 0;
        uint64_t ProducerResolutions = 0;
        uint64_t GpuBound = 0;
        uint64_t GpuBytes = 0;
        uint64_t GpuExtentPixels = 0;
        uint64_t NativeExtentPixels = 0;
        uint64_t FallbackUnknownProducer = 0;
        uint64_t FallbackUncapturedProducer = 0;
        uint64_t FallbackLayout = 0;
        uint64_t FallbackMipChain = 0;
        uint64_t Regions = 0;
        uint64_t ProducerRestores = 0;
        double ResolveSeconds = 0.0;
        double BindSeconds = 0.0;
    };
    GpuTextureCopySamplerCost mGpuTextureCopySamplerCost;
    // Why a view that carries producer references cannot be served from the
    // immutable GPU copy. None of these is allowed to pass as a GPU result.
    enum class GpuTextureCopyFallback : uint8_t {
        None = 0,
        UnknownProducer,
        UncapturedProducer,
        Layout,
        MipChain,
    };
    // A resolved derivation of a raw copy into the texture a native draw
    // samples: the immutable source captures to sample, and the regions of the
    // sampled texture the bytes their producers still own map to.
    struct GpuTextureCopyDerivation {
        struct Group {
            uint64_t SurfaceIdentity = 0;
            std::vector<Renderer3ds::PicaTextureCopyRegion> Regions;
        };
        std::vector<Group> Groups;
        // Every texel of the declared layout is owned by a producer, so the
        // sampled image carries no CPU-owned pixel.
        bool CoversDeclaredLayout = false;
        // Bounding rectangle of every owned region in declared native texels.
        uint32_t OwnedX0 = 0;
        uint32_t OwnedY0 = 0;
        uint32_t OwnedX1 = 0;
        uint32_t OwnedY1 = 0;
        // Destination texels per declared native texel, per axis: at least the
        // producers' internal-per-native ratio, so the sampled image is at
        // internal-or-higher density even for anisotropic or non-integral
        // source scales.
        uint32_t DensityX = 1;
        uint32_t DensityY = 1;
        uint32_t DeclaredWidth = 0;
        uint32_t DeclaredHeight = 0;
        uint32_t ImageWidth = 0;
        uint32_t ImageHeight = 0;
        // Declared provenance of the resolved producers: the encoded domain and
        // whether their captures already carry this boundary's scene effects.
        Extension::SceneColorEncoding Encoding = Extension::NativePicaSceneColorEncoding();
        bool SceneEffectsResolved = false;
        uint64_t FirstProducerCompletionId = 0;
    };
    void PublishGpuTextureCopySurface(GpuTextureCopySurface& surface);
    void DestroyGpuTextureCopySurface(uint64_t identity);
    void DestroyGpuTextureCopySurfaces();
    // Retires the copies a render-target reset invalidates, keeping those whose
    // bytes guest memory still defers to.
    void RetireGpuTextureCopySurfacesForTargetReset();
    // Resolve a view's producer references into the regions to materialize.
    // Returns false, with `fallback` and `error` set, when the bytes cannot be
    // served from the GPU; that is never downgraded to a CPU sample.
    [[nodiscard]] bool BuildGpuTextureCopyDerivation(
        const GfxNativePicaTextureView& texture, uint64_t renderTargetNamespace,
        GpuTextureCopyDerivation& derivation, GpuTextureCopyFallback* fallback,
        std::string* error) const;
    // Record the derivation's regions into the frame command buffer, sampling
    // the immutable captures into the record's image.
    bool RecordGpuTextureCopyDerivation(
        const GpuTextureCopyDerivation& derivation, TextureRecord& record);
    // Expand the native-sized CPU-owned base into the sampled image before the
    // owned regions are overlaid. The base carries no native format semantics:
    // it is already the decoded RGBA8 the CPU path produced.
    bool RecordGpuTextureCopyBaseFill(const GpuTextureCopyDerivation& derivation,
                                      VkImage baseImage,
                                      TextureRecord& record);
    // Identity of the resolved producer set, stable across lookups and restores.
    [[nodiscard]] static uint64_t GpuTextureCopyReferenceIdentity(
        const GfxNativePicaTextureView& texture,
        uint64_t renderTargetNamespace);
    // True when every texture coordinate the draw samples lies inside the owned
    // (producer-covered) rectangle of that resolved copy record, so the pixels it
    // writes are resolved copy pixels rather than the CPU-owned remainder of a
    // padded texture. Requires a float texture coordinate attribute whose values
    // are either normalised or in texel units - both interpretations must accept;
    // anything else answers false instead of guessing.
    [[nodiscard]] bool SampledFootprintInsideOwnedRect(
        const GfxNativePicaDrawView& draw, const TextureRecord& record) const;
    // True when the displayed target's current content is exactly the draw that
    // consumed a copy whose scene effects were already resolved and covered the
    // whole target, so that target's colour already carries them. Provenance is
    // per target and per content write: any later draw, clear or memory fill of
    // that target answers false, so a textured quad or a later world draw on
    // another surface never suppresses scene effects for unrelated content.
    [[nodiscard]] bool NativePicaSceneFullyResolvedForTarget(
        uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress) const;
    // Monotonic serial of content writes, used to invalidate that provenance.
    uint64_t mGpuTextureCopyWriteSerial = 0;
    // Opt-in (TRIAEVUM_GPU_COPY_DUMP) readback of the producer's immutable copy
    // and of the image a consumer actually samples, so the mapping can be
    // asserted from pixels rather than from extents and counters.
    void DumpGpuTextureCopyCapture(const GpuTextureCopySurface& surface);
    void DumpGpuTextureCopyConsumer(
        VkImage sampled, uint32_t sampledWidth, uint32_t sampledHeight,
        const GfxNativePicaTextureView& texture, uint64_t producerCompletionId,
        Extension::SceneColorEncoding encoding, bool sceneEffectsResolved);
    void DestroyRawCopyEncodeResources();
    bool EnsureRawCopySampler(std::string* error);
    bool EnsureRawCopyEncodeResources(uint32_t encodedBytes, std::string* error);
    bool RecordRawCopyEncode(VkCommandBuffer command, VkImage sourceImage,
                             VkImageView sourceView,
                             uint32_t imageWidth, uint32_t imageHeight,
                             uint32_t width, uint32_t height, uint8_t format,
                             uint32_t encodedBytes);
    // Capture this copy's source into an immutable device-local image at the
    // source's internal extent, inside the command buffer the copy is ordered
    // with. No readback, no decode, no re-upload.
    bool CaptureGpuTextureCopySource(const GpuTextureCopySourceImport& source,
                                     GpuTextureCopySurface& surface);
    // Resolve the sampled source of a raw copy at its order boundary. Returns
    // the renderer-composed scene colour when the display graph composes this
    // boundary, and the identity import of the native colour target otherwise.
    // A false return fails the copy: an enabled effect must never fall back to
    // sampling the base colour target, and a composed source whose extent or
    // encoded domain does not match the source's internal extent is an error,
    // not a fallback. `afterDrawSubmissionId` is the copy's real order boundary:
    // the scene this copy samples is composed as of that submission, so a
    // provider anchor the native sequence has not reached is never flushed into
    // the composition. Implemented in the display-graph region.
    bool ComposeNativePicaSceneForCopy(NativePicaRenderTarget& source,
                                       uint64_t afterDrawSubmissionId,
                                       GpuTextureCopySourceImport& output,
                                       std::string* error,
                                       const Renderer3ds::PicaCompositionStageAnchor* beforeUi = nullptr);
    // The display graph body, shared between presentation and the raw copy's
    // scene-source composition so the graph is executed exactly once and in the
    // same order in both cases. `copyOutput` (and the `copySource` it belongs
    // to) is non-null only for a native boundary: the shared body then stops
    // before scanout and declares the sampled source it reached. Presentation
    // passes null pointers. `beforeNativeUi` marks the boundary that commits the
    // scene before native UI, where same-extent AA resolves. Implemented in the
    // display-graph region.
    bool ExecuteNativePicaDisplayGraph(
        const GfxNativePicaDisplayTransferView& transfer,
        const NativePicaDisplayImage& display,
        NativePicaRenderTarget* copySource,
        GpuTextureCopySourceImport* copyOutput, std::string* error,
        bool beforeNativeUi = false);
    bool SubmitNativePicaTextureCopy(const GfxNativePicaDisplayTransferView& transfer, std::string* error);
    // GPU producers a later draw samples without a raw copy: a colour target
    // written by earlier draws, and the tiled result of a DisplayTransfer.
    // Both become immutable identity-mapped producer versions on the raw-copy
    // register, so the existing derivation serves them.
    bool SubmitNativePicaRenderTargetCapture(const GfxNativePicaDisplayTransferView& transfer,
                                             std::string* error);
    void PublishTiledTransferProducer(const GfxNativePicaDisplayTransferView& transfer,
                                      const NativePicaDisplayImage& display);
    bool GpuSurfaceProducerVersionExists(const GfxNativePicaDisplayTransferView& transfer) const;
  public:
    void SetCanonicalPicaRenderTargetNamespace(uint64_t renderTargetNamespace) override {
        mCanonicalRenderTargetNamespace = renderTargetNamespace;
    }
  private:
    std::map<uint64_t, NativePicaLightingLutTexture>
        mNativePicaLightingLuts;
    // Texture-cache residency (ReleaseIdleNativePicaTextures): the policy, the
    // last budget reading and what was released since the last report.
    struct NativePicaTextureResidency {
        Renderer3ds::GpuMemoryResidencyPolicy Policy;
        uint64_t NextSweepFrame = 0;
        uint64_t NextReportFrame = 0;
        bool Pressure = false;
        std::optional<Renderer3ds::VulkanDeviceLocalMemory> DeviceLocal;
        uint64_t ReleasedRecords = 0;
        uint64_t ReleasedBytes = 0;
    };
    NativePicaTextureResidency mNativePicaTextureResidency;
    std::map<NativePicaRenderTargetKey, NativePicaRenderTarget>
        mNativePicaRenderTargets;
    std::map<std::pair<uint64_t, uint32_t>, NativePicaDisplayImage>
        mNativePicaDisplayImages;
    // A display image resized while frames are in flight stays alive until
    // the frame slot that last recorded it completes.
    std::array<std::vector<NativePicaDisplayImage>, kFramesInFlight>
        mRetiredNativePicaDisplayImages;
    // The scene before native UI a target's boundary before UI kept for an
    // upscaler (by colour target), and its transfer for each display.
    std::map<std::pair<uint64_t, uint32_t>, NativePicaDisplayImage>
        mNativePicaSceneBeforeUiTargets;
    std::map<std::pair<uint64_t, uint32_t>, NativePicaDisplayImage>
        mNativePicaSceneBeforeUiDisplays;
    std::map<std::pair<uint64_t, uint32_t>, NativePicaRenderTargetKey>
        mNativePicaDisplayDepthTargets;
    // Framebuffers transferred while they had no target (namespace, color
    // address): presenting them is skipped until a draw recreates the target.
    std::set<std::pair<uint64_t, uint32_t>> mInvalidatedNativePicaRenderTargetAddresses;
    uint64_t mNativePicaTransfersWithoutTarget = 0U;
    NativePicaRenderTarget* mActiveNativePicaRenderTarget = nullptr;
    // The layout the active native PICA pass began with.
    Extension::PicaAttachmentRequirements mActiveNativePicaAttachments;
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
    Renderer3ds::PicaSurfaceCalibrationTrace mSurfaceCalibrationTrace;
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
    bool mPresentationSettingsHeld = false;
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
    std::atomic<bool> mImGuiInitialized{false};
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
    // Bound where a pass declares the geometry guide but the frame has none:
    // its integer views need an integer image (cleared guide, 1x1).
    TextureRecord mGeometryGuideFallback;
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
    Extension::RendererValidationTelemetry mValidationTelemetry;
    Oot3dVulkanValidation mVulkanValidation;

    Oot3dVulkanGpuProfiler mGpuProfiler;
    Renderer3ds::VulkanDeviceFaultReporter mDeviceFault;
    Renderer3ds::VulkanBreadcrumbMechanism mBreadcrumbMechanism = Renderer3ds::VulkanBreadcrumbMechanism::Off;
    Renderer3ds::VulkanBreadcrumbTrail mBreadcrumbs;
    std::atomic<bool> mDeviceLossReported{false};
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
    Extension::D3d12NgxProvider mD3d12NgxProvider;
#endif
    // Destroyed after the NRI objects whose background creation it runs.
    std::unique_ptr<Renderer3ds::PicaPreparationQueue> mPreparationQueue;
    // Persists the NRI pipeline cache while the session runs; stopped before
    // the final store at shutdown.
    Renderer3ds::PipelineCacheFlusher mNriPipelineCacheFlusher;
    // Per-frame vertex/upload arena sizing evidence, summarized at shutdown.
    VkDeviceSize mPeakFrameVertexBytes = 0;
    uint32_t mFrameVertexArenaGrowths = 0;
    // Startup preparation of the always-available parts: threads of its own,
    // so specialized programs never queue behind it.
    std::vector<std::thread> mStartupPreparationThreads;
    void JoinStartupPreparation();
    Extension::NriInteropContext mNriInterop;
    Extension::AmbientOcclusionPass mAmbientOcclusionPass;
    Extension::CacaoPass mCacaoPass;
    Extension::MxaoPass mMxaoPass;
    Extension::FidelityFxSssrPass mFidelityFxSssrPass;
    Extension::HiZDepthPyramidPass mHiZDepthPyramidPass;
    Extension::HiZReflectionPass mHiZReflectionPass;
    Extension::InteractiveGrassPass mInteractiveGrassPass;
    Extension::WorldDebugLinePass mWorldDebugLinePass;
    Extension::LinearSceneColorPass mLinearSceneColorPass;
    Extension::MotionVectorPass mMotionVectorPass;
    Extension::NriDirectionalShadowPass mNriDirectionalShadowPass;
    Extension::NriEffectGraphTransientImageArena
        mNriEffectGraphTransientImageArena;
    Extension::NriPicaDisplayCopyPass mNriPicaDisplayCopyPass;
    // Raw-transfer source capture. A separate instance so the display
    // transfer's per-frame descriptor budget stays independent of raw copies.
    Extension::NriPicaDisplayCopyPass mNriPicaTextureCopyCapturePass;
    // Raw-copy derivation into the sampled texture, likewise independent.
    Extension::NriPicaDisplayCopyPass mNriPicaTextureCopyDerivePass;
    Extension::NriPicaMemoryFillClearPass mNriPicaMemoryFillClearPass;
    Extension::NriPicaScanoutPass mNriPicaScanoutPass;
    Extension::NriPicaPipelineBridge mNriPicaPipelineBridge;
    Extension::NriPicaRenderTargetInitPass mNriPicaRenderTargetInitPass;
    Extension::NriPicaRenderTargetOwner mNriPicaRenderTargetOwner;
    Extension::NriPicaTextureImageOwner mNriPicaDisplayImageOwner;
    Extension::NriPicaTextureImageOwner mNriPicaTextureImageOwner;
    Extension::NriPicaTextureUploadPass mNriPicaTextureUploadPass;
    Extension::NriSwapchain mNriSwapchain;
    Extension::PicaDynamicRenderingScope mPicaDynamicRenderingScope;
    Extension::NriUpscalerPass mNriUpscalerPass;
    Extension::UpscalerGuidePass mUpscalerGuidePass;
    Extension::NativeUiOverlayPass mNativeUiOverlayPass;
    Extension::TemporalHistoryManager mTemporalHistory;
    Extension::SceneCompositePass mSceneCompositePass;
    Extension::Smaa1xPass mSmaa1xPass;
    Extension::ReflectionIblPass mReflectionIblPass;
    Extension::ReflectionMaterialResolvePass
        mReflectionMaterialResolvePass;
    Extension::TemporalAaPass mTemporalAaPass;
    uint64_t mAmbientOcclusionRenderTargetNamespace = 0;
    uint32_t mAmbientOcclusionDisplayPhysicalAddress = 0;
    bool mAmbientOcclusionOutputValid = false;
    bool mAmbientOcclusionExecutedThisFrame = false;
    bool mHiZExecutedThisFrame = false;
    uint64_t mHiZRenderTargetNamespace = 0;
    uint32_t mHiZDisplayPhysicalAddress = 0;
    bool mHiZOutputValid = false;
    std::optional<Extension::SceneSurfaceKey> mHiZReflectionSurface;
    bool mReflectionExecutedThisFrame = false;
    Extension::ReflectionProvider mReflectionProviderThisFrame =
        Extension::ReflectionProvider::Off;
    Extension::SceneColorEncoding mReflectionOutputEncodingThisFrame =
        Extension::SceneColorEncoding::Unknown;
    std::optional<Extension::SceneSurfaceKey> mMotionSurface;
    std::optional<Extension::SceneSurfaceKey> mLinearColorSurface;
    bool mLinearColorExecutedThisFrame = false;
    bool mUpscalerExecutedThisFrame = false;
    bool mUpscalerUsedD3d12ThisFrame = false;
    Extension::UpscalerProvider mUpscalerProviderThisFrame =
        Extension::UpscalerProvider::Nis;
    Extension::SceneColorEncoding mUpscalerOutputEncodingThisFrame =
        Extension::SceneColorEncoding::Unknown;
    bool mUpscalerEffectsCompositedThisFrame = false;
    uint64_t mUpscalerRenderTargetNamespace = 0;
    uint32_t mUpscalerDisplayPhysicalAddress = 0;
    uint32_t mUpscalerOutputWidth = 0;
    uint32_t mUpscalerOutputHeight = 0;
    std::optional<Extension::SceneSurfaceKey> mTaaSurface;
    std::optional<Extension::SceneSurfaceKey> mCompositeSurface;
    bool mCompositeExecutedThisFrame = false;
    Extension::SceneColorEncoding mCompositeOutputEncodingThisFrame =
        Extension::SceneColorEncoding::Unknown;
    bool mTaaOutputValid = false;
    bool mTaaExecutedThisFrame = false;
    Extension::SceneColorEncoding mTaaOutputEncodingThisFrame =
        Extension::SceneColorEncoding::Unknown;
    bool mTaaEffectsCompositedThisFrame = false;
    bool mSmaaExecutedThisFrame = false;
    bool mSmaaEffectsCompositedThisFrame = false;
    Extension::SceneColorEncoding mSmaaOutputEncodingThisFrame =
        Extension::SceneColorEncoding::Unknown;
    uint64_t mSmaaRenderTargetNamespace = 0;
    uint32_t mSmaaDisplayPhysicalAddress = 0;
    uint32_t mSmaaWidth = 0;
    uint32_t mSmaaHeight = 0;
    bool mTemporalMotionEnabled = false;
    bool mTemporalJitterEnabled = false;
    // AA stages that present the world surface are configured this frame.
    bool mPresentationWorldSurface = false;
    // TAA or SMAA is configured: a target's boundary before native UI resolves
    // it into the scene it commits.
    bool mSceneAntiAliasing = false;
    // An upscaler is configured: a target's boundary before native UI keeps
    // the scene it completed (SceneBeforeUi) for it.
    bool mPresentationUpscaler = false;
    // The native UI overlay of this frame's upscaled display (reused with it).
    bool mNativeUiOverlayExecutedThisFrame = false;
    bool mNativeFidelityProfile = false;
    Extension::PicaAttachmentRequirements mFramePicaAttachmentRequirements;
    std::array<float, 2> mTemporalJitterPixels{};
    // Frames that drew the PICA scene: the jitter phase. Frames without the
    // scene (the host holding the guest for its initial preparation) do not
    // advance it, so the images do not depend on how long that took.
    uint64_t mTemporalJitterSequence = 0;
    bool mGrassExecutedThisFrame = false;
    std::set<NativePicaRenderTargetKey>
        mGrassRenderedTargetsThisFrame;
    std::set<NativePicaRenderTargetKey>
        mGrassInsertionAttemptedTargetsThisFrame;
    std::set<NativePicaRenderTargetKey>
        mGrassWorldTargetsThisFrame;
    std::set<std::pair<uint64_t, NativePicaRenderTargetKey>>
        mDirectionalShadowInsertionAttemptedTargetsThisFrame;
    std::set<std::pair<uint64_t, NativePicaRenderTargetKey>>
        mPointShadowInsertionAttemptedTargetsThisFrame;
    std::unordered_map<uint64_t, uint32_t>
        mGrassSurfaceOccurrencesThisFrame;
    std::unordered_map<uint64_t, uint32_t> mGrassOverlayOccurrences;
    std::unordered_map<uint64_t, uint64_t> mGrassDrawOrdinals;
    Renderer3ds::PicaTemporalHistory mPicaTemporalHistory;
    uint32_t mNativePicaDepthWritingDrawsThisFrame = 0;
    Extension::SceneSurfaceRegistry mSceneSurfaces;
    Extension::ResourceStateTracker mResourceStates;
};

} // namespace Fast

#endif
