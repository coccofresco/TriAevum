#pragma once

#include "three_ds_recomp/oot3d/Oot3dNativeRenderer.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fast {
class GfxRenderingAPI;
struct ShaderProgram;
} // namespace Fast

namespace ThreeDsRecomp::Oot3d {

struct Oot3dNativeFast3dRenderConfig {
    Oot3dNativeFast3dRenderConfig();

    Matrix4f WorldToClip;
    Matrix4f ViewToClip;
    bool ViewToClipAvailable = false;
    bool AdjustForBackendClipParameters = true;
    bool FlipTextureV = true;
    bool LinearFilter = true;
    bool EnableAlphaBlend = false;
    bool EnableKankyoPrimitiveRender = true;
    bool ClearDepthBeforeSmallQueue = true;
    size_t MaxTrianglesPerDraw = 256;
};

struct Oot3dNativeFast3dRenderStats {
    size_t TextureUploadCount = 0;
    size_t TextureUploadByteCount = 0;
    size_t TextureMipLevelUploadCount = 0;
    size_t TextureMipLevelUploadByteCount = 0;
    size_t TextureCacheHitCount = 0;
    size_t TextureResidentCount = 0;
    size_t BatchDrawCount = 0;
    size_t BackendDrawCallCount = 0;
    size_t TriangleCount = 0;
    size_t VertexFloatCount = 0;
    size_t PackedVertexCacheHitCount = 0;
    size_t PackedVertexCacheMissCount = 0;
    size_t FrustumCulledBatchCount = 0;
    std::map<std::string, size_t> FrustumCulledModelBatchCounts;
    size_t ShaderSwitchCount = 0;
    size_t MissingTextureBatchCount = 0;
    std::map<std::string, size_t> BackendDrawModelCounts;
    std::map<std::string, size_t> MissingTextureBatchModelCounts;
    size_t SecondaryTextureCoordBatchCount = 0;
    size_t SecondaryTextureBindingCount = 0;
    size_t MissingSecondaryTextureBindingCount = 0;
    size_t TertiaryTextureCoordBatchCount = 0;
    size_t TertiaryTextureBindingCount = 0;
    size_t MissingTertiaryTextureBindingCount = 0;
    size_t NativePicaTexture2BackendUnsupportedBatchCount = 0;
    size_t ShaderInputLayoutMismatchCount = 0;
    size_t NativeBlendStateBatchCount = 0;
    size_t NativeBlendStateAppliedBatchCount = 0;
    size_t NativeBlendStateUnsupportedBatchCount = 0;
    size_t NativeBlendStateBackendAppliedBatchCount = 0;
    size_t NativeBlendStateBackendUnsupportedBatchCount = 0;
    size_t NativeCullStateBatchCount = 0;
    size_t NativeCullStateAppliedBatchCount = 0;
    size_t NativeCullStateBackendUnsupportedBatchCount = 0;
    size_t NativeSamplerStateApplicationCount = 0;
    size_t NativeSamplerStateBackendAppliedCount = 0;
    size_t NativeSamplerStateBackendUnsupportedCount = 0;
    size_t NativePicaAlphaTestDecodedBatchCount = 0;
    size_t NativePicaAlphaTestAppliedBatchCount = 0;
    size_t NativePicaAlphaTestBackendUnsupportedBatchCount = 0;
    size_t NativePicaMaterialLutInputPacketAvailableBatchCount = 0;
    size_t NativePicaMaterialLutInputPacketCompleteBatchCount = 0;
    size_t NativePicaMaterialLutInputFragmentLightingBatchCount = 0;
    size_t NativePicaMaterialLutInputEvaluationPendingBatchCount = 0;
    size_t NativePicaMaterialLutInputEvaluationAppliedBatchCount = 0;
    bool NativePicaFogAvailable = false;
    bool NativePicaFogUsedForRender = false;
    bool NativePicaFogShaderSupported = false;
    bool NativePicaFogLutShaderSupported = false;
    bool NativePicaFogFragmentLutBackendSupported = false;
    bool NativePicaFogEnabled = false;
    std::string NativePicaFogSourceKind;
    std::string NativePicaFogBlockedReason;
    ColorRgba8 NativePicaFogColor = { 0, 0, 0, 255 };
    uint32_t NativePicaFogMode = 0;
    uint32_t NativePicaFogLutWordCount = 0;
    std::vector<uint32_t> NativePicaFogLutWords;
    size_t NativePicaFogFragmentParameterBindBatchCount = 0;
    size_t NativePicaFogFragmentParameterBindFailureBatchCount = 0;
    size_t NativePicaFogDecodedBatchCount = 0;
    size_t NativePicaFogAppliedBatchCount = 0;
    size_t NativePicaFogPendingBatchCount = 0;
    bool NativePicaSelfShadowRouteAvailable = false;
    bool NativePicaSelfShadowShaderRouteDecoded = false;
    bool NativePicaSelfShadowShaderRoutePending = false;
    bool NativePicaSelfShadowShaderEquationSemanticsSupported = false;
    bool NativePicaSelfShadowTextureSamplingSemanticsSupported = false;
    bool NativePicaSelfShadowFullPrimaryLightContributionSupported = false;
    bool NativePicaShadow2dTextureTypeSupported = false;
    bool NativePicaShadow2dBackendPassRequestSupported = false;
    bool NativePicaShadow2dBackendPassRequested = false;
    bool NativePicaShadow2dBackendPassPending = false;
    bool NativePicaShadow2dVisualPassRequestSupported = false;
    bool NativePicaShadow2dVisualPassRequested = false;
    bool NativePicaShadow2dVisualPassPending = false;
    bool NativePicaShadow2dMaterialTextureProjectionInputSupported = false;
    bool NativePicaShadow2dTexCoord0WInputSupported = false;
    bool NativePicaShadow2dEncodedDepthCompareSupported = false;
    bool NativePicaShadow2dProjectionRegisterValuesDecoded = false;
    bool NativePicaShadow2dProjectionRegisterTraceAvailable = false;
    bool NativePicaShadow2dDmpShadowZUniformsDecoded = false;
    bool NativePicaShadow2dPicaTextureShadowRegisterDecoded = false;
    bool NativePicaShadow2dPicaFramebufferShadowRegisterDecoded = false;
    bool NativePicaShadow2dShaderRouteRegisterTraceDecoded = false;
    bool NativePicaShadow2dShaderRouteMatchesPrimaryRgbShadowTerm = false;
    bool NativePicaShadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm = false;
    bool NativePicaShadow2dShadowTextureDimDecoded = false;
    bool NativePicaShadow2dBackendShadowMapRenderTargetDecoded = false;
    bool NativePicaShadow2dBackendShadowMapRenderTargetRequested = false;
    bool NativePicaShadow2dBackendShadowMapRenderTargetAllocated = false;
    bool NativePicaShadow2dBackendShadowMapRenderTargetNativeFormatSupported = false;
    bool NativePicaShadow2dBackendR32uiPipelineSupported = false;
    bool NativePicaShadow2dBackendDepthEncodeShaderSupported = false;
    bool NativePicaShadow2dPrimaryRgbSampleCompareShaderSupported = false;
    bool NativePicaShadow2dVisualPassReady = false;
    bool NativePicaShadow2dVisualPassUsesRuntimeN64AssetSubstitution = true;
    bool NativePicaSelfShadowUsesRuntimeN64AssetSubstitution = false;
    std::string NativePicaSelfShadowLightContributionFormula;
    std::string NativePicaShadow2dBackendPassRequestSource;
    std::string NativePicaShadow2dVisualPassSourceKind;
    std::string NativePicaShadow2dVisualPassRenderTargetFormat;
    std::string NativePicaShadow2dMaterialTextureProjectionInputSource;
    std::string NativePicaShadow2dTexCoord0WInputSource;
    std::string NativePicaShadow2dProjectionRegisterValueSource;
    std::string NativePicaShadow2dProjectionRegisterTraceSourceKind;
    std::string NativePicaShadow2dProjectionRegisterTraceFormat;
    std::string NativePicaShadow2dProjectionRegisterDecodeSource;
    std::string NativePicaShadow2dShaderRouteTraceStatus;
    std::string NativePicaShadow2dBackendShadowMapRenderTargetSource;
    std::string NativePicaShadow2dBackendShadowMapRenderTargetBlockedReason;
    std::string NativePicaShadow2dVisualPassApplication;
    std::string NativePicaShadow2dVisualPassBlockedReason;
    std::string NativePicaShadow2dShadowMapFormat;
    std::string NativePicaShadow2dCompareSource;
    std::string NativePicaShadow2dFilter;
    std::string NativePicaShadow2dZFormula;
    std::string NativePicaShadow2dEncodedDepthDecodeSource;
    std::string NativePicaShadow2dOutOfBoundsResult;
    std::string NativePicaShadow2dFilterInterpolationSource;
    uint32_t NativePicaShadow2dEncodedDepthBits = 0;
    uint32_t NativePicaShadow2dEncodedAlphaBits = 0;
    uint32_t NativePicaShadow2dBiasShift = 0;
    uint32_t NativePicaShadow2dFilterTapCount = 0;
    uint32_t NativePicaShadow2dFilterResultChannelCount = 0;
    uint32_t NativePicaShadow2dTextureShadowCompareBias = 0;
    uint32_t NativePicaShadow2dShadowTextureDimRegisterIndex = 0;
    uint32_t NativePicaShadow2dShadowTextureDimRaw = 0;
    uint32_t NativePicaShadow2dShadowTextureWidth = 0;
    uint32_t NativePicaShadow2dShadowTextureHeight = 0;
    int32_t NativePicaShadow2dBackendShadowMapFramebufferId = -1;
    bool NativePicaShadow2dTextureShadowOrthographic = false;
    double NativePicaShadow2dDmpShadowZBias = 0.0;
    double NativePicaShadow2dDmpShadowZScale = 0.0;
    double NativePicaShadow2dFramebufferShadowConstant = 0.0;
    double NativePicaShadow2dFramebufferShadowLinear = 0.0;
    size_t NativePicaSelfShadowedLightRegisterCount = 0;
    size_t NativePicaSelfShadowCandidateBatchCount = 0;
    size_t NativePicaSelfShadowCandidateVertexCount = 0;
    size_t NativePicaShadow2dMaterialTextureProjectionDecodedBatchCount = 0;
    size_t NativePicaShadow2dTexCoord0WInputDecodedBatchCount = 0;
    size_t NativePicaSelfShadowPendingBatchCount = 0;
    size_t NativePicaSelfShadowAppliedBatchCount = 0;
    size_t NativeKankyoPrimitiveBackendInputSubmitCount = 0;
    size_t NativeKankyoPrimitiveDecodedTextureSubmitCount = 0;
    size_t NativeKankyoPrimitiveMissingTextureSubmitCount = 0;
    size_t NativeKankyoPrimitiveOverlayVertexInputCount = 0;
    size_t NativeKankyoPrimitiveExpectedExpandedVertexCount = 0;
    size_t NativeKankyoPrimitiveRuntime28CQuadLaneVertexInputCount = 0;
    size_t NativeKankyoPrimitiveRuntime28CDrawCount = 0;
    size_t NativeKankyoPrimitiveVisibleDrawCallCount = 0;
    size_t NativeKankyoPrimitiveVisibleTriangleCount = 0;
    size_t NativeKankyoPrimitiveVisibleVertexCount = 0;
    uint32_t NativeKankyoPrimitiveColorPassPrimitiveValue = 0;
    uint32_t NativeKankyoPrimitiveAlphaPassPrimitiveValue = 0;
    std::string NativeKankyoPrimitiveTextureName;
    std::string NativeKankyoPrimitiveTerminalTextureName;
    float NativeKankyoPrimitiveVisibilityTarget = 0.0f;
    float NativeKankyoPrimitiveVisibilityFactor = 0.0f;
    bool NativeKankyoPrimitiveVisibilityScreenGateResolved = false;
    bool NativeKankyoPrimitiveVisibilitySceneOcclusionResolved = false;
    bool NativeKankyoPrimitiveVisibilitySourceInViewport = false;
    bool NativeKankyoPrimitiveVisibilitySourceOccluded = false;
    size_t NativeKankyoPrimitiveVisibilitySuppressedDrawCount = 0;
    bool NativeKankyoPrimitiveTextureHasNativeAlpha = false;
    bool NativeKankyoPrimitivePicaAlphaBlendSemanticsRequired = false;
    bool NativeKankyoPrimitivePicaAlphaBlendSemanticsResolved = false;
    bool NativeKankyoPrimitiveBackendInputResolved = false;
    bool NativeKankyoPrimitiveRuntime28CQuadLaneMaterialized = false;
    bool NativeKankyoPrimitiveRuntime28CPositionProducerResolved = false;
    bool NativeKankyoPrimitiveRuntime28CPositionRuntimeInputResolved = false;
    bool NativeKankyoPrimitivePicaFogDecoded = false;
    bool NativeKankyoPrimitivePicaFogApplied = false;
    bool NativeKankyoPrimitivePicaFogPending = false;
    bool NativeKankyoPrimitiveVisibleDrawPending = false;
    std::string NativeKankyoPrimitiveVisibleDrawBlockedReason;
};

class Oot3dNativeFast3dRenderBackend final : public IOot3dNativeRenderBackend {
  public:
    explicit Oot3dNativeFast3dRenderBackend(Fast::GfxRenderingAPI& renderingApi,
                                            Oot3dNativeFast3dRenderConfig config = {});
    ~Oot3dNativeFast3dRenderBackend() override;

