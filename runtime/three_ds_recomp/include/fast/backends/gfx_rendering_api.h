#pragma once

#include <stdint.h>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <set>
#include <vector>
#include "imconfig.h"
#include "fast/renderer3ds/pica_render_backend.h"

namespace Fast {
struct ShaderProgram;

struct GfxClipParameters {
    bool z_is_from_0_to_1;
    bool invertY;
};

enum FilteringMode { FILTER_THREE_POINT, FILTER_LINEAR, FILTER_NONE };

enum class GfxFramebufferColorFormat : uint8_t {
    Rgba8,
    R32ui,
};

using GfxNativeBlendEquation = ::Fast::Renderer3ds::NativeBlendEquation;
using GfxNativeBlendFactor = ::Fast::Renderer3ds::NativeBlendFactor;
using GfxNativeBlendState = ::Fast::Renderer3ds::NativeBlendState;
using GfxNativeCullMode = ::Fast::Renderer3ds::NativeCullMode;
using GfxNativeTextureFilter = ::Fast::Renderer3ds::NativeTextureFilter;
using GfxNativeTextureWrap = ::Fast::Renderer3ds::NativeTextureWrap;
using GfxNativeSamplerState = ::Fast::Renderer3ds::NativeSamplerState;
using GfxNativePicaTopology = ::Fast::Renderer3ds::PicaTopology;
using GfxNativePicaVertexFormat = ::Fast::Renderer3ds::PicaVertexFormat;
using GfxNativePicaCompareFunction =
    ::Fast::Renderer3ds::PicaCompareFunction;
using GfxNativePicaStencilAction = ::Fast::Renderer3ds::PicaStencilAction;
using GfxNativePicaLogicOperation = ::Fast::Renderer3ds::PicaLogicOperation;
using GfxNativePicaVertexBindingView =
    ::Fast::Renderer3ds::PicaVertexBindingView;
using GfxNativePicaVertexAttributeView =
    ::Fast::Renderer3ds::PicaVertexAttributeView;
using GfxNativePicaTextureView = ::Fast::Renderer3ds::PicaTextureView;
using GfxNativePicaStencilState = ::Fast::Renderer3ds::PicaStencilState;
using GfxNativePicaDrawView = ::Fast::Renderer3ds::PicaDrawView;
using GfxNativePicaPresentationMode =
    ::Fast::Renderer3ds::PicaPresentationMode;
using GfxNativePicaDisplayTransferView =
    ::Fast::Renderer3ds::PicaDisplayTransferView;
using GfxNativePicaMemoryFillView = ::Fast::Renderer3ds::PicaMemoryFillView;
using GfxNativePicaTextureSnapshotFormat =
    ::Fast::Renderer3ds::PicaTextureSnapshotFormat;
using GfxNativePicaTextureCacheEntrySnapshot =
    ::Fast::Renderer3ds::PicaTextureCacheEntrySnapshot;
using GfxNativePicaRenderTargetColorSnapshot =
    ::Fast::Renderer3ds::PicaRenderTargetColorSnapshot;
using GfxNativePicaDisplayImageColorSnapshot =
    ::Fast::Renderer3ds::PicaDisplayImageColorSnapshot;
using GfxNativePicaPresentationStateSnapshot =
    ::Fast::Renderer3ds::PicaPresentationStateSnapshot;

// Startup shader/pipeline precompilation state. While Blocking, the host must
// hold the guest and show progress instead of advancing the game.
struct GfxNativePicaPrewarmProgress {
    uint32_t Completed = 0;
    uint32_t Total = 0;
    bool Blocking = false;
};

struct GfxNativePicaBackendStats {
    bool Available = false;
    bool GeometryCacheEnabled = false;
    uint64_t GeometryRegistryHits = 0;
    uint64_t GeometryRegistryMisses = 0;
    uint64_t GeometryRegistryUpdates = 0;
    uint64_t GeometryRegistryEvictions = 0;
    uint64_t GeometryRegistryEntries = 0;
    uint64_t GeometryPersistentDraws = 0;
    uint64_t GeometryStreamingDraws = 0;
    uint64_t GeometryPersistentUploads = 0;
    uint64_t GeometryPersistentUploadBytes = 0;
    uint64_t GeometryStreamingUploads = 0;
    uint64_t GeometryStreamingUploadBytes = 0;
    uint64_t UniformUploads = 0;
    uint64_t UniformUploadBytes = 0;
};

// A hash function used to hash a: pair<float, float>
struct hash_pair_ff {
    size_t operator()(const std::pair<float, float>& p) const {
        const auto hash1 = std::hash<float>{}(p.first);
        const auto hash2 = std::hash<float>{}(p.second);

        // If hash1 == hash2, their XOR is zero.
        return (hash1 != hash2) ? hash1 ^ hash2 : hash1;
    }
};

class GfxRenderingAPI : public ::Fast::Renderer3ds::PicaRenderBackend {
  public:
    virtual ~GfxRenderingAPI() = default;
    virtual const char* GetName() = 0;
    virtual int GetMaxTextureSize() = 0;
    virtual GfxClipParameters GetClipParameters() = 0;
    virtual void UnloadShader(ShaderProgram* oldPrg) = 0;
    virtual void LoadShader(ShaderProgram* newPrg) = 0;
    virtual void ClearShaderCache() = 0;
    virtual ShaderProgram* CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) = 0;
    virtual ShaderProgram* LookupShader(uint64_t shaderId0, uint64_t shaderId1) = 0;
    virtual void ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) = 0;
    virtual uint32_t NewTexture() = 0;
    virtual void SelectTexture(int tile, uint32_t textureId) = 0;
    virtual void UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) = 0;
    virtual void SetSamplerParameters(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt) = 0;
    virtual bool UploadTextureMipLevel(uint32_t level, const uint8_t* rgba32Buf,
                                       uint32_t width, uint32_t height) {
        return false;
    }
    virtual bool SetNativeSamplerParameters(int sampler, const GfxNativeSamplerState& state) {
        return false;
    }
    virtual void SetDepthTestAndMask(bool depth_test, bool z_upd) = 0;
    virtual void SetZmodeDecal(bool decal) = 0;
    virtual void SetViewport(int x, int y, int width, int height) = 0;
    virtual void SetScissor(int x, int y, int width, int height) = 0;
    virtual void SetUseAlpha(bool useAlpha) = 0;
    virtual bool SetNativeBlendState(const GfxNativeBlendState& state) {
        const bool opaqueReplace =
            !state.Enabled ||
            (state.EquationRgb == GfxNativeBlendEquation::Add &&
             state.EquationAlpha == GfxNativeBlendEquation::Add &&
             state.SourceRgb == GfxNativeBlendFactor::One &&
             state.DestRgb == GfxNativeBlendFactor::Zero &&
             state.SourceAlpha == GfxNativeBlendFactor::One &&
             state.DestAlpha == GfxNativeBlendFactor::Zero);
        if (opaqueReplace) {
            SetUseAlpha(false);
            return true;
        }

        const bool sourceAlpha =
            state.EquationRgb == GfxNativeBlendEquation::Add &&
            state.EquationAlpha == GfxNativeBlendEquation::Add &&
            state.SourceRgb == GfxNativeBlendFactor::SourceAlpha &&
            state.DestRgb == GfxNativeBlendFactor::OneMinusSourceAlpha &&
            state.SourceAlpha == GfxNativeBlendFactor::SourceAlpha &&
            state.DestAlpha == GfxNativeBlendFactor::OneMinusSourceAlpha;
        if (sourceAlpha) {
            SetUseAlpha(true);
            return true;
        }
        return false;
    }
    virtual bool SetNativeCullMode(GfxNativeCullMode mode) {
        return mode == GfxNativeCullMode::KeepAll;
    }
    virtual bool SetOot3dNativeTransform(const float*) {
        return false;
    }
    virtual bool DrawTrianglesCached(uint64_t, uint64_t, const float*, size_t, size_t) {
        return false;
    }
    virtual void DrawTriangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) = 0;
    virtual void Init() = 0;
    virtual void Shutdown() {
    }
    virtual void OnResize() = 0;
    virtual void StartFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void FinishRender() = 0;
    virtual int CreateFramebuffer() = 0;
    virtual void UpdateFramebufferParameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                             bool opengl_invertY, bool render_target, bool has_depth_buffer,
                                             bool can_extract_depth) = 0;
    virtual bool UpdateFramebufferParametersWithColorFormat(int fb_id, uint32_t width, uint32_t height,
                                                            uint32_t msaa_level, bool opengl_invertY,
                                                            bool render_target, bool has_depth_buffer,
                                                            bool can_extract_depth,
                                                            GfxFramebufferColorFormat color_format) {
        if (color_format != GfxFramebufferColorFormat::Rgba8) {
            return false;
        }
        UpdateFramebufferParameters(fb_id, width, height, msaa_level, opengl_invertY, render_target,
                                    has_depth_buffer, can_extract_depth);
        return true;
    }
    virtual bool SupportsOot3dShadow2dR32uiPipeline() const {
        return false;
    }
    virtual bool BindOot3dShadow2dTexture(int, uint32_t) {
        return false;
    }
    virtual bool SetOot3dShadow2dShaderParameters(uint32_t, bool, bool) {
        return false;
    }
    virtual bool SupportsOot3dPicaFogLut() const {
        return false;
    }
    virtual bool SetOot3dPicaFogShaderParameters(const uint32_t*, size_t, bool, uint64_t) {
        return false;
    }
    virtual bool SetOot3dPicaAlphaTestShaderParameters(bool, uint32_t, uint8_t) {
        return false;
    }
    virtual bool SupportsOot3dPicaTexture2() const {
        return false;
    }
    virtual GfxNativePicaBackendStats GetNativePicaBackendStats() const noexcept {
        return {};
    }
    virtual void SetNativePicaGeometryCacheEnabled(bool) noexcept {
    }
    virtual GfxNativePicaPrewarmProgress NativePicaPipelinePrewarmProgress()
        const noexcept {
        return {};
    }
    bool SubmitPicaDraw(const GfxNativePicaDrawView&,
                        std::string* error = nullptr) override {
        if (error != nullptr) {
            *error = "rendering backend does not consume native PICA draws";
        }
        return false;
    }
    bool SubmitPicaDisplayTransfer(
        const GfxNativePicaDisplayTransferView&,
        std::string* error = nullptr) override {
        if (error != nullptr) {
            *error = "rendering backend does not accept native PICA display transfers";
        }
        return false;
    }
    bool ClearPicaRenderTarget(
        uint64_t, uint32_t, std::string* error = nullptr) override {
        if (error != nullptr) {
            *error = "rendering backend does not clear native PICA render targets";
        }
        return false;
    }
    bool SubmitPicaMemoryFill(
        const GfxNativePicaMemoryFillView&,
        std::string* error = nullptr) override {
        if (error != nullptr) {
            *error = "rendering backend does not accept native PICA memory fills";
        }
        return false;
    }
    virtual bool StartOot3dShadow2dDepthEncodePass(int, uint32_t) {
        return false;
    }
    virtual void EndOot3dShadow2dDepthEncodePass() {
    }
    virtual bool DrawOot3dShadow2dDepthEncodedTriangles(float[], size_t, size_t) {
        return false;
    }
    virtual void StartDrawToFramebuffer(int fbId, float noiseScale) = 0;
    virtual void CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1, int dstX0,
                                 int dstY0, int dstX1, int dstY1) = 0;
    virtual void SetClearColor(float r, float g, float b, float a) {
        mClearColor[0] = r;
        mClearColor[1] = g;
        mClearColor[2] = b;
        mClearColor[3] = a;
    }
    virtual void ClearFramebuffer(bool color, bool depth) = 0;
    virtual void ClearDepthRegion(int x, int y, int w, int h) {
        // Default: full depth clear. Backends that support scissored depth clears
        // (e.g. OpenGL) should override for a more precise partial clear.
        ClearFramebuffer(false, true);
    }
    virtual void ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) = 0;
    virtual void ResolveMSAAColorBuffer(int fbIdTarger, int fbIdSrc) = 0;
    virtual std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int fb_id, const std::set<std::pair<float, float>>& coordinates) = 0;
    virtual void* GetFramebufferTextureId(int fbId) = 0;
    virtual void SelectTextureFb(int fbId) = 0;
    virtual void DeleteTexture(uint32_t texId) = 0;
    virtual void SetTextureFilter(FilteringMode mode) = 0;
    virtual FilteringMode GetTextureFilter() = 0;
    virtual void SetSrgbMode() = 0;
    virtual ImTextureID GetTextureById(int id) = 0;
    virtual void SetCurrentPrimDepth(float depth) = 0;

  protected:
    int8_t mCurrentDepthTest = 0;
    int8_t mCurrentDepthMask = 0;
    int8_t mCurrentZmodeDecal = 0;
    int8_t mLastDepthTest = -1;
    int8_t mLastDepthMask = -1;
    int8_t mLastZmodeDecal = -1;
    bool mSrgbMode = false;
    float mCurrentPrimDepth = 0.0f;
    bool mPrimDepthDirty = true;
    float mClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
};
} // namespace Fast
