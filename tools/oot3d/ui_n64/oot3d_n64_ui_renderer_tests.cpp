#include "oot3d_n64_ui_renderer.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_n64_ui_renderer_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

class RecordingBackend final : public Oot3d::Renderer::UiRenderBackend {
  public:
    bool PrepareUiOverlay(const Oot3d::Renderer::UiOverlayViewport&,
                          std::string*) override {
        return true;
    }
    bool IsUiFramebufferYInverted() const noexcept override { return false; }
    uint32_t CreateUiTexture(std::span<const uint8_t>, uint16_t, uint16_t,
                             std::string*) override {
        return ++created;
    }
    void DeleteUiTexture(uint32_t) noexcept override {}
    bool DrawUiTriangles(std::span<const Oot3d::Renderer::UiOverlayVertex>,
                         uint32_t textureId, std::string*) override {
        drawnTextures.push_back(textureId);
        return true;
    }
    uint32_t created = 0;
    std::vector<uint32_t> drawnTextures;
};

class CountingProvider final : public oot3d::ui::UiTextureProvider {
  public:
    bool Resolve(const oot3d::ui::UiTextureIdentity& identity,
                 oot3d::ui::UiTexturePixels& pixels,
                 std::string* error) const override {
        ++calls;
        if (identity.semantic_name == "oot3d/native/broken") {
            if (error != nullptr) *error = "broken payload";
            return false;
        }
        pixels.width = 2;
        pixels.height = 1;
        pixels.rgba8.assign(8, 0xFF);
        return true;
    }
    mutable int calls = 0;
};

oot3d::ui::UiPrimitive TexturedPrimitive(std::string_view name) {
    oot3d::ui::UiPrimitive primitive;
    primitive.texture.semantic_name = std::string(name);
    primitive.destination = {0.0F, 0.0F, 8.0F, 8.0F};
    return primitive;
}

// Prepared rendering reads the provider only while preparing, once per key,
// and renders exactly what the reading Render renders.
void TestPreparedTextures() {
    using namespace oot3d::ui;
    const std::vector<UiPrimitive> primitives = {
        TexturedPrimitive("oot3d/native/a"), TexturedPrimitive("oot3d/native/b"),
        TexturedPrimitive("oot3d/native/a"),
        TexturedPrimitive("oot3d/native/broken")};
    CountingProvider readingProvider;
    RecordingBackend readingBackend;
    std::string readingError;
    {
        N64UiFast3dRenderer renderer(readingBackend, &readingProvider);
        Require(!renderer.Render(primitives, 400, 240,
                                 N64UiCanvasMode::NativeTopScreen400x240,
                                 &readingError),
                "a broken UI texture was not reported");
    }
    CountingProvider provider;
    RecordingBackend backend;
    N64UiFast3dRenderer renderer(backend, &provider);
    auto prepared = renderer.PrepareTextures(primitives);
    Require(prepared.size() == 3U && provider.calls == 3,
            "UI texture preparation did not resolve each key once");
    std::string error;
    Require(!renderer.Render(primitives, 400, 240,
                             N64UiCanvasMode::NativeTopScreen400x240,
                             std::move(prepared), &error),
            "a prepared broken UI texture was not reported");
    Require(provider.calls == 3,
            "prepared UI rendering read the texture provider");
    Require(backend.drawnTextures == readingBackend.drawnTextures &&
                backend.created == readingBackend.created &&
                error == readingError && error == "broken payload",
            "prepared UI rendering differs from reading rendering");
    Require(renderer.PrepareTextures(primitives).empty() && provider.calls == 3,
            "UI texture preparation repeated a prepared key");
    const auto stats = renderer.Stats();
    Require(stats.native_texture_resolves == 3U &&
                stats.native_texture_failures == 1U &&
                stats.texture_uploads == 2U,
            "prepared UI texture statistics changed");
    renderer.Release();
    Require(renderer.PrepareTextures(primitives).size() == 3U &&
                provider.calls == 6,
            "Release kept prepared UI texture keys");
}