    Oot3dNativeFast3dRenderBackend(const Oot3dNativeFast3dRenderBackend&) = delete;
    Oot3dNativeFast3dRenderBackend& operator=(const Oot3dNativeFast3dRenderBackend&) = delete;

    Oot3dNativeTextureHandle UploadTexture(std::string_view modelName, uint32_t textureIndex,
                                           const Oot3dNativeRenderTexture& texture) override;
    void BeginScene(const Oot3dNativeDemoRenderScene& scene) override;
    void BeginSubmitQueue(Oot3dNativeSubmitQueue queue) override;
    void BeginModel(const Oot3dNativeRenderModel& model) override;
    void DrawBatch(std::string_view modelName, uint32_t batchIndex, const Oot3dNativeRenderBatch& batch,
                   Oot3dNativeTextureHandle textureHandle, const Matrix4f& modelToWorld) override;
    void DrawKankyoPrimitive(const Oot3dNativeKankyoPrimitiveBackendInputState& input,
                             Oot3dNativeTextureHandle textureHandle) override;

    void SetConfig(Oot3dNativeFast3dRenderConfig config);
    void SetRuntimePicaLighting(
        std::optional<Oot3dNativePicaLightingRenderState> runtimeLighting);
    void SetRuntimePicaFog(std::optional<Oot3dNativePicaFogState> runtimeFog);
    void ReleaseCurrentShaderBinding();
    void ReleaseTextures();
    const Oot3dNativeFast3dRenderConfig& Config() const;
    const Oot3dNativeFast3dRenderStats& Stats() const;

