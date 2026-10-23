#include "fast/oot3d/hiz_reflection.h"

#include <array>
#include <iostream>
#include <stdexcept>

using namespace Fast::Oot3d;
using Fast::Renderer3ds::PicaSceneWaterBodies;
using Fast::Renderer3ds::PicaWaterMap;

static void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static PicaSceneWaterBodies body(std::array<float, 2> minimum, std::array<float, 2> maximum, float height) {
    PicaSceneWaterBodies bodies;
    bodies.Bodies[0] = {minimum, maximum, height, 0U};
    bodies.Count = 1U;
    return bodies;
}

static PicaWaterMap map(std::array<float, 2> minimum, std::array<float, 2> maximum, float low, float high) {
    PicaWaterMap result;
    result.Size = 4U;
    result.Texel = 1.0F;
    result.Levels = {low};
    result.HeightRange = {low, high};
    result.ExtentMinimumXZ = minimum;
    result.ExtentMaximumXZ = maximum;
    return result;
}

static int run() {
    int checks = 0;
    // The camera at the origin looking down -z, a 90 degree view (the
    // trace's projection: ndc = xy / w), far plane 1000.
    const std::array<float, 16> view{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 16> projection{};
    projection[0] = projection[5] = 1.0F;
    const float far = 1000.0F;
    const auto inView = [&](const PicaSceneWaterBodies* bodies, const PicaWaterMap* water) {
        return HiZWaterPossiblyInView(&view, projection, far, bodies, water);
    };
    const auto ahead = body({-10, -100}, {10, -50}, -5.0F);
    expect(inView(&ahead, nullptr), "a body ahead is in view");
    const auto behind = body({-10, 50}, {10, 100}, -5.0F);
    expect(!inView(&behind, nullptr), "a body behind the camera is not");
    const auto aside = body({500, -100}, {600, -50}, -5.0F);
    expect(!inView(&aside, nullptr), "a body beyond the right edge is not");
    const auto below = body({-10, -100}, {10, -50}, -500.0F);
    expect(!inView(&below, nullptr), "a body far below the bottom edge is not");
    const auto beyond = body({-10, -2000}, {10, -1500}, -5.0F);
    expect(inView(&beyond, nullptr), "a body beyond the far plane counts (its search has no far bound)");
    const auto wide = body({-500, -100}, {500, -50}, -5.0F);
    expect(inView(&wide, nullptr), "a body straddling the view is in view");
    PicaSceneWaterBodies none;
    expect(!inView(&none, nullptr) && !inView(nullptr, nullptr), "no water, nothing in view");
    checks += 7;
    const auto pond = map({-10, -100}, {10, -50}, -6.0F, -4.0F);
    expect(inView(nullptr, &pond), "drawn water ahead is in view");
    const auto pondBehind = map({-10, 50}, {10, 100}, -6.0F, -4.0F);
    expect(!inView(nullptr, &pondBehind), "drawn water behind the camera is not");
    const auto pondBeyond = map({-10, -2000}, {10, -1500}, -6.0F, -4.0F);
    expect(!inView(nullptr, &pondBeyond), "drawn water beyond the far plane is not (its search stops there)");
    const auto pondAside = map({-600, -100}, {-500, -50}, -6.0F, -4.0F);
    expect(!inView(nullptr, &pondAside), "drawn water beyond the left edge is not");
    PicaWaterMap empty;
    expect(!inView(nullptr, &empty), "an empty map holds no water");
    expect(HiZWaterPossiblyInView(nullptr, projection, far, &none, &empty), "without a view, water is assumed");
    expect(inView(&behind, &pond) && inView(&ahead, &pondBehind), "either the bodies or the map in view suffices");
    checks += 7;
    // A rotated view (the camera turned so that world +x is ahead: view z =
    // -x, view x = z; column-major): the same bodies judged in view space.
    const std::array<float, 16> turned{0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1};
    expect(!HiZWaterPossiblyInView(&turned, projection, far, &ahead, nullptr), "turned away from the body");
    const auto right = body({50, -10}, {100, 10}, -5.0F);
    expect(HiZWaterPossiblyInView(&turned, projection, far, &right, nullptr), "turned towards the body");
    checks += 2;
    std::cout << "hiz_water_view_tests: " << checks << " checks passed\n";
    return 0;
}

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << "hiz_water_view_tests failed: " << error.what() << '\n';
        return 1;
    }
}
