#include "three_ds_recomp/oot3d/Oot3dNativeRenderer.h"

#include <utility>

namespace ThreeDsRecomp::Oot3d {
namespace {

bool IsTextureUploadable(const Oot3dNativeRenderTexture& texture) {
    return texture.Rgba8Decoded && texture.Width > 0 && texture.Height > 0 &&
           texture.Rgba8.size() == static_cast<size_t>(texture.Width) * static_cast<size_t>(texture.Height) * 4;
}

bool TextureHasCacheKey(const Oot3dNativeRenderTexture& texture) {
    return texture.Rgba8Decoded && texture.Width > 0 && texture.Height > 0 &&
           texture.Rgba8HashAvailable && texture.Rgba8ByteCount > 0;
}

bool MatrixIsIdentity(const Matrix4f& matrix) {
    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            const float expected = row == col ? 1.0f : 0.0f;
            if (matrix.M[row][col] != expected) {
                return false;
            }
        }
    }
    return true;
}

void AddIssue(Oot3dNativeRendererSubmitResult& result, std::string code, std::string modelName, uint32_t batchIndex,
              std::string message) {
    result.Issues.push_back({ std::move(code), std::move(modelName), batchIndex, std::move(message) });
}

void MergeSubmitResult(Oot3dNativeRendererSubmitResult& destination,
                       const Oot3dNativeRendererSubmitResult& source) {
    destination.ModelCount += source.ModelCount;
    destination.TextureUploadCount += source.TextureUploadCount;
    destination.TextureUploadByteCount += source.TextureUploadByteCount;
    destination.DrawCallCount += source.DrawCallCount;
    destination.TexturedDrawCallCount += source.TexturedDrawCallCount;
    destination.MissingTextureDrawCallCount += source.MissingTextureDrawCallCount;
    destination.AlphaDrawCallCount += source.AlphaDrawCallCount;
    destination.TriangleCount += source.TriangleCount;
    destination.VertexCount += source.VertexCount;
    destination.EffectPrimitiveSubmitCount += source.EffectPrimitiveSubmitCount;
    destination.EffectPrimitiveTextureUploadCount += source.EffectPrimitiveTextureUploadCount;
    destination.EffectPrimitiveMissingTextureCount += source.EffectPrimitiveMissingTextureCount;
    destination.EffectPrimitiveOverlayVertexInputCount += source.EffectPrimitiveOverlayVertexInputCount;
    destination.EffectPrimitiveExpectedExpandedVertexCount += source.EffectPrimitiveExpectedExpandedVertexCount;
    destination.EffectPrimitiveRuntime28CQuadLaneVertexInputCount +=
        source.EffectPrimitiveRuntime28CQuadLaneVertexInputCount;
    destination.EffectPrimitiveRuntime28CDrawCount += source.EffectPrimitiveRuntime28CDrawCount;
    destination.Issues.insert(destination.Issues.end(), source.Issues.begin(), source.Issues.end());
}

Oot3dNativeSubmittedBatch BuildSubmittedBatch(std::string_view modelName, uint32_t batchIndex,
                                              const Oot3dNativeRenderBatch& batch,
                                              Oot3dNativeTextureHandle textureHandle,
                                              const Matrix4f& modelToWorld) {
    return {
        std::string(modelName),
        batchIndex,
        batch.MeshIndex,
        batch.ShapeIndex,
        batch.MaterialIndex,
        batch.PrimitiveIndex,
        batch.SkinningMode,
        batch.Material.Textured,
        batch.Material.TextureIndex,
        textureHandle,
        batch.Material.VertexColorModulatesTexture,
        batch.Material.NativePicaLightingApplied,
        batch.Material.AlphaTest || batch.Material.TextureHasNativeAlpha,
        batch.Material.DepthTest,
        batch.Material.DepthWrite,
        batch.TriangleCount(),
        batch.Vertices.size(),
        !MatrixIsIdentity(modelToWorld),
    };
}