  private:
    struct TextureBinding {
        uint32_t RendererTextureId = 0;
        uint16_t Width = 0;
        uint16_t Height = 0;
        uint32_t MipLevelCount = 1;
        bool HasNativeAlpha = false;
    };

    struct TextureCacheKey {
        std::string ModelName;
        std::string TextureName;
        uint32_t TextureIndex = 0;
        uint32_t SourceIndex = 0;
        uint16_t Width = 0;
        uint16_t Height = 0;
        uint16_t TextureFormat = 0;
        uint32_t MipmapCount = 1;
        bool HasNativeAlpha = false;
        size_t Rgba8ByteCount = 0;
        uint64_t Rgba8Hash = 0;

        bool operator<(const TextureCacheKey& other) const;
    };

    struct ShaderKey {
        bool Textured = false;
        bool Alpha = false;
        bool AlphaThreshold = false;
        bool PicaAlphaTest = false;
        bool VertexColorInput = false;
        bool VertexAlphaInput = false;
        bool TextureAlphaMultipliesVertexAlpha = false;
        bool PicaTextureEnvClamp = false;
        bool PicaFog = false;
        bool PicaShadow2dPrimaryRgb = false;
        bool TextureColorAddendInput = false;
        bool TextureColorAddTexture0AlphaInput = false;
        bool TextureColorMultiplierInput = false;
        bool Texture1ColorAddInput = false;
        bool Texture1ColorAddStageLocalPrimaryInput = false;
        bool Texture1ColorAddTexture0AlphaInput = false;
        bool Texture1ColorMultiplyInput = false;
        bool Texture0Texture1AddThenPrimaryColorModulateInput = false;
        bool Texture1Texture2MultiplyAddPreviousInput = false;
        bool Texture0Texture1AddMultiplyTexture0Input = false;

