#include "fast/oot3d/grass_texture_source_cache.h"
#include <array>
#include <iostream>
#include <stdexcept>

int main() try {
    using namespace Fast::Oot3d;
    const auto check = [](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    constexpr std::array<uint8_t, 8> pixels{255, 32, 16, 255, 0, 0, 255, 0};
    uint64_t decoded = 14695981039346656037ULL;
    for (auto byte : pixels) decoded = (decoded ^ byte) * 1099511628211ULL;
    auto& cache = GrassTextureSourceCache::Instance();
    cache.Clear();
    cache.ObserveDecoded(91, 2, 1, pixels);
    const auto mask = cache.AcquireMaskShared(91, GrassSampleChannel::Red);
    check(mask && mask->Samples == std::vector<uint8_t>({255, 0}), "native mask before alias query");
    check(cache.AcquireAverageColor(91)->at(0) == 1.0F, "native average before alias query");
    check(cache.AcquireColorSource(91).Grid != nullptr, "native color grid before alias query");
    cache.ObserveDecoded(92, 2, 1, pixels);
    cache.ObserveDecoded(91, 2, 1, pixels);
    check(cache.ResolveObservedHash(decoded, 2, 1) == 91, "pending last observation wins");
    check(cache.ResolveObservedHash(decoded, 1, 2) == decoded, "dimensions are part of alias identity");
    cache.ObserveDecoded(92, 2, 1, pixels);
    check(cache.ResolveObservedHash(decoded, 2, 1) == 92, "observation after index activation");
    cache.ObserveDecoded(93, 2, 1, pixels);
    check(cache.ResolveObservedHash(decoded, 2, 1) == 93, "new source after index activation");
    cache.ObserveDecoded(95, 1, 2, pixels);
    check(cache.ResolveObservedHash(decoded, 1, 2) == 95, "new source for a previously empty dimension");
    check(cache.ResolveObservedHash(decoded, 2, 1) == 93, "dimension indexes remain independent");
    check(mask == cache.AcquireMaskShared(91, GrassSampleChannel::Red), "alias queries preserve masks");
    cache.Clear();
    check(cache.ResolveObservedHash(decoded, 2, 1) == decoded, "clear removes pending and materialized aliases");
    cache.ObserveDecoded(94, 2, 1, pixels);
    check(cache.ResolveObservedHash(decoded, 2, 1) == 94, "observation after an empty lookup");
    cache.Clear();
    cache.ObserveDecoded(201, 2, 1, pixels);
    cache.ObserveDecoded(202, 1, 2, pixels);
    cache.ObserveDecoded(201, 1, 2, pixels);
    check(cache.ResolveObservedHash(decoded, 1, 2) == 201,
          "materializing older observations cannot replace a newer alternate-dimension alias");
    check(cache.ResolveObservedHash(decoded, 2, 1) == 201, "original dimension alias retained");
    auto alternate = pixels;
    alternate[0] = 42;
    uint64_t alternateHash = 14695981039346656037ULL;
    for (auto byte : alternate) alternateHash = (alternateHash ^ byte) * 1099511628211ULL;
    cache.ObserveDecoded(201, 2, 1, alternate);
    check(cache.ResolveObservedHash(alternateHash, 2, 1) == 201, "alternate pixel interpretation alias");
    check(cache.ResolveObservedHash(decoded, 2, 1) == 201, "original pixel interpretation alias retained");
    check(cache.AcquireMaskShared(201, GrassSampleChannel::Red)->Samples[0] == 255,
          "alternate interpretations preserve the existing immutable mask source");
    cache.Clear();
    std::cout << "Texture source and lazy decoded aliases verified\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