Oot3dNativeSubmittedKankyoPrimitive BuildSubmittedKankyoPrimitive(
    const Oot3dNativeKankyoPrimitiveBackendInputState& input,
    Oot3dNativeTextureHandle textureHandle) {
    Oot3dNativeSubmittedKankyoPrimitive submitted;
    submitted.SourceKind = input.SourceKind;
    submitted.TextureName = input.TextureName;
    submitted.TextureHandle = textureHandle;
    submitted.TerminalTextureName = input.TerminalTextureName;
    submitted.TerminalNativeVertexCount = input.TerminalNativeVertexCount;
    submitted.TerminalSubmitElementCount = input.TerminalSubmitElementCount;
    submitted.TerminalElementInputResolved = input.TerminalElementInputResolved;
    submitted.ColorPassPrimitiveValue = input.ColorPassPrimitiveValue;
    submitted.AlphaPassPrimitiveValue = input.AlphaPassPrimitiveValue;
    submitted.QuadBatchVisibleElementCount = input.QuadBatchVisibleElementCount;
    submitted.QuadBatchExpandedVertexCount = input.QuadBatchExpandedVertexCount;
    submitted.Runtime28CQuadLaneVertexInputCount = input.Runtime28CQuadLaneVertexInputCount;
    submitted.Runtime28CDrawCount = input.Runtime28CDrawCount;
    submitted.Runtime28CQuadLaneMaterialized = input.Runtime28CQuadLaneMaterialized;
    submitted.OverlayPrimitiveVertexCount = input.OverlayPrimitiveVertexCount;
    submitted.OverlayVertexInputCount = input.OverlayVertices.size();
    submitted.DecodedTextureInputResolved = input.DecodedTextureInputResolved;
    return submitted;
}

struct PreparedRenderModel {
    const Oot3dNativeRenderModel* Model = nullptr;
    std::vector<Oot3dNativeTextureHandle> TextureHandles;
};

PreparedRenderModel PrepareRenderModel(const Oot3dNativeRenderModel& model,
                                       IOot3dNativeRenderBackend& backend,
                                       Oot3dNativeRendererSubmitResult& result) {
    PreparedRenderModel prepared;
    prepared.Model = &model;
    prepared.TextureHandles.resize(model.Textures.size(), kInvalidOot3dNativeTextureHandle);
    ++result.ModelCount;

    for (size_t textureIndex = 0; textureIndex < model.Textures.size(); ++textureIndex) {
        const auto& texture = model.Textures[textureIndex];
        if (!IsTextureUploadable(texture) && !TextureHasCacheKey(texture)) {
            continue;
        }

        const auto handle = backend.UploadTexture(model.Name, static_cast<uint32_t>(textureIndex), texture);
        prepared.TextureHandles[textureIndex] = handle;
        ++result.TextureUploadCount;
        result.TextureUploadByteCount +=
            texture.Rgba8ByteCount != 0 ? texture.Rgba8ByteCount : texture.Rgba8.size();
        if (handle == kInvalidOot3dNativeTextureHandle) {
            AddIssue(result, "oot3d_native_texture_upload_failed", model.Name, 0,
                     "render backend returned an invalid texture handle");
        }
    }
    return prepared;
}

bool BatchMatchesNativeCmbSubmitPass(const Oot3dNativeRenderModel& model,
                                     const Oot3dNativeRenderBatch& batch, int pass) {
    if (pass < 0) {
        return true;
    }
    if (!model.NativeCmbMeshPassSplitIndexDecoded) {
        return pass == 0;
    }
    const int batchPass = batch.MeshIndex < model.NativeCmbMeshPassSplitIndex ? 0 : 1;
    return batchPass == pass;
}

void SubmitPreparedRenderModelBatches(const PreparedRenderModel& prepared, int pass,
                                      IOot3dNativeRenderBackend& backend,
                                      Oot3dNativeRendererSubmitResult& result) {
    const auto& model = *prepared.Model;
    backend.BeginModel(model);
    for (size_t batchIndex = 0; batchIndex < model.Batches.size(); ++batchIndex) {
        const auto& batch = model.Batches[batchIndex];
        if (!BatchMatchesNativeCmbSubmitPass(model, batch, pass)) {
            continue;
        }

        Oot3dNativeTextureHandle textureHandle = kInvalidOot3dNativeTextureHandle;
        if (batch.Material.Textured) {
            ++result.TexturedDrawCallCount;
            if (batch.Material.TextureIndex >= 0 &&
                static_cast<size_t>(batch.Material.TextureIndex) < prepared.TextureHandles.size()) {
                textureHandle = prepared.TextureHandles[static_cast<size_t>(batch.Material.TextureIndex)];
            }
            if (textureHandle == kInvalidOot3dNativeTextureHandle) {
                ++result.MissingTextureDrawCallCount;
                AddIssue(result, "oot3d_native_textured_batch_missing_upload", model.Name,
                         static_cast<uint32_t>(batchIndex),
                         "textured OOT3D render batch has no uploaded native texture");
            }
        }
        if (batch.Material.AlphaTest || batch.Material.TextureHasNativeAlpha ||
            batch.Material.NativeBlendStateEnabled) {
            ++result.AlphaDrawCallCount;
        }

        backend.DrawBatch(model.Name, static_cast<uint32_t>(batchIndex), batch, textureHandle,
                          model.ModelToWorld);
        ++result.DrawCallCount;
        result.TriangleCount += batch.TriangleCount();
        result.VertexCount += batch.Vertices.size();
    }
}

