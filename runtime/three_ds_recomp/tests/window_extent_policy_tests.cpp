#include "../include/fast/WindowExtentPolicy.h"
#include <stdexcept>

int main() {
    using Fast::WindowExtent;
    using Fast::WindowWorkArea;
    const auto require = [](bool value) { if (!value) throw std::runtime_error("window extent policy regression"); };

    // The stored F1 request wins; launch arguments only seed a first run.
    require(Fast::ResolveInitialWindowExtent(WindowExtent{1920, 1080}, {1280, 720}) == WindowExtent{1920, 1080});
    require(Fast::ResolveInitialWindowExtent(std::nullopt, {1280, 720}) == WindowExtent{1280, 720});
    // A damaged stored request cannot open an unusable window.
    require(Fast::ResolveInitialWindowExtent(WindowExtent{0, 1080}, {1280, 720}) == WindowExtent{1280, 720});
    require(Fast::ResolveInitialWindowExtent(WindowExtent{100, 100}, {1280, 720}) == WindowExtent{1280, 720});

    // Issue #111: 1360x768 with a 40 px task bar and Windows decorations.
    const WindowWorkArea small{1360, 728, 16, 39};
    const auto fitted = Fast::FitWindowedExtent({1280, 720}, small);
    require(fitted.Height == 728 - 39 && fitted.Width + 16 <= 1360);
    require(fitted.Width * 9 / 16 == fitted.Height || fitted.Width * 9 / 16 + 1 == fitted.Height);
    // A request that fits is untouched, whatever its shape.
    require(Fast::FitWindowedExtent({1280, 720}, {1920, 1032, 16, 39}) == WindowExtent{1280, 720});
    require(Fast::FitWindowedExtent({1560, 720}, {3840, 2064, 24, 58}) == WindowExtent{1560, 720});
    // A width-limited request keeps its shape: 4K windowed on a scaled 1080p work area.
    const auto wide = Fast::FitWindowedExtent({3840, 1080}, {1920, 1032, 16, 39});
    require(wide.Width == 1904 && wide.Height == 535);
    // At 150 % the decorations grow with the monitor scale; still inside.
    const auto scaled = Fast::FitWindowedExtent({2560, 1440}, {2560, 1380, 24, 58});
    require(scaled.Height == 1322 && scaled.Width + 24 <= 2560);
    // An unknown work area changes nothing; a tiny one keeps the renderer's minimum.
    require(Fast::FitWindowedExtent({1280, 720}, {}) == WindowExtent{1280, 720});
    require(Fast::FitWindowedExtent({1280, 720}, {300, 200, 0, 0}) == Fast::kMinimumWindowExtent);

    // A fresh default request on a 1366x768 screen (40 px task bar, Windows
    // decorations): 16:9 inside 90 % of what is left, title bar included.
    const auto fresh = Fast::DefaultWindowedExtent({1366, 728, 16, 39});
    require(fresh == WindowExtent{1102, 620});
    require(fresh.Width + 16 <= 1366 && fresh.Height + 39 <= 728);
    // 1080p with a task bar, and an ultrawide screen: height-limited, even sizes.
    require(Fast::DefaultWindowedExtent({1920, 1032, 16, 39}) == WindowExtent{1586, 892});
    require(Fast::DefaultWindowedExtent({3440, 1400, 16, 39}) == WindowExtent{2176, 1224});
    // A tall work area is width-limited.
    require(Fast::DefaultWindowedExtent({1000, 2000, 0, 0}) == WindowExtent{900, 506});
    // An unknown work area keeps the product default; a tiny one the minimum.
    require(Fast::DefaultWindowedExtent({}) == Fast::kProductDefaultWindowExtent);
    require(Fast::DefaultWindowedExtent({300, 200, 0, 0}) == Fast::kMinimumWindowExtent);
    // Configurations from before the provenance: the product default is fresh.
    using Fast::WindowExtentProvenance;
    require(Fast::InferWindowExtentProvenance(true, {1280, 720}) == WindowExtentProvenance::Default);
    require(Fast::InferWindowExtentProvenance(true, {1920, 1080}) == WindowExtentProvenance::User);
    require(Fast::InferWindowExtentProvenance(false, {1280, 720}) == WindowExtentProvenance::User);
}
