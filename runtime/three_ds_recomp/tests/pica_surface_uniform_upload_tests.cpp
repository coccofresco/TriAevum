#include "fast/renderer3ds/pica_surface_uniform_upload.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

using namespace Fast::Renderer3ds;
namespace {
size_t checks = 0;
void Require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
uint64_t Aligned(uint64_t bytes, uint64_t alignment) {
    return (bytes + alignment - 1) & ~(alignment - 1);
}
}
int main() {
    Require(PicaSurfaceUniformUploadSize(false, false) == 0, "inactive plain reserves nothing");
    Require(PicaSurfaceUniformUploadSize(false, true) == 0, "inactive balance reserves nothing");
    Require(PicaSurfaceUniformUploadSize(true, false) == sizeof(PicaSurfaceLightingUniforms), "legacy range unchanged");
    Require(PicaSurfaceUniformUploadSize(true, true) == sizeof(PicaBalancedSurfaceUniforms), "balance tail reserved");
    // At an arena boundary the old plain-size reservation was one 256-byte
    // allocation unit short. Test all supported alignment shapes, not just 256.
    for (uint64_t alignment : {16U, 64U, 256U, 1024U}) {
        const uint64_t capacity = 65536;
        const uint64_t reserve = Aligned(PicaSurfaceUniformUploadSize(true, true), alignment);
        const uint64_t start = capacity - reserve;
        Require(Aligned(start, alignment) + sizeof(PicaBalancedSurfaceUniforms) <= capacity,
                "reservation covers the exact upload at the arena end");
    }
    Require(Aligned(sizeof(PicaBalancedSurfaceUniforms), 256) >
            Aligned(sizeof(PicaSurfaceLightingUniforms), 256), "fixture reproduces old under-reservation");

    PicaBalancedSurfaceUploadCache cache;
    PicaBalancedSurfaceUniforms value{};
    std::vector<uint8_t> arena;
    size_t uploads = 0;
    auto upload = [&](std::span<const uint8_t> bytes) -> uint64_t {
        ++uploads;
        const auto offset = arena.size();
        arena.insert(arena.end(), bytes.begin(), bytes.end());
        return offset;
    };
    Require(cache.Resolve(value, upload) == 0, "offset zero is a valid cached allocation");
    Require(cache.Resolve(value, upload) == 0 && uploads == 1, "equal payload reuses upload");
    // Exhaustive byte-key sensitivity includes every native fragment balance
    // control/gain and every point/placed/water/shadow/material field. No
    // hash collision or float epsilon can hide a changed lighting input.
    for (size_t i = 0; i < sizeof(value); ++i) {
        auto changed = value;
        reinterpret_cast<uint8_t*>(&changed)[i] ^= 1U;
        const auto before = uploads;
        const auto offset = cache.Resolve(changed, upload);
        Require(uploads == before + 1, "changed byte invalidates");
        Require(cache.Resolve(changed, upload) == offset && uploads == before + 1, "changed payload then reuses");
        Require(std::memcmp(arena.data() + offset, &changed, sizeof(changed)) == 0, "uploaded bytes exact");
        cache.Resolve(value, upload);
    }
    auto before = uploads;
    cache.Reset(); // frame recycling
    cache.Resolve(value, upload);
    Require(uploads == before + 1, "new frame cannot reuse the prior offset");
    cache.Reset(); // arena replacement, same frame
    arena.clear();
    before = uploads;
    Require(cache.Resolve(value, upload) == 0 && uploads == before + 1, "new arena cannot reuse retired storage");

    auto changed = value;
    changed.Balance.Parameters[0] = 0.5F;
    bool threw = false;
    try {
        cache.Resolve(changed, [](std::span<const uint8_t>) -> uint64_t { throw std::runtime_error("full"); });
    } catch (const std::runtime_error&) { threw = true; }
    Require(threw, "upload failure propagated");
    before = uploads;
    Require(cache.Resolve(value, upload) == 0 && uploads == before, "failed upload preserves last good entry");
    cache.Resolve(changed, upload);
    Require(uploads == before + 1, "failed payload must retry its upload");
    std::printf("PASS %zu checks; plain=%zu balanced=%zu aligned256=%llu/%llu\n", checks,
        sizeof(PicaSurfaceLightingUniforms), sizeof(PicaBalancedSurfaceUniforms),
        static_cast<unsigned long long>(Aligned(sizeof(PicaSurfaceLightingUniforms), 256)),
        static_cast<unsigned long long>(Aligned(sizeof(PicaBalancedSurfaceUniforms), 256)));
}
