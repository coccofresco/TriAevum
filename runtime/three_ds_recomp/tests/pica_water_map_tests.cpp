#include "fast/renderer3ds/pica_water_map.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Fast::Renderer3ds;

static void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static int run() {
    int checks = 0;
    // A river with a step: an upper piece (y 50, z 0-100), a vertical drop at
    // z 100 and a lower piece (y 0, z 100-200), placed 10 units along x.
    const std::vector<std::array<float, 3>> positions{
        {0, 50, 0}, {100, 50, 0}, {100, 50, 100}, {0, 50, 100},  // upper
        {0, 0, 100}, {100, 0, 100}, {100, 0, 200}, {0, 0, 200},  // lower
        {0, 30, 100}, {100, 30, 100},                            // the drop's middle
        {0, 0, 0}, {50, 0, 30}, {100, 9, 0},                     // a gentle slope (rapids)
    };
    const std::vector<uint32_t> indices{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 3, 2, 9, 3, 9, 8, 8, 9, 5, 8, 5, 4,
                                        10, 11, 12};
    std::array<float, 16> world{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 0, 0, 1};
    const auto split = SplitPicaWaterTriangles(positions, indices, world);
    expect(split.Level.size() == 5U && split.Falling.size() == 4U, "lying faces (the rapids too) and falling ones");
    expect(split.Level[0][0][0] == 10.0F, "world placement");
    const PicaWaterTriangles* sets[] = {&split};
    const auto map = BuildPicaWaterMap(sets);
    expect(map.Size == kPicaWaterMapSize && map.Texel >= kPicaWaterMapMinimumTexel, "map extent");
    const auto* upper = map.At(60, 50);
    const auto* lower = map.At(60, 150);
    expect(upper && std::abs((*upper)[0] - 50.0F) < 1.0e-3F, "upper piece height");
    expect(lower && std::abs((*lower)[0]) < 1.0e-3F, "lower piece height");
    expect(map.At(60, 106) && (*map.At(60, 106))[1] > 0.5F, "the drop lands foam on the lower piece");
    expect(upper && (*upper)[1] == 0.0F, "none above the drop");
    expect(lower && (*lower)[1] == 0.0F, "none far from the drop");
    expect(map.At(200, 50) && std::isnan((*map.At(200, 50))[0]), "no water outside the pieces");
    expect(map.At(-1000, 0) == nullptr, "outside the map");
    expect(map.Levels.size() == 2U && std::abs(map.Levels[0]) < 1.0F && std::abs(map.Levels[1] - 50.0F) < 1.0F,
           "two levels");
    const PicaWaterTriangles* none[] = {};
    expect(BuildPicaWaterMap(std::span<const PicaWaterTriangles* const>(none, 0)).Size == 0U, "no water, no map");
    checks += 11;
    // Regions: the two pieces and the drop's landing.
    const auto regions = PicaWaterMapRegions(map);
    size_t levels = 0, landings = 0;
    for (const auto& region : regions) {
        (region.Landing ? landings : levels) += 1U;
        if (region.Landing)
            expect(std::abs(region.Height) < 1.0e-3F && region.MinimumXZ[1] > 99.0F && region.MinimumXZ[1] < 106.0F,
                   "landing on the lower piece below the drop");
    }
    expect(levels == 2U && landings == 1U, "two pieces, one landing");
    expect(!regions.empty() && !regions[0].Landing && regions[0].Texels > regions.back().Texels, "largest first");
    checks += 3;
    // A stream running downhill (y 100 at z 0 to y 40 at z 300, about 11
    // degrees): one continuous surface, its slope in every texel.
    {
        const std::vector<std::array<float, 3>> stream{{0, 100, 0}, {80, 100, 0}, {80, 40, 300}, {0, 40, 300}};
        const std::vector<uint32_t> quad{0, 1, 2, 0, 2, 3};
        const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const auto sloped = SplitPicaWaterTriangles(stream, quad, identity);
        expect(sloped.Level.size() == 2U && sloped.Falling.empty(), "a stream's slope lies, it does not fall");
        const PicaWaterTriangles* streamSets[] = {&sloped};
        const auto streamMap = BuildPicaWaterMap(streamSets);
        const auto* middle = streamMap.At(40, 150);
        expect(middle && std::abs((*middle)[0] - (100.0F - 0.2F * 150.0F)) < 0.2F * streamMap.Texel,
               "its height along the slope");
        expect(middle && std::abs((*middle)[2]) < 1.0e-4F && std::abs((*middle)[3] + 0.2F) < 1.0e-4F,
               "its slope (d height / dz = -0.2)");
        expect(streamMap.HeightRange[0] <= 40.0F && streamMap.HeightRange[0] > 30.0F &&
                   streamMap.HeightRange[1] >= 100.0F && streamMap.HeightRange[1] < 110.0F,
               "the height range");
        size_t pieces = 0;
        for (const auto& region : PicaWaterMapRegions(streamMap)) pieces += region.Landing ? 0U : 1U;
        expect(pieces == 1U, "one piece, not a band per height");
        checks += 5;
    }
    std::cout << "pica_water_map_tests: " << checks << " checks passed\n";
    return 0;
}

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << "pica_water_map_tests failed: " << error.what() << '\n';
        return 1;
    }
}
