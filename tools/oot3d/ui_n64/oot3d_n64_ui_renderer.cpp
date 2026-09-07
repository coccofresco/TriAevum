#include "oot3d_n64_ui_renderer.h"

#include "oot3d/renderer/ui_presentation_layout.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace oot3d::ui {
namespace {

constexpr float kWidescreenCanvasWidth = 1280.0F / 3.0F;
constexpr float kNativeTopScreenCanvasWidth = 400.0F;
constexpr float kCanvasHeight = 240.0F;

constexpr std::array<std::pair<std::string_view, std::string_view>, 18>
    kTexturePaths = {{
        {"n64/file_select/title/select_file",
         "__OTR__textures/title_static/gFileSelPleaseSelectAFileENGTex"},
        {"n64/file_select/file_1",
         "__OTR__textures/title_static/gFileSelFile1ButtonENGTex"},
        {"n64/file_select/file_2",
         "__OTR__textures/title_static/gFileSelFile2ButtonENGTex"},
        {"n64/file_select/file_3",
         "__OTR__textures/title_static/gFileSelFile3ButtonENGTex"},
        {"n64/file_select/name_box",
         "__OTR__textures/title_static/gFileSelNameBoxTex"},
        {"n64/file_select/copy",
         "__OTR__textures/title_static/gFileSelCopyButtonENGTex"},
        {"n64/file_select/erase",
         "__OTR__textures/title_static/gFileSelEraseButtonENGTex"},
        {"n64/file_select/options",
         "__OTR__textures/title_static/gFileSelOptionsButtonENGTex"},
        {"n64/file_select/highlight",
         "__OTR__textures/title_static/gFileSelBigButtonHighlightTex"},
        {"n64/name_entry/title/name",
         "__OTR__textures/title_static/gFileSelNameENGTex"},
        {"n64/name_entry/backspace",
         "__OTR__textures/title_static/gFileSelBackspaceButtonTex"},
        {"n64/name_entry/end",
         "__OTR__textures/title_static/gFileSelENDButtonENGTex"},
        {"n64/name_entry/highlight/character",
         "__OTR__textures/title_static/gFileSelCharHighlightTex"},
        {"n64/name_entry/highlight/medium",
         "__OTR__textures/title_static/gFileSelMediumButtonHighlightTex"},
        {"n64/name_entry/highlight/small",
         "__OTR__textures/title_static/gFileSelSmallButtonHighlightTex"},
        {"n64/name_entry/confirm/prompt",
         "__OTR__textures/title_static/gFileSelAreYouSureENGTex"},
        {"n64/name_entry/confirm/yes",
         "__OTR__textures/title_static/gFileSelYesButtonENGTex"},
        {"n64/name_entry/confirm/quit",
         "__OTR__textures/title_static/gFileSelQuitButtonENGTex"},
    }};

std::uint64_t PackShaderInput(std::uint32_t value, std::uint32_t cycle,
                              std::uint32_t component,
                              std::uint32_t term) noexcept {
    return static_cast<std::uint64_t>(value)
           << (cycle * 32U + component * 16U + term * 4U);
}

float ClipX(float x, float canvasWidth) noexcept {
    return (x / canvasWidth) * 2.0F - 1.0F;
}

float ClipY(float y, float canvasHeight, bool invertY) noexcept {
    const float clip = 1.0F - (y / canvasHeight) * 2.0F;
    return invertY ? -clip : clip;
}

void AppendVertex(std::vector<Oot3d::Renderer::UiOverlayVertex>& output,
                  float x, float y, float u, float v, const UiColor& color,
                  float canvasWidth, float canvasHeight, bool invertY) {
    output.push_back({
        ClipX(x, canvasWidth),
        ClipY(y, canvasHeight, invertY),
        u,
        v,
        color.red,
        color.green,
        color.blue,
        color.alpha,
    });
}

std::vector<Oot3d::Renderer::UiOverlayVertex>
BuildQuad(const UiPrimitive& primitive, float canvasWidth, float canvasHeight,
          bool invertY) {
    const float left = primitive.destination.x;
    const float top = primitive.destination.y;
    const float right = left + primitive.destination.width;
    const float bottom = top + primitive.destination.height;
    const float u0 = primitive.uv.x;
    const float v0 = primitive.uv.y;
    const float u1 = u0 + primitive.uv.width;
    const float v1 = v0 + primitive.uv.height;
    std::vector<Oot3d::Renderer::UiOverlayVertex> vertices;
    vertices.reserve(6U);
    AppendVertex(vertices, left, top, u0, v0, primitive.color, canvasWidth,
                 canvasHeight, invertY);
    AppendVertex(vertices, left, bottom, u0, v1, primitive.color, canvasWidth,
                 canvasHeight, invertY);
    AppendVertex(vertices, right, bottom, u1, v1, primitive.color, canvasWidth,
                 canvasHeight, invertY);
    AppendVertex(vertices, left, top, u0, v0, primitive.color, canvasWidth,
                 canvasHeight, invertY);
    AppendVertex(vertices, right, bottom, u1, v1, primitive.color, canvasWidth,
                 canvasHeight, invertY);
    AppendVertex(vertices, right, top, u1, v0, primitive.color, canvasWidth,
                 canvasHeight, invertY);
    return vertices;
}

} // namespace