// A texture pack that is still decoding a replacement, then finishes it.
class CustomTextureProvider final : public oot3d::ui::UiTextureProvider {
  public:
    bool Resolve(const oot3d::ui::UiTextureIdentity&,
                 oot3d::ui::UiTexturePixels& pixels,
                 std::string*) const override {
        ++calls;
        const uint16_t extent = ready ? 4U : 2U;
        pixels.width = extent;
        pixels.height = extent;
        pixels.rgba8.assign(std::size_t(extent) * extent * 4U, 0xFF);
        pixels.provisional = !ready;
        return true;
    }
    std::uint64_t Revision() const noexcept override { return revision; }
    bool ready = false;
    std::uint64_t revision = 1U;
    mutable int calls = 0;
};

class SizeRecordingBackend final : public Oot3d::Renderer::UiRenderBackend {
  public:
    bool PrepareUiOverlay(const Oot3d::Renderer::UiOverlayViewport&,
                          std::string*) override {
        return true;
    }
    bool IsUiFramebufferYInverted() const noexcept override { return false; }
    uint32_t CreateUiTexture(std::span<const uint8_t>, uint16_t width, uint16_t,
                             std::string*) override {
        widths.push_back(width);
        return static_cast<uint32_t>(widths.size());
    }
    void DeleteUiTexture(uint32_t id) noexcept override { deleted.push_back(id); }
    bool DrawUiTriangles(std::span<const Oot3d::Renderer::UiOverlayVertex>,
                         uint32_t textureId, std::string*) override {
        drawn = textureId;
        return true;
    }
    std::vector<uint16_t> widths;
    std::vector<uint32_t> deleted;
    uint32_t drawn = 0;
};

// Provisional pixels are drawn until the final payload is ready, which then
// replaces them once; a new provider revision resolves every texture again.
void TestProvisionalTextures() {
    using namespace oot3d::ui;
    const std::vector<UiPrimitive> primitives = {
        TexturedPrimitive("oot3d/native/hud")};
    CustomTextureProvider provider;
    SizeRecordingBackend backend;
    N64UiFast3dRenderer renderer(backend, &provider);
    std::string error;
    const auto render = [&] {
        Require(renderer.Render(primitives, 400, 240,
                                N64UiCanvasMode::NativeTopScreen400x240,
                                renderer.PrepareTextures(primitives), &error),
                "provisional UI rendering failed");
    };
    render();
    render();
    Require(provider.calls == 2 && backend.widths == std::vector<uint16_t>{2U} &&
                backend.drawn == 1U,
            "an unchanged provisional UI texture was uploaded again");
    provider.ready = true;
    render();
    Require(backend.widths == std::vector<uint16_t>({2U, 4U}) &&
                backend.deleted == std::vector<uint32_t>{1U} && backend.drawn == 2U,
            "the final UI texture did not replace the provisional one");
    render();
    Require(provider.calls == 3 && backend.widths.size() == 2U,
            "a final UI texture was resolved again");
    provider.revision = 2U;
    render();
    Require(provider.calls == 4 && backend.widths.size() == 3U &&
                backend.deleted == std::vector<uint32_t>({1U, 2U}) &&
                backend.drawn == 3U,
            "a new provider revision did not resolve the UI texture again");
}

class VertexRecordingBackend final : public Oot3d::Renderer::UiRenderBackend {
  public:
    bool PrepareUiOverlay(const Oot3d::Renderer::UiOverlayViewport&,
                          std::string*) override {
        return true;
    }
    bool IsUiFramebufferYInverted() const noexcept override { return false; }
    uint32_t CreateUiTexture(std::span<const uint8_t>, uint16_t, uint16_t,
                             std::string*) override {
        return 1U;
    }
    void DeleteUiTexture(uint32_t) noexcept override {}
    bool DrawUiTriangles(std::span<const Oot3d::Renderer::UiOverlayVertex> vertices,
                         uint32_t, std::string*) override {
        draws.emplace_back(vertices.begin(), vertices.end());
        return true;
    }
    std::vector<std::vector<Oot3d::Renderer::UiOverlayVertex>> draws;
};

