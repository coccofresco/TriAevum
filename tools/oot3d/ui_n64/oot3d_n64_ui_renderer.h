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
};

// Backend-independent boundary for textures whose authoritative payload lives
// in the running OoT3D process rather than in an OTR archive.
class UiTextureProvider {
  public:
    virtual ~UiTextureProvider() = default;
    virtual bool Resolve(const UiTextureIdentity& identity,
                         UiTexturePixels& pixels,
                         std::string* error = nullptr) const = 0;
};

std::uint64_t N64UiSolidShaderId0() noexcept;
std::uint64_t N64UiTexturedShaderId0() noexcept;
std::uint64_t N64UiShaderId1() noexcept;
std::string_view ResolveN64UiTexturePath(
    std::string_view semanticName) noexcept;
std::string BuildUiTextureCacheKey(const UiTextureIdentity& identity);

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
    void SetTextureProvider(const UiTextureProvider* textureProvider) noexcept;
    void Release() noexcept;

    const N64UiRendererStats& Stats() const noexcept;

  private:
    struct ResidentTexture {
        std::uint32_t renderer_id = 0;
        std::uint16_t width = 0;
        std::uint16_t height = 0;
    };

    const ResidentTexture* ResolveTexture(const UiTextureIdentity& identity,
                                          std::string* error);
    Oot3d::Renderer::UiRenderBackend& rendering_api_;
    const UiTextureProvider* texture_provider_ = nullptr;
    std::unordered_map<std::string, ResidentTexture> textures_;
    std::unordered_set<std::string> failed_textures_;
    N64UiRendererStats stats_;
};

} // namespace oot3d::ui
