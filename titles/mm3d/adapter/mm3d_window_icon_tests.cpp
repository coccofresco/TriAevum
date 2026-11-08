// The game window's icon comes from the active title: OoT3D (no title icon)
// keeps the compiled-in game icon, MM3D's session gives its own image.
#include "mm3d_window_icon.h"
#include "triaevum_window_icon.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// FNV-1a over the pixels: a fingerprint printed for the receipt.
uint64_t Fingerprint(const Fast::WindowIconImage& icon) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size_t{icon.Width} * icon.Height * 4; ++i) {
        hash = (hash ^ icon.Rgba[i]) * 1099511628211ull;
    }
    return hash;
}

uint64_t OpaquePixels(const Fast::WindowIconImage& icon) {
    uint64_t count = 0;
    for (size_t i = 0; i < size_t{icon.Width} * icon.Height; ++i) {
        count += icon.Rgba[i * 4 + 3] > 128 ? 1 : 0;
    }
    return count;
}

} // namespace

int main() {
    const Fast::WindowIconImage& oot3d = TriAevumWindowIcon();
    const Fast::WindowIconImage& mm3d = Oot3dNativeGame::Mm3dWindowIcon();
    Check(oot3d.Width == 64 && oot3d.Height == 64 && oot3d.Rgba != nullptr, "OoT3D icon is 64x64");
    Check(mm3d.Width == 64 && mm3d.Height == 64 && mm3d.Rgba != nullptr, "MM3D icon is 64x64");
    // OoT3D: no title icon, the game icon itself (same pixels, same object).
    Check(&ResolveWindowIcon(nullptr) == &oot3d, "OoT3D title (no title icon) resolves to the game icon");
    // MM3D: its session's icon.
    Check(&ResolveWindowIcon(&mm3d) == &mm3d, "MM3D title resolves to its own icon");
    Check(std::memcmp(oot3d.Rgba, mm3d.Rgba, 64 * 64 * 4) != 0, "MM3D pixels differ from OoT3D's");
    // Both are round emblems on transparency: corners clear, a solid body.
    Check(oot3d.Rgba[3] == 0 && mm3d.Rgba[3] == 0, "top-left corner is transparent");
    Check(OpaquePixels(oot3d) > 1500 && OpaquePixels(mm3d) > 1500, "emblems are mostly opaque");
    std::printf("oot3d fnv1a=%016llx opaque=%llu\n", static_cast<unsigned long long>(Fingerprint(oot3d)),
                static_cast<unsigned long long>(OpaquePixels(oot3d)));
    std::printf("mm3d  fnv1a=%016llx opaque=%llu\n", static_cast<unsigned long long>(Fingerprint(mm3d)),
                static_cast<unsigned long long>(OpaquePixels(mm3d)));
    std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
