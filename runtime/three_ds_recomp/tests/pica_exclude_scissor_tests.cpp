// Exact coverage and no overlap for every hole placement, including empty,
// covering, edge-touching and partially out-of-bounds rectangles.
#include "fast/renderer3ds/pica_exclude_scissor.h"

#include <cstdio>
#include <cstdlib>

using Fast::Renderer3ds::ExcludePicaScissor;
using Fast::Renderer3ds::PicaScissorRect;

namespace {

bool Contains(PicaScissorRect r, int x, int y) {
    return x >= r.X && y >= r.Y && int64_t{ x } < int64_t{ r.X } + r.Width && int64_t{ y } < int64_t{ r.Y } + r.Height;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "pica_exclude_scissor_tests: %s\n", message);
        std::exit(1);
    }
}

} // namespace

int main() {
    for (int hx = -3; hx < 12; ++hx)
        for (int hy = -3; hy < 10; ++hy)
            for (unsigned hw = 0; hw < 14; ++hw)
                for (unsigned hh = 0; hh < 12; ++hh) {
                    const PicaScissorRect bounds{ 1, 2, 8, 6 };
                    const PicaScissorRect hole{ hx, hy, hw, hh };
                    const auto parts = ExcludePicaScissor(bounds, hole);
                    Require(parts.Count <= 4, "at most four rectangles");
                    for (int x = -4; x < 15; ++x)
                        for (int y = -4; y < 15; ++y) {
                            int count = 0;
                            for (unsigned i = 0; i < parts.Count; ++i)
                                count += Contains(parts.Rects[i], x, y);
                            Require(count == int(Contains(bounds, x, y) && !Contains(hole, x, y)),
                                    "every retained fragment is covered exactly once");
                        }
                }
    const auto large = ExcludePicaScissor({ 0, 0, 0xFFFFFFFFU, 0xFFFFFFFFU }, { 0, 0, 0xFFFFFFFFU, 0xFFFFFFFFU });
    Require(large.Count == 0, "a covering hole leaves nothing");
    std::puts("pica_exclude_scissor_tests passed");
    return 0;
}
