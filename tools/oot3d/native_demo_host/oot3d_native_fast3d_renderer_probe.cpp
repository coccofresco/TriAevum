#include <cmath>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "fast/backends/gfx_rendering_api.h"
#include "fast/interpreter.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeFast3dRenderer.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

namespace {

struct Args {
    std::filesystem::path ManifestPath;
    std::filesystem::path OutputPath;
};

struct FakeShaderRecord {
    uint64_t ShaderId0 = 0;
    uint64_t ShaderId1 = 0;
    uint8_t NumInputs = 0;
    bool UsedTextures[2] = {};
    bool UsesAlpha = false;
    bool UsesOot3dShadow2d = false;
};

struct FakeApiStats {
    size_t TextureCreateCount = 0;
    size_t TextureSelectCount = 0;
    size_t TextureUploadCount = 0;
    size_t TextureUploadByteCount = 0;
    size_t SamplerParameterCount = 0;
    size_t ShaderCreateCount = 0;
    size_t ShaderLoadCount = 0;
    size_t ShaderUnloadCount = 0;
    size_t DrawTrianglesCallCount = 0;
    size_t DrawTrianglesTriangleCount = 0;
    size_t DrawTrianglesFloatCount = 0;
    size_t MaxTrianglesPerDraw = 0;
    size_t FramebufferCreateCount = 0;
    size_t FramebufferUpdateCount = 0;
    uint32_t LastFramebufferWidth = 0;
    uint32_t LastFramebufferHeight = 0;
    std::string LastFramebufferColorFormat;
};

bool ParseArgs(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--manifest" && i + 1 < argc) {
            args.ManifestPath = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            args.OutputPath = argv[++i];
        } else {
            return false;
        }
    }
    return !args.ManifestPath.empty() && !args.OutputPath.empty();
}

void PrintUsage() {
    std::cerr << "usage: oot3d_native_fast3d_renderer_probe --manifest <demo_manifest.json> "
                 "--output <summary.json>\n";
}

void WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write JSON file: " + path.string());
    }
    file << data.dump(2) << "\n";
}

Fast::ShaderProgram* AsShaderProgram(FakeShaderRecord* shader) {
    return reinterpret_cast<Fast::ShaderProgram*>(shader);
}

const FakeShaderRecord* AsFakeShaderRecord(Fast::ShaderProgram* shader) {
    return reinterpret_cast<const FakeShaderRecord*>(shader);
}

bool ShaderIdUsesTexture(uint64_t shaderId0, uint32_t textureToken, uint32_t textureAlphaToken) {
    for (uint32_t shift = 0; shift < 64; shift += 4) {
        const uint32_t value = static_cast<uint32_t>((shaderId0 >> shift) & 0xf);
        if (value == textureToken || value == textureAlphaToken) {
            return true;
        }
    }
    return false;
}

bool ShaderIdUsesOot3dShadow2d(uint64_t shaderId1) {
    return Fast::ShaderIdUnmask(shaderId1) == Fast::SHADER_ID_PICA_TEXTURE_ENV_SHADOW2D;
}

uint8_t ShaderIdNumInputs(uint64_t shaderId0) {
    uint32_t maxInput = 0;
    for (uint32_t shift = 0; shift < 64; shift += 4) {
        const uint32_t value = static_cast<uint32_t>((shaderId0 >> shift) & 0xf);
        if (value >= SHADER_INPUT_1 && value <= SHADER_INPUT_7) {
            maxInput = std::max(maxInput, value);
        }
    }
    return maxInput == 0 ? 0 : static_cast<uint8_t>(maxInput - SHADER_INPUT_1 + 1);
}

size_t ShaderVertexStride(const FakeShaderRecord& shader) {
    size_t stride = 4;
    if (shader.UsedTextures[0]) {
        stride += 2;
    }
    if (shader.UsedTextures[1]) {
        stride += 2;
    }
    stride += static_cast<size_t>(shader.NumInputs) * (shader.UsesAlpha ? 4 : 3);
    if (shader.UsesOot3dShadow2d) {
        stride += 3;
    }
    return stride;
}

class FakeFast3dRenderingApi final : public Fast::GfxRenderingAPI {
  public:
    const char* GetName() override {
        return "FakeFast3D";
    }

    int GetMaxTextureSize() override {
        return 16384;
    }

    Fast::GfxClipParameters GetClipParameters() override {
        return { false, false };
    }