Oot3dNativeRendererSubmitResult SubmitNativeCmbModelQueue(
    const std::vector<const Oot3dNativeRenderModel*>& models,
    IOot3dNativeRenderBackend& backend) {
    Oot3dNativeRendererSubmitResult result;
    std::vector<PreparedRenderModel> preparedModels;
    preparedModels.reserve(models.size());
    for (const auto* model : models) {
        preparedModels.push_back(PrepareRenderModel(*model, backend, result));
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& prepared : preparedModels) {
            SubmitPreparedRenderModelBatches(prepared, pass, backend, result);
        }
    }
    result.IsValid = result.Issues.empty();
    return result;
}

} // namespace

void IOot3dNativeRenderBackend::BeginScene(const Oot3dNativeDemoRenderScene&) {
}

void IOot3dNativeRenderBackend::BeginSubmitQueue(Oot3dNativeSubmitQueue) {
}

void IOot3dNativeRenderBackend::BeginModel(const Oot3dNativeRenderModel&) {
}

void IOot3dNativeRenderBackend::DrawKankyoPrimitive(
    const Oot3dNativeKankyoPrimitiveBackendInputState&,
    Oot3dNativeTextureHandle) {
}

Oot3dNativeTextureHandle Oot3dNativeRecordingRenderBackend::UploadTexture(
    std::string_view modelName, uint32_t textureIndex, const Oot3dNativeRenderTexture& texture) {
    const auto handle = mNextHandle++;
    mUploadedTextures.push_back({
        std::string(modelName),
        textureIndex,
        texture.Width,
        texture.Height,
        texture.TextureFormat,
        texture.HasNativeAlpha,
        texture.Rgba8.size(),
        handle,
    });
    return handle;
}

void Oot3dNativeRecordingRenderBackend::DrawBatch(std::string_view modelName, uint32_t batchIndex,
                                                  const Oot3dNativeRenderBatch& batch,
                                                  Oot3dNativeTextureHandle textureHandle,
                                                  const Matrix4f& modelToWorld) {
    mSubmittedBatches.push_back(BuildSubmittedBatch(modelName, batchIndex, batch, textureHandle, modelToWorld));
}

void Oot3dNativeRecordingRenderBackend::DrawKankyoPrimitive(
    const Oot3dNativeKankyoPrimitiveBackendInputState& input,
    Oot3dNativeTextureHandle textureHandle) {
    mSubmittedKankyoPrimitives.push_back(
        BuildSubmittedKankyoPrimitive(input, textureHandle));
}

const std::vector<Oot3dNativeSubmittedTexture>& Oot3dNativeRecordingRenderBackend::UploadedTextures() const {
    return mUploadedTextures;
}

const std::vector<Oot3dNativeSubmittedBatch>& Oot3dNativeRecordingRenderBackend::SubmittedBatches() const {
    return mSubmittedBatches;
}

const std::vector<Oot3dNativeSubmittedKankyoPrimitive>&
Oot3dNativeRecordingRenderBackend::SubmittedKankyoPrimitives() const {
    return mSubmittedKankyoPrimitives;
}

Oot3dNativeRendererSubmitResult SubmitOot3dNativeRenderModel(const Oot3dNativeRenderModel& model,
                                                             IOot3dNativeRenderBackend& backend) {
    Oot3dNativeRendererSubmitResult result;
    const auto prepared = PrepareRenderModel(model, backend, result);
    SubmitPreparedRenderModelBatches(prepared, -1, backend, result);
    result.IsValid = result.Issues.empty();
    return result;
}