bool Near(float left, float right) {
    return left - right < 1.0e-5F && right - left < 1.0e-5F;
}

// Free corners draw as the strip (0,1,2) (2,1,3) with their own UVs, even
// with an empty destination; rectangles keep their corner order.
void TestFreeCorners() {
    using namespace oot3d::ui;
    UiPrimitive quad;
    quad.corners = std::array<UiVertex, 4>{{{100.0F, 60.0F, 0.25F, 0.5F},
                                            {200.0F, 60.0F, 0.75F, 0.5F},
                                            {120.0F, 180.0F, 0.25F, 1.0F},
                                            {180.0F, 180.0F, 0.75F, 1.0F}}};
    UiPrimitive hidden = quad;
    hidden.visible = false;
    UiPrimitive rectangle;
    rectangle.destination = {0.0F, 0.0F, 200.0F, 120.0F};
    rectangle.uv = {0.0F, 0.0F, 1.0F, 1.0F};
    const std::vector<UiPrimitive> primitives = {quad, hidden, rectangle};
    VertexRecordingBackend backend;
    N64UiFast3dRenderer renderer(backend);
    std::string error;
    Require(renderer.Render(primitives, 400, 240,
                            N64UiCanvasMode::NativeTopScreen400x240,
                            renderer.PrepareTextures(primitives), &error),
            "free-corner UI rendering failed");
    Require(backend.draws.size() == 2U,
            "a hidden free-corner quad was drawn or a visible one was rejected");
    const auto& strip = backend.draws[0];
    Require(strip.size() == 6U, "a free-corner quad is not two triangles");
    const std::array<std::size_t, 6> order{0U, 1U, 2U, 2U, 1U, 3U};
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto& corner = (*quad.corners)[order[i]];
        Require(Near(strip[i].X, corner.x / 200.0F - 1.0F) &&
                    Near(strip[i].Y, 1.0F - corner.y / 120.0F) &&
                    Near(strip[i].U, corner.u) && Near(strip[i].V, corner.v),
                "a free corner lost its position or UV");
    }
    const auto& box = backend.draws[1];
    Require(box.size() == 6U && Near(box[0].X, -1.0F) && Near(box[0].Y, 1.0F) &&
                Near(box[1].X, -1.0F) && Near(box[1].Y, 0.0F) &&
                Near(box[2].X, 0.0F) && Near(box[2].Y, 0.0F) &&
                Near(box[5].U, 1.0F) && Near(box[5].V, 0.0F),
            "the rectangle path changed");
}

class PixelRecordingBackend final : public Oot3d::Renderer::UiRenderBackend {
  public:
    bool PrepareUiOverlay(const Oot3d::Renderer::UiOverlayViewport&,
                          std::string*) override {
        return true;
    }
    bool IsUiFramebufferYInverted() const noexcept override { return false; }
    uint32_t CreateUiTexture(std::span<const uint8_t> rgba8, uint16_t, uint16_t,
                             std::string*) override {
        uploads.emplace_back(rgba8.begin(), rgba8.end());
        return static_cast<uint32_t>(uploads.size());
    }
    void DeleteUiTexture(uint32_t) noexcept override {}
    bool DrawUiTriangles(std::span<const Oot3d::Renderer::UiOverlayVertex>,
                         uint32_t textureId, std::string*) override {
        drawn.push_back(textureId);
        return true;
    }
    std::vector<std::vector<uint8_t>> uploads;
    std::vector<uint32_t> drawn;
};

