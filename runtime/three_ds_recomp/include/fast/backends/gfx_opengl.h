#ifdef ENABLE_OPENGL
#pragma once

#include "gfx_rendering_api.h"
#include "../interpreter.h"

#include <memory>

#ifdef _MSC_VER
#include <SDL2/SDL.h>
// #define GL_GLEXT_PROTOTYPES 1
#include <GL/glew.h>
#elif FOR_WINDOWS
#include <GL/glew.h>
#include "SDL.h"
#define GL_GLEXT_PROTOTYPES 1
#include "SDL_opengl.h"
#elif __APPLE__
#include <SDL2/SDL.h>
#include <GL/glew.h>
#elif defined(__SWITCH__)
#include <SDL2/SDL.h>
#include <glad/glad.h>
#elif USE_OPENGLES
#include <SDL2/SDL.h>
#include <GLES3/gl3.h>
#else
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengl.h>
#endif
namespace Fast {
struct ShaderProgram {
    GLuint openglProgramId;
    uint8_t numInputs;
    bool usedTextures[SHADER_MAX_TEXTURES];
    uint8_t numFloats;
    GLint attribLocations[16];
    uint8_t attribSizes[16];
    uint8_t numAttribs;
    GLint frameCountLocation;
    GLint noiseScaleLocation;
    GLint prim_depth_location;
    GLint texture_width_location;
    GLint texture_height_location;
    GLint texture_filtering_location;
    GLint oot3d_shadow2d_texture_bias_location;
    GLint oot3d_shadow2d_orthographic_location;
    GLint oot3d_shadow2d_invert_location;
    GLint oot3d_pica_fog_lut_location;
    GLint oot3d_pica_fog_flip_location;
    GLint oot3d_pica_alpha_test_enabled_location;
    GLint oot3d_pica_alpha_test_func_location;
    GLint oot3d_pica_alpha_test_ref_location;
    GLint oot3d_native_transform_location;
    uint64_t oot3d_pica_fog_state_key;
    bool oot3d_pica_fog_state_key_valid;
};

struct FramebufferOGL {
    uint32_t width, height;
    bool has_depth_buffer;
    uint32_t msaa_level;
    bool invertY;
    GfxFramebufferColorFormat color_format = GfxFramebufferColorFormat::Rgba8;

    GLuint fbo, clrbuf, clrbufMsaa, rbo;
};

struct TextureInfo {
    uint16_t width;
    uint16_t height;
    uint16_t filtering;
};

class GfxRenderingAPIOGL final : public GfxRenderingAPI {
  public:
    struct NativePicaState;