Oot3dNativeRendererSubmitResult SubmitOot3dNativeKankyoPrimitiveBackendInput(
    const Oot3dNativeKankyoPrimitiveBackendInputState& input,
    IOot3dNativeRenderBackend& backend) {
    Oot3dNativeRendererSubmitResult result;
    if (!input.ReadyForBackendInput) {
        result.IsValid = true;
        return result;
    }

    result.EffectPrimitiveOverlayVertexInputCount = input.OverlayVertices.size();
    result.EffectPrimitiveExpectedExpandedVertexCount = input.QuadBatchExpandedVertexCount;
    result.EffectPrimitiveRuntime28CQuadLaneVertexInputCount =
        input.Runtime28CQuadLaneVertexInputCount;
    result.EffectPrimitiveRuntime28CDrawCount = input.Runtime28CDrawCount;
    if (!input.DecodedTextureInputResolved || !IsTextureUploadable(input.Texture)) {
        ++result.EffectPrimitiveMissingTextureCount;
        AddIssue(result, "oot3d_native_kankyo_primitive_missing_decoded_ctxb_texture",
                 "oot3d_kankyo_primitive", 0,
                 "resolved native kankyo primitive input has no decoded CTXB texture payload");
        result.IsValid = false;
        return result;
    }
    const auto textureHandle =
        backend.UploadTexture("oot3d_kankyo_primitive", 0, input.Texture);
    ++result.TextureUploadCount;
    ++result.EffectPrimitiveTextureUploadCount;
    result.TextureUploadByteCount += input.Texture.Rgba8.size();
    if (textureHandle == kInvalidOot3dNativeTextureHandle) {
        ++result.EffectPrimitiveMissingTextureCount;
        AddIssue(result, "oot3d_native_kankyo_primitive_texture_upload_failed",
                 "oot3d_kankyo_primitive", 0,
                 "render backend returned an invalid handle for the native kankyo CTXB texture");
        result.IsValid = false;
        return result;
    }

    backend.DrawKankyoPrimitive(input, textureHandle);
    ++result.EffectPrimitiveSubmitCount;
    result.IsValid = true;
    return result;
}

Oot3dNativeRendererSubmitResult SubmitOot3dNativeDemoRenderScene(const Oot3dNativeDemoRenderScene& scene,
                                                                 IOot3dNativeRenderBackend& backend) {
    return SubmitOot3dNativeDemoRenderScene(
        scene, backend, std::span<const Oot3dNativeEnvironmentOverlay>{});
}

Oot3dNativeRendererSubmitResult SubmitOot3dNativeDemoRenderScene(
    const Oot3dNativeDemoRenderScene& scene, IOot3dNativeRenderBackend& backend,
    std::span<const Oot3dNativeRenderModel* const> environmentOverlays) {
    std::vector<Oot3dNativeEnvironmentOverlay> descriptors;
    descriptors.reserve(environmentOverlays.size());
    for (const auto* model : environmentOverlays) {
        descriptors.push_back({ model, nullptr, nullptr, 0.0f });
    }
    return SubmitOot3dNativeDemoRenderScene(scene, backend, descriptors);
}

