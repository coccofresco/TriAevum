#pragma once

#include "oot3d/renderer/ui_render_backend.h"
#include "oot3d_ui/ui_primitives.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace oot3d::ui {

enum class N64UiCanvasMode : std::uint8_t {
    Widescreen16x9,
    NativeTopScreen400x240,
};

struct N64UiRendererStats {
    std::uint64_t frames = 0;
    std::uint64_t frames_with_primitives = 0;
    std::uint64_t primitives_received = 0;
    std::uint64_t primitives_drawn = 0;
    std::uint64_t solid_draws = 0;
    std::uint64_t textured_draws = 0;
    std::uint64_t draw_calls = 0;
    std::uint64_t triangles = 0;
    std::uint64_t texture_uploads = 0;
    std::uint64_t texture_cache_hits = 0;
    std::uint64_t native_texture_resolves = 0;
    std::uint64_t native_texture_failures = 0;
    std::uint64_t missing_texture_resources = 0;
    std::uint64_t unsupported_texture_resources = 0;
};

struct UiTexturePixels {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<std::uint8_t> rgba8;
    // Valid pixels standing in for a final payload that is still being
    // prepared (a custom texture decoding); the texture is resolved again.
    bool provisional = false;
};

// Backend-independent boundary for textures whose authoritative payload lives
// in the running OoT3D process rather than in an OTR archive.
class UiTextureProvider {
  public:
    virtual ~UiTextureProvider() = default;
    virtual bool Resolve(const UiTextureIdentity& identity,
                         UiTexturePixels& pixels,
                         std::string* error = nullptr) const = 0;
    // Changes whenever previously resolved payloads may have become stale
    // (for example a texture pack was enabled or reloaded).
    virtual std::uint64_t Revision() const noexcept { return 0U; }
};

// A texture payload read from its provider ahead of the Render that first
// uses it. Resolution reads the running process, so a threaded host prepares
// on the guest side and the renderer only consumes the result.
struct PreparedUiTexture {
    enum class Status : std::uint8_t { Resolved, MissingProvider, Invalid };
    std::string key;
    Status status = Status::Invalid;
    bool native = false;
    UiTexturePixels pixels;
    std::string error;
};
using PreparedUiTextures = std::vector<PreparedUiTexture>;

std::uint64_t N64UiSolidShaderId0() noexcept;
std::uint64_t N64UiTexturedShaderId0() noexcept;
std::uint64_t N64UiShaderId1() noexcept;
std::string_view ResolveN64UiTexturePath(
    std::string_view semanticName) noexcept;
std::string BuildUiTextureCacheKey(const UiTextureIdentity& identity);
// The resident texture a primitive draws with: its identity's key, or a
// white-RGB variant of it for texture_alpha_only.
std::string BuildUiPrimitiveTextureKey(const UiPrimitive& primitive);

// Draws typed N64-layout UI primitives after the native PICA scene through a
// renderer-owned 2D backend contract.
class N64UiFast3dRenderer {
  public:
    explicit N64UiFast3dRenderer(
        Oot3d::Renderer::UiRenderBackend& renderingApi,
        const UiTextureProvider* textureProvider = nullptr) noexcept;
    ~N64UiFast3dRenderer();

    N64UiFast3dRenderer(const N64UiFast3dRenderer&) = delete;
    N64UiFast3dRenderer& operator=(const N64UiFast3dRenderer&) = delete;

    bool Render(std::span<const UiPrimitive> primitives,
                std::uint32_t framebufferWidth,
                std::uint32_t framebufferHeight, N64UiCanvasMode canvasMode,
                std::string* error = nullptr);
    // Resolves every texture a Render of these primitives would resolve for
    // the first time. Prepared keys are remembered (until Release), so each
    // payload is read once, as Render alone would; a provisional payload is
    // resolved again on later calls, and a new provider Revision resolves
    // every texture again.
    [[nodiscard]] PreparedUiTextures
    PrepareTextures(std::span<const UiPrimitive> primitives);
    // Hands prepared payloads to the renderer without drawing (a frame that
    // renders nothing still owns them for its next Render). A payload
    // prepared again replaces the resident texture of the same key.
    void Stage(PreparedUiTextures prepared);
    // Render without reading the provider: first uses consume `prepared`.
    bool Render(std::span<const UiPrimitive> primitives,
                std::uint32_t framebufferWidth,
                std::uint32_t framebufferHeight, N64UiCanvasMode canvasMode,
                PreparedUiTextures prepared, std::string* error = nullptr);
    void SetTextureProvider(const UiTextureProvider* textureProvider) noexcept;
    void Release() noexcept;

    const N64UiRendererStats& Stats() const noexcept;

  private:
    struct ResidentTexture {
        std::uint32_t renderer_id = 0;
        std::uint16_t width = 0;
        std::uint16_t height = 0;
    };

    const ResidentTexture* ResolveTexture(const std::string& semanticKey,
                                          std::string* error);
    Oot3d::Renderer::UiRenderBackend& rendering_api_;
    const UiTextureProvider* texture_provider_ = nullptr;
    std::unordered_map<std::string, ResidentTexture> textures_;
    std::unordered_set<std::string> failed_textures_;
    // Preparing side: keys already handed to the renderer, those handed with
    // provisional pixels, and the provider revision they were resolved at.
    std::unordered_set<std::string> prepared_keys_;
    std::unordered_set<std::string> provisional_keys_;
    std::uint64_t prepared_revision_ = 0;
    // Rendering side: prepared payloads not consumed yet.
    std::unordered_map<std::string, PreparedUiTexture> staged_textures_;
    N64UiRendererStats stats_;
};

} // namespace oot3d::ui