std::uint64_t N64UiSolidShaderId0() noexcept {
    constexpr std::uint32_t kShaderInput1 = 1U;
    return PackShaderInput(kShaderInput1, 0U, 0U, 3U) |
           PackShaderInput(kShaderInput1, 0U, 1U, 3U);
}

std::uint64_t N64UiTexturedShaderId0() noexcept {
    constexpr std::uint32_t kShaderZero = 0U;
    constexpr std::uint32_t kShaderInput1 = 1U;
    constexpr std::uint32_t kShaderTexel0 = 8U;
    return PackShaderInput(kShaderTexel0, 0U, 0U, 0U) |
           PackShaderInput(kShaderZero, 0U, 0U, 1U) |
           PackShaderInput(kShaderInput1, 0U, 0U, 2U) |
           PackShaderInput(kShaderZero, 0U, 0U, 3U) |
           PackShaderInput(kShaderTexel0, 0U, 1U, 0U) |
           PackShaderInput(kShaderZero, 0U, 1U, 1U) |
           PackShaderInput(kShaderInput1, 0U, 1U, 2U) |
           PackShaderInput(kShaderZero, 0U, 1U, 3U);
}

std::uint64_t N64UiShaderId1() noexcept {
    return 1U;
}

std::string_view ResolveN64UiTexturePath(
    std::string_view semanticName) noexcept {
    const auto found = std::find_if(
        kTexturePaths.begin(), kTexturePaths.end(),
        [semanticName](const auto& entry) { return entry.first == semanticName; });
    return found != kTexturePaths.end() ? found->second : std::string_view{};
}

std::string BuildUiTextureCacheKey(const UiTextureIdentity& identity) {
    std::string key = identity.semantic_name;
    if (identity.guest_resource_address == 0U &&
        identity.guest_surface_address == 0U) {
        return key;
    }
    std::array<char, 32> suffix{};
    std::snprintf(suffix.data(), suffix.size(), "@%08X:%08X",
                  identity.guest_resource_address,
                  identity.guest_surface_address);
    key.append(suffix.data());
    return key;
}

N64UiFast3dRenderer::N64UiFast3dRenderer(
    Oot3d::Renderer::UiRenderBackend& renderingApi,
    const UiTextureProvider* textureProvider) noexcept
    : rendering_api_(renderingApi), texture_provider_(textureProvider) {}

N64UiFast3dRenderer::~N64UiFast3dRenderer() {
    Release();
}

void N64UiFast3dRenderer::Release() noexcept {
    for (const auto& [name, texture] : textures_) {
        (void)name;
        if (texture.renderer_id != 0U) {
            rendering_api_.DeleteUiTexture(texture.renderer_id);
        }
    }
    textures_.clear();
    failed_textures_.clear();
}

void N64UiFast3dRenderer::SetTextureProvider(
    const UiTextureProvider* textureProvider) noexcept {
    if (texture_provider_ == textureProvider) {
        return;
    }
    Release();
    texture_provider_ = textureProvider;
}