    void UnloadShader(Fast::ShaderProgram* oldPrg) override {
        if (oldPrg != nullptr) {
            ++Stats.ShaderUnloadCount;
        }
        if (mCurrentShader == oldPrg) {
            mCurrentShader = nullptr;
        }
    }

    void LoadShader(Fast::ShaderProgram* newPrg) override {
        if (newPrg == nullptr) {
            throw std::runtime_error("attempted to load a null shader");
        }
        ++Stats.ShaderLoadCount;
        mCurrentShader = newPrg;
    }

    void ClearShaderCache() override {
        mShaders.clear();
        mCurrentShader = nullptr;
    }

    Fast::ShaderProgram* CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) override {
        auto* shader = LookupShader(shaderId0, shaderId1);
        if (shader != nullptr) {
            LoadShader(shader);
            return shader;
        }

        auto record = std::make_unique<FakeShaderRecord>();
        record->ShaderId0 = shaderId0;
        record->ShaderId1 = shaderId1;
        record->NumInputs = ShaderIdNumInputs(shaderId0);
        record->UsedTextures[0] = ShaderIdUsesTexture(shaderId0, SHADER_TEXEL0, SHADER_TEXEL0A);
        record->UsedTextures[1] = ShaderIdUsesTexture(shaderId0, SHADER_TEXEL1, SHADER_TEXEL1A);
        record->UsesAlpha = (shaderId1 & SHADER_OPT(ALPHA)) != 0;
        record->UsesOot3dShadow2d = ShaderIdUsesOot3dShadow2d(shaderId1);

        const auto key = std::make_pair(shaderId0, shaderId1);
        auto* shaderPtr = AsShaderProgram(record.get());
        mShaders.emplace(key, std::move(record));
        ++Stats.ShaderCreateCount;
        LoadShader(shaderPtr);
        return shaderPtr;
    }

    Fast::ShaderProgram* LookupShader(uint64_t shaderId0, uint64_t shaderId1) override {
        const auto it = mShaders.find(std::make_pair(shaderId0, shaderId1));
        return it == mShaders.end() ? nullptr : AsShaderProgram(it->second.get());
    }

    void ShaderGetInfo(Fast::ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) override {
        const auto* shader = AsFakeShaderRecord(prg);
        *numInputs = shader->NumInputs;
        usedTextures[0] = shader->UsedTextures[0];
        usedTextures[1] = shader->UsedTextures[1];
    }

    uint32_t NewTexture() override {
        const uint32_t id = static_cast<uint32_t>(++Stats.TextureCreateCount);
        mTextures[id] = {};
        return id;
    }

    void SelectTexture(int tile, uint32_t textureId) override {
        ++Stats.TextureSelectCount;
        mCurrentTextureIds[tile] = textureId;
    }

    void UploadTexture(const uint8_t*, uint32_t width, uint32_t height) override {
        if (width == 0 || height == 0) {
            throw std::runtime_error("attempted to upload an empty texture");
        }
        ++Stats.TextureUploadCount;
        Stats.TextureUploadByteCount += static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    }

    void SetSamplerParameters(int, bool, uint32_t, uint32_t) override {
        ++Stats.SamplerParameterCount;
    }

    void SetDepthTestAndMask(bool depthTest, bool depthMask) override {
        mDepthTest = depthTest;
        mDepthMask = depthMask;
    }

    void SetZmodeDecal(bool decal) override {
        mZmodeDecal = decal;
    }

    void SetViewport(int, int, int, int) override {
    }

    void SetScissor(int, int, int, int) override {
    }

    void SetUseAlpha(bool useAlpha) override {
        mUseAlpha = useAlpha;
    }

    void DrawTriangles(float bufVbo[], size_t bufVboLen, size_t bufVboNumTris) override {
        if (mCurrentShader == nullptr) {
            throw std::runtime_error("draw submitted without a loaded shader");
        }
        const auto* shader = AsFakeShaderRecord(mCurrentShader);
        const size_t expectedLen = ShaderVertexStride(*shader) * bufVboNumTris * 3;
        if (bufVboLen != expectedLen) {
            throw std::runtime_error("Fast3D VBO float count does not match selected shader stride");
        }
        for (size_t i = 0; i < bufVboLen; ++i) {
            if (!std::isfinite(bufVbo[i])) {
                throw std::runtime_error("Fast3D VBO contains a non-finite float");
            }
        }

        ++Stats.DrawTrianglesCallCount;
        Stats.DrawTrianglesTriangleCount += bufVboNumTris;
        Stats.DrawTrianglesFloatCount += bufVboLen;
        Stats.MaxTrianglesPerDraw = std::max(Stats.MaxTrianglesPerDraw, bufVboNumTris);
    }

    void Init() override {
    }

    void OnResize() override {
    }

    void StartFrame() override {
    }

    void EndFrame() override {
    }

    void FinishRender() override {
    }

    int CreateFramebuffer() override {
        return static_cast<int>(++Stats.FramebufferCreateCount);
    }

    void UpdateFramebufferParameters(int, uint32_t, uint32_t, uint32_t, bool, bool, bool, bool) override {
    }

    bool UpdateFramebufferParametersWithColorFormat(int, uint32_t width, uint32_t height,
                                                    uint32_t msaaLevel, bool, bool renderTarget,
                                                    bool, bool,
                                                    Fast::GfxFramebufferColorFormat colorFormat) override {
        if (width == 0 || height == 0 || msaaLevel != 1 || !renderTarget ||
            colorFormat != Fast::GfxFramebufferColorFormat::R32ui) {
            return false;
        }
        ++Stats.FramebufferUpdateCount;
        Stats.LastFramebufferWidth = width;
        Stats.LastFramebufferHeight = height;
        Stats.LastFramebufferColorFormat = "r32ui_encoded_shadow_depth_reference";
        return true;
    }

    bool SupportsOot3dShadow2dR32uiPipeline() const override {
        return true;
    }

    bool BindOot3dShadow2dTexture(int, uint32_t) override {
        return true;
    }

    bool SetOot3dShadow2dShaderParameters(uint32_t, bool, bool) override {
        return true;
    }

    void StartDrawToFramebuffer(int, float) override {
    }

    void CopyFramebuffer(int, int, int, int, int, int, int, int, int, int) override {
    }

    void ClearFramebuffer(bool, bool) override {
    }

    void ReadFramebufferToCPU(int, uint32_t, uint32_t, uint16_t*) override {
    }

    void ResolveMSAAColorBuffer(int, int) override {
    }

    std::unordered_map<std::pair<float, float>, uint16_t, Fast::hash_pair_ff>
    GetPixelDepth(int, const std::set<std::pair<float, float>>&) override {
        return {};
    }

    void* GetFramebufferTextureId(int) override {
        return nullptr;
    }

    void SelectTextureFb(int) override {
    }

    void DeleteTexture(uint32_t texId) override {
        mTextures.erase(texId);
    }

    void SetTextureFilter(Fast::FilteringMode mode) override {
        mFilteringMode = mode;
    }

    Fast::FilteringMode GetTextureFilter() override {
        return mFilteringMode;
    }

    void SetSrgbMode() override {
        mSrgbMode = true;
    }

    ImTextureID GetTextureById(int) override {
        return nullptr;
    }

    void SetCurrentPrimDepth(float depth) override {
        mCurrentPrimDepth = depth;
    }

    FakeApiStats Stats;

  private:
    std::map<std::pair<uint64_t, uint64_t>, std::unique_ptr<FakeShaderRecord>> mShaders;
    std::unordered_map<uint32_t, bool> mTextures;
    uint32_t mCurrentTextureIds[SHADER_MAX_TEXTURES] = {};
    Fast::ShaderProgram* mCurrentShader = nullptr;
    Fast::FilteringMode mFilteringMode = Fast::FILTER_LINEAR;
    bool mDepthTest = false;
    bool mDepthMask = false;
    bool mZmodeDecal = false;
    bool mUseAlpha = false;
    bool mSrgbMode = false;
    float mCurrentPrimDepth = 0.0f;
};