Oot3dNativeRendererSubmitResult SubmitOot3dNativeDemoRenderScene(
    const Oot3dNativeDemoRenderScene& scene, IOot3dNativeRenderBackend& backend,
    std::span<const Oot3dNativeEnvironmentOverlay> environmentOverlays) {
    Oot3dNativeRendererSubmitResult result;
    backend.BeginScene(scene);
    const auto collectEnvironmentModels = [&](Oot3dNativeSubmitQueue queue) {
        std::vector<const Oot3dNativeRenderModel*> models;
        for (const auto& environmentModel : scene.EnvironmentModels) {
            if (environmentModel.SubmitQueue == queue &&
                (environmentModel.NativeKankyoRole == Oot3dNativeKankyoModelRole::SkyBackground ||
                 environmentModel.NativeKankyoRole == Oot3dNativeKankyoModelRole::None)) {
                models.push_back(&environmentModel);
            }
        }
        for (const auto& environmentModel : scene.EnvironmentModels) {
            if (environmentModel.SubmitQueue == queue &&
                environmentModel.NativeKankyoRole == Oot3dNativeKankyoModelRole::Star) {
                models.push_back(&environmentModel);
            }
        }
        for (const auto& moonModel : scene.MoonModels) {
            if (moonModel.SubmitQueue == queue) {
                models.push_back(&moonModel);
            }
        }
        for (const auto role : {
                 Oot3dNativeKankyoModelRole::Sun,
                 Oot3dNativeKankyoModelRole::Cloud,
                 Oot3dNativeKankyoModelRole::SunHalo,
             }) {
            for (const auto& environmentModel : scene.EnvironmentModels) {
                if (environmentModel.SubmitQueue == queue && environmentModel.NativeKankyoRole == role) {
                    models.push_back(&environmentModel);
                }
            }
        }
        return models;
    };
    const auto collectSceneModels = [&](Oot3dNativeSubmitQueue queue) {
        std::vector<const Oot3dNativeRenderModel*> models;
        if (scene.Room.SubmitQueue == queue) {
            models.push_back(&scene.Room);
        }
        for (const auto& roomModel : scene.AdditionalRoomModels) {
            if (roomModel.SubmitQueue == queue) {
                models.push_back(&roomModel);
            }
        }
        for (const auto& actorVisual : scene.ActorVisuals) {
            if (actorVisual.SubmitQueue == queue) {
                models.push_back(&actorVisual);
            }
        }
        if (scene.Link.SubmitQueue == queue) {
            models.push_back(&scene.Link);
        }
        return models;
    };
    backend.BeginSubmitQueue(Oot3dNativeSubmitQueue::Primary);
    const auto submitOverlay = [&](const Oot3dNativeEnvironmentOverlay& overlay,
                                   Oot3dNativeSubmitQueue queue) {
        if (overlay.Model == nullptr || overlay.Model->SubmitQueue != queue) {
            return;
        }
        if (overlay.SourceModel != nullptr && overlay.MaterialAnimations != nullptr &&
            !overlay.MaterialAnimations->empty()) {
            auto animated = *overlay.Model;
            ApplyOot3dNativeRenderModelMaterialAnimationFrame(
                animated, *overlay.SourceModel, *overlay.MaterialAnimations,
                overlay.MaterialAnimationFrame);
            MergeSubmitResult(result, SubmitOot3dNativeRenderModel(animated, backend));
        } else {
            MergeSubmitResult(result, SubmitOot3dNativeRenderModel(*overlay.Model, backend));
        }
    };
    for (const auto& environmentOverlay : environmentOverlays) {
        submitOverlay(environmentOverlay, Oot3dNativeSubmitQueue::Primary);
    }
    for (const auto* environmentModel :
         collectEnvironmentModels(Oot3dNativeSubmitQueue::Primary)) {
        MergeSubmitResult(result, SubmitOot3dNativeRenderModel(*environmentModel, backend));
    }
    MergeSubmitResult(
        result,
        SubmitOot3dNativeKankyoPrimitiveBackendInput(
            scene.EnvironmentBackground.NativeKankyoLensEffect.PrimitiveBackendInput,
            backend));
    MergeSubmitResult(result,
                      SubmitNativeCmbModelQueue(
                          collectSceneModels(Oot3dNativeSubmitQueue::Primary), backend));
    backend.BeginSubmitQueue(Oot3dNativeSubmitQueue::Small);
    for (const auto& environmentOverlay : environmentOverlays) {
        submitOverlay(environmentOverlay, Oot3dNativeSubmitQueue::Small);
    }
    for (const auto* environmentModel :
         collectEnvironmentModels(Oot3dNativeSubmitQueue::Small)) {
        MergeSubmitResult(result, SubmitOot3dNativeRenderModel(*environmentModel, backend));
    }
    MergeSubmitResult(result,
                      SubmitNativeCmbModelQueue(
                          collectSceneModels(Oot3dNativeSubmitQueue::Small), backend));
    result.IsValid = result.Issues.empty();
    return result;
}

nlohmann::json Oot3dNativeRendererSubmitResultToJson(const Oot3dNativeRendererSubmitResult& result) {
    nlohmann::json issues = nlohmann::json::array();
    for (const auto& issue : result.Issues) {
        issues.push_back({
            { "code", issue.Code },
            { "model_name", issue.ModelName },
            { "batch_index", issue.BatchIndex },
            { "message", issue.Message },
        });
    }

    return {
        { "status", result.IsValid ? "valid" : "invalid" },
        { "model_count", result.ModelCount },
        { "texture_upload_count", result.TextureUploadCount },
        { "texture_upload_byte_count", result.TextureUploadByteCount },
        { "draw_call_count", result.DrawCallCount },
        { "textured_draw_call_count", result.TexturedDrawCallCount },
        { "missing_texture_draw_call_count", result.MissingTextureDrawCallCount },
        { "alpha_draw_call_count", result.AlphaDrawCallCount },
        { "triangle_count", result.TriangleCount },
        { "vertex_count", result.VertexCount },
        { "effect_primitive_submit_count", result.EffectPrimitiveSubmitCount },
        { "effect_primitive_texture_upload_count", result.EffectPrimitiveTextureUploadCount },
        { "effect_primitive_missing_texture_count", result.EffectPrimitiveMissingTextureCount },
        { "effect_primitive_overlay_vertex_input_count",
          result.EffectPrimitiveOverlayVertexInputCount },
        { "effect_primitive_expected_expanded_vertex_count",
          result.EffectPrimitiveExpectedExpandedVertexCount },
        { "effect_primitive_runtime28c_quad_lane_vertex_input_count",
          result.EffectPrimitiveRuntime28CQuadLaneVertexInputCount },
        { "effect_primitive_runtime28c_draw_count",
          result.EffectPrimitiveRuntime28CDrawCount },
        { "issue_count", result.Issues.size() },
        { "issues", issues },
    };
}

} // namespace ThreeDsRecomp::Oot3d