class DarkProvider final : public oot3d::ui::UiTextureProvider {
  public:
    bool Resolve(const oot3d::ui::UiTextureIdentity&, oot3d::ui::UiTexturePixels& pixels,
                 std::string*) const override {
        pixels.width = 2;
        pixels.height = 1;
        pixels.rgba8 = {0x10, 0x20, 0x30, 0x00, 0x00, 0x00, 0x00, 0xFF};
        return true;
    }
};

// An alpha-only primitive draws a white-RGB copy that keeps the texel alpha;
// the ordinary primitive of the same texture keeps the payload unchanged.
void TestAlphaOnlyTexture() {
    using namespace oot3d::ui;
    UiPrimitive normal = TexturedPrimitive("oot3d/native/ocarina");
    UiPrimitive mask = normal;
    mask.texture_alpha_only = true;
    Require(BuildUiPrimitiveTextureKey(mask) != BuildUiPrimitiveTextureKey(normal),
            "an alpha-only texture shares the ordinary texture's key");
    const std::vector<UiPrimitive> primitives = {normal, mask};
    DarkProvider provider;
    PixelRecordingBackend backend;
    N64UiFast3dRenderer renderer(backend, &provider);
    std::string error;
    Require(renderer.Render(primitives, 400, 240,
                            N64UiCanvasMode::NativeTopScreen400x240,
                            renderer.PrepareTextures(primitives), &error),
            "alpha-only UI rendering failed");
    const std::vector<uint8_t> original{0x10, 0x20, 0x30, 0x00, 0x00, 0x00, 0x00, 0xFF};
    const std::vector<uint8_t> white{0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    Require(backend.uploads.size() == 2U && backend.uploads[0] == original &&
                backend.uploads[1] == white,
            "the alpha-only texture did not keep only the texel alpha");
    Require(backend.drawn == std::vector<uint32_t>({1U, 2U}),
            "the alpha-only primitive did not draw its own texture");
}

class SaturatingBackend final : public Oot3d::Renderer::UiRenderBackend {
  public:
    bool PrepareUiOverlay(const Oot3d::Renderer::UiOverlayViewport&,
                          std::string*) override {
        return true;
    }
    bool IsUiFramebufferYInverted() const noexcept override { return false; }
    uint32_t CreateUiTexture(std::span<const uint8_t>, uint16_t, uint16_t,
                             std::string*) override {
        return 1U;
    }
    void DeleteUiTexture(uint32_t) noexcept override {}
    bool DrawUiTriangles(std::span<const Oot3d::Renderer::UiOverlayVertex> vertices,
                         uint32_t, std::string*) override {
        draws.push_back({false, vertices.front().Alpha});
        return true;
    }
    bool DrawUiTrianglesSaturated(std::span<const Oot3d::Renderer::UiOverlayVertex> vertices,
                                  uint32_t, std::string*) override {
        draws.push_back({true, vertices.front().Alpha});
        return true;
    }
    std::vector<std::pair<bool, float>> draws;
};

// A texture alpha scale multiplies the vertex alpha and asks the backend for
// its saturating combiner; scale 1 keeps the ordinary path, and a backend
// without that combiner draws the alpha clamped to 1.
void TestTextureAlphaScale() {
    using namespace oot3d::ui;
    UiPrimitive ordinary = TexturedPrimitive("oot3d/native/ocarina");
    ordinary.color.alpha = 0.5F;
    UiPrimitive scaled = ordinary;
    scaled.texture_alpha_scale = 2.0F;
    UiPrimitive solid;
    solid.destination = {0.0F, 0.0F, 8.0F, 8.0F};
    solid.texture_alpha_scale = 2.0F;
    const std::vector<UiPrimitive> primitives = {ordinary, scaled, solid};
    CountingProvider provider;
    SaturatingBackend backend;
    N64UiFast3dRenderer renderer(backend, &provider);
    std::string error;
    Require(renderer.Render(primitives, 400, 240, N64UiCanvasMode::NativeTopScreen400x240,
                            renderer.PrepareTextures(primitives), &error),
            "alpha-scaled UI rendering failed");
    Require(backend.draws.size() == 3U && !backend.draws[0].first && Near(backend.draws[0].second, 0.5F) &&
                backend.draws[1].first && Near(backend.draws[1].second, 1.0F) &&
                !backend.draws[2].first && Near(backend.draws[2].second, 1.0F),
            "texture alpha scale did not reach the saturating combiner");
    UiPrimitive opaque = scaled;
    opaque.color.alpha = 1.0F;
    VertexRecordingBackend fallback;
    N64UiFast3dRenderer plain(fallback, &provider);
    const std::vector<UiPrimitive> single = {opaque};
    Require(plain.Render(single, 400, 240, N64UiCanvasMode::NativeTopScreen400x240,
                         plain.PrepareTextures(single), &error) &&
                fallback.draws.size() == 1U && Near(fallback.draws[0][0].Alpha, 1.0F),
            "a backend without a saturating combiner did not clamp the scaled alpha");
}

} // namespace

