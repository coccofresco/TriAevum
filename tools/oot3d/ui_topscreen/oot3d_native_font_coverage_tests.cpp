#include "oot3d_native_font_coverage.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace Oot3dNativeGame;
namespace {
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::vector<std::uint8_t> Font(std::uint8_t size) {
    std::vector<std::uint8_t> bytes(32 + size * size, 0x1f);
    const std::uint8_t header[] = {'Q','B','F','1',2,0,2,0,42,0,0,0,4,size,size,2,
                                 32,0,0,0,6,4,0,0,42,0,1,0,5,6,0,0};
    std::copy(std::begin(header), std::end(header), bytes.begin());
    return bytes;
}
}
int main() {
    try {
        auto native = Font(16), hd = Font(64);
        NativeFontCoverage font;
        std::string error;
        Check(font.Load(native, hd, &error), "valid QBF");
        Check(font.Density() == 4 && font.LogicalWidth() == 16 && font.LogicalHeight() == 16,
              "separate logical and coverage extent");
        Check(font.Glyph(32).size() == 4096 && font.Glyph(32)[0] == 17 && font.Glyph(32)[1] == 255,
              "native A4 nibble order");
        Check(font.Glyph(33).empty() && font.Glyph(0xffffffff).empty(), "exact glyph offsets only");
        for (const auto offset : {0, 4, 6, 8, 12, 13, 14, 15, 18, 24}) {
            auto bad = hd;
            bad[offset] = 255;
            Check(!font.Load(native, bad, &error), "reject malformed QBF");
            Check(font.Density() == 4, "failed load keeps prior valid font");
        }
        Check(!font.Load(native, std::span(hd).first(hd.size() - 1), &error), "reject truncation");
        Check(!font.Load(native, Font(24), &error), "reject fractional density");
        auto alias = native;
        alias[26] = 0;
        auto different = hd;
        different[32 + 2048] = 0;
        Check(!font.Load(alias, different, &error), "reject conflicting native aliases");
        NativeFontCoverageAtlas atlas;
        Check(!atlas.Reset(0, 256, 256, 4), "epoch required");
        Check(!atlas.Reset(1, 4096, 4096, 32), "bounded allocation");
        Check(atlas.Reset(1, 32, 32, 4), "atlas allocation");
        auto empty = atlas.Snapshot();
        Check(atlas.Blit(font, 32, 16, 0), "blit at native logical coordinates");
        auto drawn = atlas.Snapshot();
        Check(drawn.Width == 128 && drawn.Height == 128, "HD atlas extent");
        Check((*drawn.Coverage)[63] == 0 && (*drawn.Coverage)[64] == 17 &&
              (*drawn.Coverage)[65] == 255 && (*drawn.Coverage)[64 * 128 + 64] == 0,
              "coverage occupies exactly scaled logical rectangle");
        Check((*empty.Coverage)[64] == 0, "queued snapshot is immutable");
        Check(atlas.Blit(font, 32, 16, 0), "repeat blit");
        Check(atlas.Snapshot().Coverage == drawn.Coverage &&
              atlas.Snapshot().Generation == drawn.Generation, "identical blit does not rebuild");
        Check(!atlas.Blit(font, 32, 17, 0) && !atlas.Blit(font, 33, 0, 0) &&
              !atlas.Clear(0xffffffff, 0, 16, 16), "reject invalid ranges without mutation");
        Check(atlas.Clear(16, 0, 8, 16), "partial clear");
        auto cleared = atlas.Snapshot();
        Check((*cleared.Coverage)[64] == 0 && (*cleared.Coverage)[96] == 17 &&
              (*drawn.Coverage)[64] == 17, "partial clear preserves neighbor and prior snapshot");
        Check(atlas.Clear(16, 0, 8, 16) && atlas.Snapshot().Generation == cleared.Generation,
              "empty clear does not rebuild");
        Check(atlas.Reset(2, 32, 32, 4), "restore/reallocation resets surface");
        Check(atlas.Snapshot().Epoch == 2 && (*atlas.Snapshot().Coverage)[96] == 0,
              "no stale coverage after reset");
        std::cout << "Native font coverage and atlas lifecycle checks passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