nlohmann::json FakeApiStatsToJson(const FakeApiStats& stats) {
    return {
        { "texture_create_count", stats.TextureCreateCount },
        { "texture_select_count", stats.TextureSelectCount },
        { "texture_upload_count", stats.TextureUploadCount },
        { "texture_upload_byte_count", stats.TextureUploadByteCount },
        { "sampler_parameter_count", stats.SamplerParameterCount },
        { "shader_create_count", stats.ShaderCreateCount },
        { "shader_load_count", stats.ShaderLoadCount },
        { "shader_unload_count", stats.ShaderUnloadCount },
        { "draw_triangles_call_count", stats.DrawTrianglesCallCount },
        { "draw_triangles_triangle_count", stats.DrawTrianglesTriangleCount },
        { "draw_triangles_float_count", stats.DrawTrianglesFloatCount },
        { "max_triangles_per_draw", stats.MaxTrianglesPerDraw },
        { "framebuffer_create_count", stats.FramebufferCreateCount },
        { "framebuffer_update_count", stats.FramebufferUpdateCount },
        { "last_framebuffer_width", stats.LastFramebufferWidth },
        { "last_framebuffer_height", stats.LastFramebufferHeight },
        { "last_framebuffer_color_format", stats.LastFramebufferColorFormat },
    };
}

} // namespace