int main() {
    using namespace oot3d::ui;
    Require(N64UiSolidShaderId0() != 0U &&
                N64UiTexturedShaderId0() != 0U &&
                N64UiSolidShaderId0() != N64UiTexturedShaderId0(),
            "solid and textured shader contracts are not distinct");
    Require(N64UiShaderId1() == 1U,
            "N64 UI alpha shader option changed");
    Require(ResolveN64UiTexturePath("n64/file_select/highlight") ==
                "__OTR__textures/title_static/gFileSelBigButtonHighlightTex",
            "file-select texture semantic did not resolve to the N64 asset");
    Require(ResolveN64UiTexturePath("n64/name_entry/backspace") ==
                "__OTR__textures/title_static/gFileSelBackspaceButtonTex" &&
                ResolveN64UiTexturePath(
                    "n64/name_entry/highlight/character") ==
                    "__OTR__textures/title_static/gFileSelCharHighlightTex",
            "name-entry texture semantics did not resolve to N64 assets");
    Require(ResolveN64UiTexturePath("n64/name_entry/confirm/prompt") ==
                "__OTR__textures/title_static/gFileSelAreYouSureENGTex" &&
                ResolveN64UiTexturePath("n64/name_entry/confirm/yes") ==
                    "__OTR__textures/title_static/gFileSelYesButtonENGTex" &&
                ResolveN64UiTexturePath("n64/name_entry/confirm/quit") ==
                    "__OTR__textures/title_static/gFileSelQuitButtonENGTex",
            "name-entry confirmation did not resolve to N64 assets");
    Require(ResolveN64UiTexturePath("n64/file_select/missing").empty(),
            "unknown UI texture semantic unexpectedly resolved");
    UiTextureIdentity nativeIdentity;
    nativeIdentity.semantic_name =
        "oot3d/native/pause_shared/pause_top_page";
    nativeIdentity.guest_resource_address = 0x08123456U;
    nativeIdentity.guest_surface_address = 0x20100000U;
    Require(BuildUiTextureCacheKey(nativeIdentity) ==
                "oot3d/native/pause_shared/pause_top_page@08123456:20100000",
            "native texture cache identity omitted a guest address");
    UiTextureIdentity relocatedIdentity = nativeIdentity;
    relocatedIdentity.guest_surface_address += 0x1000U;
    Require(BuildUiTextureCacheKey(relocatedIdentity) !=
                BuildUiTextureCacheKey(nativeIdentity),
            "relocated native texture reused a stale cache identity");
    UiTextureIdentity archiveIdentity;
    archiveIdentity.semantic_name = "n64/file_select/highlight";
    Require(BuildUiTextureCacheKey(archiveIdentity) ==
                archiveIdentity.semantic_name,
            "archive texture cache identity unexpectedly changed");
    TestPreparedTextures();
    TestProvisionalTextures();
    TestFreeCorners();
    TestAlphaOnlyTexture();
    TestTextureAlphaScale();
    std::cout << "oot3d_n64_ui_renderer_tests: ok\n";
    return 0;
}
