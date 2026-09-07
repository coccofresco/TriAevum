#include "oot3d_n64_ui_renderer.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

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
    std::cout << "oot3d_n64_ui_renderer_tests: ok\n";
    return 0;
}