        bool operator<(const ShaderKey& other) const;
    };

    struct PackedVertexChunk {
        uint64_t CacheId = 0;
        size_t TriangleCount = 0;
        std::vector<float> Vertices;
    };

    struct PackedBatchCacheEntry {
        uint64_t StateKey = 0;
        uint64_t ContentVersion = 0;
        uint64_t LastUseSequence = 0;
        std::vector<PackedVertexChunk> Chunks;
    };

    static TextureCacheKey BuildTextureCacheKey(std::string_view modelName, uint32_t textureIndex,
                                                const Oot3dNativeRenderTexture& texture);
    static bool ConfigRequiresTextureRebuild(const Oot3dNativeFast3dRenderConfig& before,
                                             const Oot3dNativeFast3dRenderConfig& after);
    Fast::ShaderProgram* SelectShader(const ShaderKey& key);
    void AppendVertex(std::vector<float>& vbo, const Oot3dNativeRenderVertex& vertex, bool textured,
                      bool secondaryTextureCoordInput, bool tertiaryTextureCoordInput,
                      bool alpha, bool vertexColorInput, bool textureColorAddendInput,
                      ColorRgba8 textureColorAddend, bool textureColorMultiplierInput,
                      Vec3f textureColorMultiplier, Vec3f primaryColorMultiplier,
                      float primaryAlphaMultiplier,
                      bool texture1ColorAddStageLocalPrimaryInput,
                      bool primaryColorUsesTextureEnvMultiplierOnly,
                      bool primaryAlphaUsesTextureEnvMultiplierOnly,
                      bool picaFog, bool picaShadow2dPrimaryRgb,
                      const Matrix4f& modelToWorld, bool backendTransformsPosition);

