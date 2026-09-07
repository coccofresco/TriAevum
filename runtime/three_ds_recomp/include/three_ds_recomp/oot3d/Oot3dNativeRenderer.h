#pragma once

#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <span>
#include <vector>

namespace ThreeDsRecomp::Oot3d {

using Oot3dNativeTextureHandle = uint64_t;
constexpr Oot3dNativeTextureHandle kInvalidOot3dNativeTextureHandle = 0;

struct Oot3dNativeRendererIssue {
    std::string Code;
    std::string ModelName;
    uint32_t BatchIndex = 0;
    std::string Message;
};

struct Oot3dNativeSubmittedTexture {
    std::string ModelName;
    uint32_t TextureIndex = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint16_t TextureFormat = 0;
    bool HasNativeAlpha = false;
    size_t Rgba8ByteCount = 0;
    Oot3dNativeTextureHandle Handle = kInvalidOot3dNativeTextureHandle;
};

struct Oot3dNativeSubmittedBatch {
    std::string ModelName;
    uint32_t BatchIndex = 0;
    uint32_t MeshIndex = 0;
    uint32_t ShapeIndex = 0;
    int32_t MaterialIndex = -1;
    uint32_t PrimitiveIndex = 0;
    uint16_t SkinningMode = 0;
    bool Textured = false;
    int32_t TextureIndex = -1;
    Oot3dNativeTextureHandle TextureHandle = kInvalidOot3dNativeTextureHandle;
    bool VertexColorModulatesTexture = false;
    bool NativePicaLightingApplied = false;
    bool AlphaTest = false;
    bool DepthTest = true;
    bool DepthWrite = true;
    size_t TriangleCount = 0;
    size_t VertexCount = 0;
    bool UsesModelTransform = false;
};

struct Oot3dNativeSubmittedKankyoPrimitive {
    std::string SourceKind;
    std::string TextureName;
    Oot3dNativeTextureHandle TextureHandle = kInvalidOot3dNativeTextureHandle;
    std::string TerminalTextureName;
    uint32_t TerminalNativeVertexCount = 0;
    uint32_t TerminalSubmitElementCount = 0;
    bool TerminalElementInputResolved = false;
    uint32_t ColorPassPrimitiveValue = 0;
    uint32_t AlphaPassPrimitiveValue = 0;
    uint32_t QuadBatchVisibleElementCount = 0;
    uint32_t QuadBatchExpandedVertexCount = 0;
    uint32_t Runtime28CQuadLaneVertexInputCount = 0;
    uint32_t Runtime28CDrawCount = 0;
    bool Runtime28CQuadLaneMaterialized = false;
    uint32_t OverlayPrimitiveVertexCount = 0;
    size_t OverlayVertexInputCount = 0;
    bool DecodedTextureInputResolved = false;
};

struct Oot3dNativeRendererSubmitResult {
    bool IsValid = false;
    size_t ModelCount = 0;
    size_t TextureUploadCount = 0;
    size_t TextureUploadByteCount = 0;
    size_t DrawCallCount = 0;
    size_t TexturedDrawCallCount = 0;
    size_t MissingTextureDrawCallCount = 0;
    size_t AlphaDrawCallCount = 0;
    size_t TriangleCount = 0;
    size_t VertexCount = 0;
    size_t EffectPrimitiveSubmitCount = 0;
    size_t EffectPrimitiveTextureUploadCount = 0;
    size_t EffectPrimitiveMissingTextureCount = 0;
    size_t EffectPrimitiveOverlayVertexInputCount = 0;
    size_t EffectPrimitiveExpectedExpandedVertexCount = 0;
    size_t EffectPrimitiveRuntime28CQuadLaneVertexInputCount = 0;
    size_t EffectPrimitiveRuntime28CDrawCount = 0;
    std::vector<Oot3dNativeRendererIssue> Issues;
};

struct Oot3dNativeEnvironmentOverlay {
    const Oot3dNativeRenderModel* Model = nullptr;
    const CmbModel* SourceModel = nullptr;
    const std::vector<CmabMaterialAnimation>* MaterialAnimations = nullptr;
    float MaterialAnimationFrame = 0.0f;
};

class IOot3dNativeRenderBackend {
  public:
    virtual ~IOot3dNativeRenderBackend() = default;
    virtual void BeginScene(const Oot3dNativeDemoRenderScene& scene);
    virtual void BeginSubmitQueue(Oot3dNativeSubmitQueue queue);
    virtual void BeginModel(const Oot3dNativeRenderModel& model);
    virtual Oot3dNativeTextureHandle UploadTexture(std::string_view modelName, uint32_t textureIndex,
                                                   const Oot3dNativeRenderTexture& texture) = 0;
    virtual void DrawBatch(std::string_view modelName, uint32_t batchIndex, const Oot3dNativeRenderBatch& batch,
                           Oot3dNativeTextureHandle textureHandle, const Matrix4f& modelToWorld) = 0;
    virtual void DrawKankyoPrimitive(const Oot3dNativeKankyoPrimitiveBackendInputState& input,
                                     Oot3dNativeTextureHandle textureHandle);
};

class Oot3dNativeRecordingRenderBackend final : public IOot3dNativeRenderBackend {
  public:
    Oot3dNativeTextureHandle UploadTexture(std::string_view modelName, uint32_t textureIndex,
                                           const Oot3dNativeRenderTexture& texture) override;
    void DrawBatch(std::string_view modelName, uint32_t batchIndex, const Oot3dNativeRenderBatch& batch,
                   Oot3dNativeTextureHandle textureHandle, const Matrix4f& modelToWorld) override;
    void DrawKankyoPrimitive(const Oot3dNativeKankyoPrimitiveBackendInputState& input,
                             Oot3dNativeTextureHandle textureHandle) override;

    const std::vector<Oot3dNativeSubmittedTexture>& UploadedTextures() const;
    const std::vector<Oot3dNativeSubmittedBatch>& SubmittedBatches() const;
    const std::vector<Oot3dNativeSubmittedKankyoPrimitive>& SubmittedKankyoPrimitives() const;

  private:
    Oot3dNativeTextureHandle mNextHandle = 1;
    std::vector<Oot3dNativeSubmittedTexture> mUploadedTextures;
    std::vector<Oot3dNativeSubmittedBatch> mSubmittedBatches;
    std::vector<Oot3dNativeSubmittedKankyoPrimitive> mSubmittedKankyoPrimitives;
};

Oot3dNativeRendererSubmitResult SubmitOot3dNativeRenderModel(const Oot3dNativeRenderModel& model,
                                                             IOot3dNativeRenderBackend& backend);
Oot3dNativeRendererSubmitResult SubmitOot3dNativeDemoRenderScene(const Oot3dNativeDemoRenderScene& scene,
                                                                 IOot3dNativeRenderBackend& backend);
Oot3dNativeRendererSubmitResult SubmitOot3dNativeDemoRenderScene(
    const Oot3dNativeDemoRenderScene& scene, IOot3dNativeRenderBackend& backend,
    std::span<const Oot3dNativeRenderModel* const> environmentOverlays);
Oot3dNativeRendererSubmitResult SubmitOot3dNativeDemoRenderScene(
    const Oot3dNativeDemoRenderScene& scene, IOot3dNativeRenderBackend& backend,
    std::span<const Oot3dNativeEnvironmentOverlay> environmentOverlays);
nlohmann::json Oot3dNativeRendererSubmitResultToJson(const Oot3dNativeRendererSubmitResult& result);

} // namespace ThreeDsRecomp::Oot3d