const N64UiFast3dRenderer::ResidentTexture*
N64UiFast3dRenderer::ResolveTexture(const UiTextureIdentity& identity,
                                    std::string* error) {
    const std::string semanticKey = BuildUiTextureCacheKey(identity);
    const auto resident = textures_.find(semanticKey);
    if (resident != textures_.end()) {
        ++stats_.texture_cache_hits;
        return &resident->second;
    }
    if (failed_textures_.contains(semanticKey)) {
        return nullptr;
    }

    if (texture_provider_ == nullptr) {
        ++stats_.missing_texture_resources;
        if (error != nullptr) {
            *error = "UI texture provider is unavailable";
        }
        failed_textures_.insert(semanticKey);
        return nullptr;
    }
    UiTexturePixels pixels;
    const bool nativeTexture = identity.semantic_name.starts_with("oot3d/");
    if (nativeTexture) {
        ++stats_.native_texture_resolves;
    }
    if (!texture_provider_->Resolve(identity, pixels, error) ||
        pixels.width == 0U || pixels.height == 0U ||
        pixels.rgba8.size() !=
            static_cast<std::size_t>(pixels.width) * pixels.height * 4U) {
        if (nativeTexture) {
            ++stats_.native_texture_failures;
        }
        ++stats_.unsupported_texture_resources;
        if (error != nullptr && error->empty()) {
            *error = "UI texture payload is invalid";
        }
        failed_textures_.insert(semanticKey);
        return nullptr;
    }

    ResidentTexture uploaded;
    uploaded.renderer_id = rendering_api_.CreateUiTexture(
        pixels.rgba8, pixels.width, pixels.height, error);
    if (uploaded.renderer_id == 0U) {
        ++stats_.unsupported_texture_resources;
        failed_textures_.insert(semanticKey);
        return nullptr;
    }
    uploaded.width = pixels.width;
    uploaded.height = pixels.height;
    ++stats_.texture_uploads;
    return &textures_.emplace(semanticKey, uploaded).first->second;
}

bool N64UiFast3dRenderer::Render(std::span<const UiPrimitive> primitives,
                                std::uint32_t framebufferWidth,
                                std::uint32_t framebufferHeight,
                                N64UiCanvasMode canvasMode,
                                std::string* error) {
    ++stats_.frames;
    stats_.primitives_received += primitives.size();
    if (primitives.empty()) {
        return true;
    }
    ++stats_.frames_with_primitives;

    std::vector<const UiPrimitive*> ordered;
    ordered.reserve(primitives.size());
    for (const UiPrimitive& primitive : primitives) {
        ordered.push_back(&primitive);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const UiPrimitive* left, const UiPrimitive* right) {
                         return left->layer < right->layer;
                     });

    const std::uint32_t width = std::max<std::uint32_t>(1U, framebufferWidth);
    const std::uint32_t height =
        std::max<std::uint32_t>(1U, framebufferHeight);
    const float canvasWidth =
        canvasMode == N64UiCanvasMode::NativeTopScreen400x240
            ? kNativeTopScreenCanvasWidth
            : kWidescreenCanvasWidth;
    const auto viewport = Oot3d::Renderer::FitUiPresentationViewport(
        width, height, canvasWidth, kCanvasHeight);
    if (!rendering_api_.PrepareUiOverlay(
            {viewport.X, viewport.Y, viewport.Width, viewport.Height},
            error)) {
        return false;
    }
    const bool invertY = rendering_api_.IsUiFramebufferYInverted();

    bool success = true;
    for (const UiPrimitive* primitive : ordered) {
        if (!primitive->visible || primitive->color.alpha <= 0.0F ||
            primitive->destination.width <= 0.0F ||
            primitive->destination.height <= 0.0F) {
            continue;
        }
        const bool textured = !primitive->texture.semantic_name.empty();
        std::uint32_t textureId = 0U;
        if (textured) {
            const ResidentTexture* texture = ResolveTexture(
                primitive->texture, error);
            if (texture == nullptr) {
                success = false;
                continue;
            }
            textureId = texture->renderer_id;
        }
        const auto vertices =
            BuildQuad(*primitive, canvasWidth, kCanvasHeight, invertY);
        if (!rendering_api_.DrawUiTriangles(vertices, textureId, error)) {
            return false;
        }
        ++stats_.primitives_drawn;
        ++stats_.draw_calls;
        stats_.triangles += 2U;
        if (textured) {
            ++stats_.textured_draws;
        } else {
            ++stats_.solid_draws;
        }
    }
    return success;
}

const N64UiRendererStats& N64UiFast3dRenderer::Stats() const noexcept {
    return stats_;
}

} // namespace oot3d::ui