int main(int argc, char** argv) {
    try {
        Args args;
        if (!ParseArgs(argc, argv, args)) {
            PrintUsage();
            return 2;
        }

        const auto scene = ThreeDsRecomp::Oot3d::LoadOot3dNativeDemoSceneFromManifest(args.ManifestPath);
        const auto renderScene = ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoRenderScene(scene);
        const size_t animatedClipIndex = scene.LinkMovementClipIndex;
        const auto& animatedClip = scene.LinkCsabClips.at(animatedClipIndex);
        const float animatedFrame = animatedClip.Metadata.FrameCount > 1 ? 1.0f : 0.0f;
        const auto animatedPose =
            ThreeDsRecomp::Oot3d::SampleOot3dNativeDemoLinkPoseFrame(scene, animatedClipIndex, animatedFrame);
        const auto animatedSkinTransforms =
            ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoSkinTransforms(scene.LinkBindWorldTransforms, animatedPose);
        const auto animatedRenderScene =
            ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoRenderScene(scene, animatedPose, animatedSkinTransforms);

        FakeFast3dRenderingApi fakeApi;
        ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderBackend fast3dBackend(fakeApi);
        const auto firstSubmission = ThreeDsRecomp::Oot3d::SubmitOot3dNativeDemoRenderScene(renderScene, fast3dBackend);
        const auto adapterStatsAfterFirstSubmit = fast3dBackend.Stats();
        const auto fakeStatsAfterFirstSubmit = fakeApi.Stats;
        const auto secondSubmission = ThreeDsRecomp::Oot3d::SubmitOot3dNativeDemoRenderScene(animatedRenderScene, fast3dBackend);

        const auto& adapterStats = fast3dBackend.Stats();
        const auto& fakeStats = fakeApi.Stats;
        const size_t expectedTextureUploadCount = firstSubmission.TextureUploadCount;
        const size_t expectedTriangleCount = firstSubmission.TriangleCount + secondSubmission.TriangleCount;
        const bool commonValid = firstSubmission.IsValid && secondSubmission.IsValid &&
                                 animatedPose.Valid && scene.LinkCsabClips.size() >= 2 &&
                                 adapterStats.MissingTextureBatchCount == 0 &&
                                 adapterStats.TextureUploadCount == expectedTextureUploadCount &&
                                 adapterStats.TextureCacheHitCount == secondSubmission.TextureUploadCount &&
                                 adapterStats.TextureResidentCount == expectedTextureUploadCount &&
                                 fakeStats.TextureCreateCount == expectedTextureUploadCount &&
                                 fakeStats.TextureUploadCount == expectedTextureUploadCount &&
                                 adapterStats.TriangleCount == expectedTriangleCount &&
                                 adapterStats.TriangleCount == fakeStats.DrawTrianglesTriangleCount &&
                                 adapterStats.NativePicaSelfShadowShaderEquationSemanticsSupported &&
                                 adapterStats.NativePicaSelfShadowTextureSamplingSemanticsSupported &&
                                 adapterStats.NativePicaSelfShadowFullPrimaryLightContributionSupported &&
                                 adapterStats.NativePicaShadow2dTextureTypeSupported &&
                                 adapterStats.NativePicaShadow2dBackendPassRequestSupported &&
                                 adapterStats.NativePicaShadow2dVisualPassRequestSupported &&
                                 adapterStats.NativePicaShadow2dMaterialTextureProjectionInputSupported &&
                                 adapterStats.NativePicaShadow2dTexCoord0WInputSupported &&
                                 adapterStats.NativePicaShadow2dEncodedDepthCompareSupported &&
                                 adapterStats.NativePicaShadow2dBackendR32uiPipelineSupported &&
                                 adapterStats.NativePicaShadow2dBackendDepthEncodeShaderSupported &&
                                 adapterStats.NativePicaShadow2dPrimaryRgbSampleCompareShaderSupported &&
                                 !adapterStats.NativePicaShadow2dVisualPassReady &&
                                 adapterStats.NativePicaShadow2dBackendPassRequestSource ==
                                     "oot3d_pica_shadow_texture_projection_registers" &&
                                 adapterStats.NativePicaShadow2dVisualPassSourceKind ==
                                     "oot3d_pica_shadow2d_visual_pass_contract" &&
                                 adapterStats.NativePicaShadow2dVisualPassRenderTargetFormat ==
                                     "r32ui_encoded_shadow_depth_reference" &&
                                 adapterStats.NativePicaShadow2dMaterialTextureProjectionInputSource ==
                                     "oot3d_cmb_material_texture_coord0_plus_pica_texcoord0_w" &&
                                 adapterStats.NativePicaShadow2dTexCoord0WInputSource ==
                                     "oot3d_cmb_vshader_shbin_output_texcoord0_w" &&
                                 adapterStats.NativePicaShadow2dVisualPassApplication ==
                                     "sample_shadow2d_encoded_depth_in_fragment_primary_rgb_shadow_term" &&
                                 !adapterStats.NativePicaShadow2dVisualPassUsesRuntimeN64AssetSubstitution &&
                                 adapterStats.NativePicaShadow2dShadowMapFormat ==
                                     "r32ui_encoded_shadow_depth_reference" &&
                                 adapterStats.NativePicaShadow2dCompareSource ==
                                     "CompareShadow(DecodeShadow(encoded_shadow_pixel), z)" &&
                                 adapterStats.NativePicaShadow2dFilter ==
                                     "2x2_neighbor_compare_results_returned_as_rgba_shadow_vector" &&
                                 adapterStats.NativePicaShadow2dEncodedDepthDecodeSource ==
                                     "DecodeShadow(pixel) -> depth24 = pixel >> 8, alpha8 = pixel & 0xFF" &&
                                 adapterStats.NativePicaShadow2dEncodedDepthBits == 24 &&
                                 adapterStats.NativePicaShadow2dEncodedAlphaBits == 8 &&
                                 adapterStats.NativePicaShadow2dBiasShift == 1 &&
                                 adapterStats.NativePicaShadow2dFilterTapCount == 4 &&
                                 adapterStats.NativePicaShadow2dFilterResultChannelCount == 4 &&
                                 adapterStats.NativePicaShadow2dOutOfBoundsResult == "lit_1_0" &&
                                 adapterStats.NativePicaShadow2dFilterInterpolationSource ==
                                     "bilinear_mix_of_2x2_compare_results" &&
                                 adapterStats.NativePicaSelfShadowLightContributionFormula ==
                                     "pica_shadow_map_multiplies_native_cmb_vertex_hemisphere_material_ambient_light_ambient_plus_material_diffuse_light_diffuse_dot" &&
                                 !adapterStats.NativePicaSelfShadowUsesRuntimeN64AssetSubstitution &&
                                 adapterStats.NativePicaSelfShadowedLightRegisterCount == 8 &&
                                 adapterStats.NativePicaSelfShadowAppliedBatchCount == 0 &&
                                 fakeStats.MaxTrianglesPerDraw <= fast3dBackend.Config().MaxTrianglesPerDraw;

        const bool legacyShadow2dDiagnosticCountersValid =
            adapterStats.NativePicaShadow2dMaterialTextureProjectionDecodedBatchCount <=
                adapterStats.NativePicaSelfShadowCandidateBatchCount &&
            adapterStats.NativePicaShadow2dTexCoord0WInputDecodedBatchCount <=
                adapterStats.NativePicaSelfShadowCandidateBatchCount &&
            adapterStats.NativePicaSelfShadowPendingBatchCount <=
                adapterStats.NativePicaSelfShadowCandidateBatchCount;

        const bool defaultInactiveFixtureValid =
            adapterStats.NativePicaSelfShadowRouteAvailable &&
            !adapterStats.NativePicaSelfShadowShaderRouteDecoded &&
            adapterStats.NativePicaSelfShadowShaderRoutePending &&
            adapterStats.NativePicaShadow2dBackendPassRequested &&
            adapterStats.NativePicaShadow2dBackendPassPending &&
            adapterStats.NativePicaShadow2dVisualPassRequested &&
            adapterStats.NativePicaShadow2dVisualPassPending &&
            !adapterStats.NativePicaShadow2dProjectionRegisterValuesDecoded &&
            !adapterStats.NativePicaShadow2dProjectionRegisterTraceAvailable &&
            !adapterStats.NativePicaShadow2dDmpShadowZUniformsDecoded &&
            !adapterStats.NativePicaShadow2dPicaTextureShadowRegisterDecoded &&
            !adapterStats.NativePicaShadow2dPicaFramebufferShadowRegisterDecoded &&
            !adapterStats.NativePicaShadow2dShaderRouteRegisterTraceDecoded &&
            !adapterStats.NativePicaShadow2dShaderRouteMatchesPrimaryRgbShadowTerm &&
            !adapterStats.NativePicaShadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm &&
            adapterStats.NativePicaShadow2dProjectionRegisterValueSource ==
                "oot3d_codebin_default_shadow2d_inactive_or_validation_pending" &&
            adapterStats.NativePicaShadow2dVisualPassBlockedReason ==
                "shadow2d_default_or_inactive_for_current_fixture";

        const bool traceInactiveValid =
            !adapterStats.NativePicaSelfShadowRouteAvailable &&
            !adapterStats.NativePicaSelfShadowShaderRouteDecoded &&
            !adapterStats.NativePicaSelfShadowShaderRoutePending &&
            !adapterStats.NativePicaShadow2dBackendPassRequested &&
            !adapterStats.NativePicaShadow2dBackendPassPending &&
            !adapterStats.NativePicaShadow2dVisualPassRequested &&
            !adapterStats.NativePicaShadow2dVisualPassPending &&
            adapterStats.NativePicaShadow2dProjectionRegisterValuesDecoded &&
            adapterStats.NativePicaShadow2dProjectionRegisterTraceAvailable &&
            adapterStats.NativePicaShadow2dPicaTextureShadowRegisterDecoded &&
            adapterStats.NativePicaShadow2dPicaFramebufferShadowRegisterDecoded &&
            adapterStats.NativePicaShadow2dShaderRouteRegisterTraceDecoded &&
            !adapterStats.NativePicaShadow2dShaderRouteMatchesPrimaryRgbShadowTerm &&
            adapterStats.NativePicaShadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm &&
            adapterStats.NativePicaShadow2dShadowTextureDimDecoded &&
            adapterStats.NativePicaShadow2dShaderRouteTraceStatus ==
                "shadow2d_primary_rgb_shadow_term_inactive" &&
            adapterStats.NativePicaShadow2dVisualPassBlockedReason ==
                "shadow2d_shader_route_register_trace_primary_rgb_term_inactive" &&
            adapterStats.NativePicaSelfShadowCandidateBatchCount == 0 &&
            adapterStats.NativePicaSelfShadowCandidateVertexCount == 0 &&
            adapterStats.NativePicaSelfShadowPendingBatchCount == 0;

        const bool valid =
            commonValid && legacyShadow2dDiagnosticCountersValid &&
            (defaultInactiveFixtureValid || traceInactiveValid);

        const nlohmann::json output = {
            { "format", "oot3d_native_fast3d_renderer_probe_v7" },
            { "status", valid ? "valid" : "invalid" },
            { "submit_count", 2 },
            { "second_submit_uses_animated_link_pose", true },
            { "second_submit_uses_movement_clip", true },
            { "animated_link_pose",
              {
                  { "clip_index", animatedClipIndex },
                  { "clip_id", animatedClip.Id },
                  { "csab", animatedClip.CsabName },
                  { "requested_frame", animatedFrame },
                  { "sampled_pose_frame", animatedPose.Frame },
                  { "sampled_pose_valid", animatedPose.Valid },
                  { "frame_count", animatedClip.Metadata.FrameCount },
                  { "world_transform_count", animatedPose.WorldTransforms.size() },
                  { "skin_transform_count", animatedSkinTransforms.size() },
              } },
            { "engine_renderer_submission", ThreeDsRecomp::Oot3d::Oot3dNativeRendererSubmitResultToJson(firstSubmission) },
            { "first_engine_renderer_submission",
              ThreeDsRecomp::Oot3d::Oot3dNativeRendererSubmitResultToJson(firstSubmission) },
            { "second_engine_renderer_submission",
              ThreeDsRecomp::Oot3d::Oot3dNativeRendererSubmitResultToJson(secondSubmission) },
            { "fast3d_adapter_after_first_submit",
              ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderStatsToJson(adapterStatsAfterFirstSubmit) },
            { "fast3d_adapter", ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderStatsToJson(adapterStats) },
            { "fake_rendering_api_after_first_submit", FakeApiStatsToJson(fakeStatsAfterFirstSubmit) },
            { "fake_rendering_api", FakeApiStatsToJson(fakeStats) },
        };
        WriteJsonFile(args.OutputPath, output);
        return valid ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "oot3d_native_fast3d_renderer_probe: " << ex.what() << "\n";
        return 1;
    }
}