    Fast::GfxRenderingAPI& mRenderingApi;
    Oot3dNativeFast3dRenderConfig mConfig;
    Oot3dNativeTextureHandle mNextHandle = 1;
    Fast::ShaderProgram* mCurrentShader = nullptr;
    Oot3dNativePicaShadowState mPicaShadow;
    Oot3dNativePicaFogState mPicaFog;
    std::optional<Oot3dNativePicaLightingRenderState> mRuntimePicaLighting;
    std::optional<Oot3dNativePicaFogState> mRuntimePicaFog;
    uint64_t mRuntimePicaLightingStateKey = 0;
    Oot3dNativePicaLightingRenderState mPicaLighting;
    const Oot3dNativeRenderModel* mCurrentModel = nullptr;
    bool mCurrentModelOutsideFrustum = false;
    uint64_t mPicaFogLutStateKey = 0;
    bool mPicaFogFragmentLutBackendSupported = false;
    int mShadow2dFramebufferId = -1;
    uint32_t mShadow2dFramebufferWidth = 0;
    uint32_t mShadow2dFramebufferHeight = 0;
    std::unordered_map<Oot3dNativeTextureHandle, TextureBinding> mTextures;
    std::map<std::pair<std::string, uint32_t>, Oot3dNativeTextureHandle> mModelTextureHandles;
    std::map<TextureCacheKey, Oot3dNativeTextureHandle> mTextureCache;
    std::map<ShaderKey, Fast::ShaderProgram*> mShaders;
    std::map<std::pair<uint64_t, uint32_t>, PackedBatchCacheEntry> mPackedBatchCache;
    std::vector<float> mVertexScratch;
    uint64_t mNextPackedContentVersion = 1;
    uint64_t mPackedCacheUseSequence = 0;
    float mKankyoLensVisibilityFactor = 0.0f;
    Oot3dNativeFast3dRenderStats mStats;
};

nlohmann::json Oot3dNativeFast3dRenderStatsToJson(const Oot3dNativeFast3dRenderStats& stats);

} // namespace ThreeDsRecomp::Oot3d