    GfxRenderingAPIOGL();
    ~GfxRenderingAPIOGL() override;
    const char* GetName() override;
    int GetMaxTextureSize() override;
    GfxClipParameters GetClipParameters() override;
    void UnloadShader(ShaderProgram* oldPrg) override;
    void LoadShader(ShaderProgram* newPrg) override;
    ShaderProgram* CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) override;
    ShaderProgram* LookupShader(uint64_t shaderId0, uint64_t shaderId1) override;
    void ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) override;
    void ClearShaderCache() override;
    uint32_t NewTexture() override;
    void SelectTexture(int tile, uint32_t textureId) override;
    void UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) override;
    void SetSamplerParameters(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt) override;
    bool UploadTextureMipLevel(uint32_t level, const uint8_t* rgba32Buf,
                               uint32_t width, uint32_t height) override;
    bool SetNativeSamplerParameters(int sampler, const GfxNativeSamplerState& state) override;
    void SetDepthTestAndMask(bool depth_test, bool z_upd) override;
    void SetCurrentPrimDepth(float depth) override;
    void SetZmodeDecal(bool decal) override;
    void SetViewport(int x, int y, int width, int height) override;
    void SetScissor(int x, int y, int width, int height) override;
    void SetUseAlpha(bool useAlpha) override;
    bool SetNativeBlendState(const GfxNativeBlendState& state) override;
    bool SetNativeCullMode(GfxNativeCullMode mode) override;
    bool SetOot3dNativeTransform(const float* row_major_matrix) override;
    bool DrawTrianglesCached(uint64_t cache_id, uint64_t content_version,
                             const float* buf_vbo, size_t buf_vbo_len,
                             size_t buf_vbo_num_tris) override;
    void DrawTriangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) override;
    void Init() override;
    void Shutdown() override;
    void OnResize() override;
    void StartFrame() override;
    void EndFrame() override;
    void FinishRender() override;
    int CreateFramebuffer() override;
    void UpdateFramebufferParameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                     bool opengl_invertY, bool render_target, bool has_depth_buffer,
                                     bool can_extract_depth) override;
    bool UpdateFramebufferParametersWithColorFormat(int fb_id, uint32_t width, uint32_t height,
                                                    uint32_t msaa_level, bool opengl_invertY,
                                                    bool render_target, bool has_depth_buffer,
                                                    bool can_extract_depth,
                                                    GfxFramebufferColorFormat color_format) override;
    bool SupportsOot3dShadow2dR32uiPipeline() const override;
    bool BindOot3dShadow2dTexture(int fb_id, uint32_t texture_unit) override;
    bool SetOot3dShadow2dShaderParameters(uint32_t texture_bias, bool orthographic, bool invert) override;
    bool SupportsOot3dPicaFogLut() const override;
    bool SetOot3dPicaFogShaderParameters(const uint32_t* lut_words, size_t lut_word_count,
                                         bool flip, uint64_t state_key) override;
    bool SetOot3dPicaAlphaTestShaderParameters(bool enabled, uint32_t compare_func,
                                               uint8_t reference) override;
    bool SupportsOot3dPicaTexture2() const override;
    GfxNativePicaBackendStats GetNativePicaBackendStats() const noexcept override;
    void SetNativePicaGeometryCacheEnabled(bool enabled) noexcept override;
    bool PublishPicaCompositionSequence(
        const Renderer3ds::PicaCompositionSequenceView& sequence,
        std::string* error = nullptr) override;
    bool SubmitPicaDraw(const GfxNativePicaDrawView& draw,
                        std::string* error = nullptr) override;
    bool SubmitPicaDisplayTransfer(
        const GfxNativePicaDisplayTransferView& transfer,
        std::string* error = nullptr) override;
    bool ClearPicaRenderTarget(
        uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress,
        std::string* error = nullptr) override;
    bool SubmitPicaMemoryFill(
        const GfxNativePicaMemoryFillView& fill,
        std::string* error = nullptr) override;
    bool QueuePicaCompletion(
        uint64_t completionId, std::string* error = nullptr) override;
    std::vector<uint64_t> TakePicaCompletions() override;
    bool ResetPicaState(std::string* error = nullptr) override;
    bool StartOot3dShadow2dDepthEncodePass(int fb_id, uint32_t clear_value) override;
    void EndOot3dShadow2dDepthEncodePass() override;
    bool DrawOot3dShadow2dDepthEncodedTriangles(float buf_vbo[], size_t buf_vbo_len,
                                                size_t buf_vbo_num_tris) override;
    void StartDrawToFramebuffer(int fbId, float noiseScale) override;
    void CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0,
                         int dstX1, int dstY1) override;
    void ClearFramebuffer(bool color, bool depth) override;
    void ClearDepthRegion(int x, int y, int w, int h) override;
    void ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) override;
    void ResolveMSAAColorBuffer(int fbIdTarger, int fbIdSrc) override;
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int fb_id, const std::set<std::pair<float, float>>& coordinates) override;
    void* GetFramebufferTextureId(int fbId) override;
    void SelectTextureFb(int fbId) override;
    void DeleteTexture(uint32_t texId) override;
    void SetTextureFilter(FilteringMode mode) override;
    FilteringMode GetTextureFilter() override;
    void SetSrgbMode() override;
    ImTextureID GetTextureById(int id) override;

  private:
    void InitNativePica();
    void ShutdownNativePica() noexcept;
    void InvalidateLegacyStateAfterPica() noexcept;

    void SetUniforms(ShaderProgram* prg) const;
    std::string BuildFsShader(const CCFeatures& cc_features);
    void SetPerDrawUniforms();
    void PrepareTriangleDraw();
    void BindArrayBufferForCurrentShader(GLuint buffer);
    bool EnsureOot3dShadow2dDepthEncodeProgram();

    struct CachedVertexBuffer {
        GLuint Buffer = 0;
        uint64_t ContentVersion = 0;
        size_t FloatCount = 0;
        size_t TriangleCount = 0;
        uint32_t LastUsedFrame = 0;
    };

    std::vector<TextureInfo> textures;
    GLuint mCurrentTextureIds[SHADER_MAX_TEXTURES] = {};
    GLuint mLastBoundTextures[SHADER_MAX_TEXTURES] = {};
    uint8_t mCurrentTile;
    int8_t mLastActiveTexture = -1;
    int8_t mLastBlendEnabled = -1;
    int8_t mLastScissorEnabled = -1;

    std::map<std::pair<uint64_t, uint32_t>, ShaderProgram> mShaderProgramPool;
    ShaderProgram* mCurrentShaderProgram = nullptr;
    ShaderProgram* mLastLoadedShader = nullptr;

    GLuint mOpenglVbo = 0;
    GLuint mCurrentArrayBuffer = 0;
    std::unordered_map<uint64_t, CachedVertexBuffer> mOot3dCachedVertexBuffers;
    GLuint mOot3dShadow2dDepthEncodeProgram = 0;
    GLint mOot3dShadow2dDepthEncodePositionLocation = -1;
#if defined(__APPLE__) || defined(__SWITCH__) || defined(USE_OPENGLES)
    GLuint mOpenglVao;
#endif

    uint32_t mFrameCount = 0;

    std::vector<FramebufferOGL> mFrameBuffers;
    size_t mCurrentFrameBuffer = 0;
    float mCurrentNoiseScale = 0.0f;
    FilteringMode mCurrentFilterMode = FILTER_THREE_POINT;

    GLint mMaxMsaaLevel = 1;
    GLuint mPixelDepthRb = 0;
    GLuint mPixelDepthFb = 0;
    size_t mPixelDepthRbSize = 0;
    bool mNativePicaGeometryCacheEnabled = true;
    std::unique_ptr<NativePicaState> mNativePica;
};

} // namespace Fast
#endif
