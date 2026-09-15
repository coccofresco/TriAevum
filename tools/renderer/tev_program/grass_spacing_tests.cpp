#include "fast/oot3d/grass_surface_extractor.h"
#include <iostream>
#include <stdexcept>

using namespace Fast::Oot3d;

bool Same(const GrassAnchor& a, const GrassAnchor& b) {
    return a.SurfaceReference == b.SurfaceReference && a.LocalPosition == b.LocalPosition &&
        a.LocalNormal == b.LocalNormal && a.Uv == b.Uv && a.BladeHeight == b.BladeHeight &&
        a.BladeWidth == b.BladeWidth && a.Phase == b.Phase && a.WidthAxis == b.WidthAxis &&
        a.StableId == b.StableId;
}

int main() try {
    std::vector<GrassSourceVertex> vertices;
    std::vector<uint32_t> indices;
    for (float y : {-50.0F, -0.3F, 0.0F, 0.3F, 50.0F}) {
        const auto base = static_cast<uint32_t>(vertices.size());
        for (auto p : {std::array<float, 3>{-100,y,-100}, {100,y,-100}, {-100,y,100}, {100,y,100}}) {
            GrassSourceVertex v;
            v.Position = p;
            v.Uv = {p[0] / 100.0F, p[2] / 100.0F};
            vertices.push_back(v);
        }
        for (auto i : {0U,2U,1U,1U,2U,3U}) indices.push_back(base + i);
    }
    GrassSourceSurface surface;
    surface.GeometryId = 917;
    surface.Vertices = vertices;
    surface.Indices = indices;
    GrassPlacementRule rule;
    GrassScalarMask mask;
    mask.Width = mask.Height = 2;
    size_t verified = 0;
    for (bool transformed : {false, true}) for (bool holes : {false, true})
    for (float randomness : {0.0F, 0.5F, 1.0F})
    for (float spacing : {0.6F, 8.0F, 31.0F}) {
        surface.TransformBakedIntoVertices = !transformed;
        surface.ModelToWorld = {2,0,0,-10, 0,1,0,17, 0,0,0.5F,-23, 0,0,0,1};
        const auto world = [transformed](const GrassAnchor& anchor) {
            const auto& p = anchor.LocalPosition;
            return transformed ? std::array<float,3>{2*p[0]-10, p[1]+17, 0.5F*p[2]-23} : p;
        };
        mask.Samples = holes ? std::vector<uint8_t>{255,0,128,255} : std::vector<uint8_t>(4,255);
        GrassGenerationSettings generation;
        generation.InstancesPerSquareMeter = 4096;
        generation.IndividualRandomness = randomness;
        generation.MinimumSpacing = 0;
        auto candidates = GrassSurfaceExtractor::Extract(surface, rule, generation, mask, 2500);
        std::vector<GrassAnchor> expected;
        for (const auto& candidate : candidates) {
            const auto position = world(candidate);
            bool accepted = true;
            for (const auto& previous : expected) {
                const auto existing = world(previous);
                const float dx = existing[0] - position[0];
                const float dy = existing[1] - position[1];
                const float dz = existing[2] - position[2];
                if (dx*dx + dy*dy + dz*dz < spacing*spacing) { accepted = false; break; }
            }
            if (accepted) expected.push_back(candidate);
        }
        generation.MinimumSpacing = spacing;
        const auto actual = GrassSurfaceExtractor::Extract(surface, rule, generation, mask, 2500);
        if (actual.empty() || actual.size() != expected.size()) throw std::runtime_error("spacing count mismatch");
        for (size_t i = 0; i < actual.size(); ++i)
            if (!Same(actual[i], expected[i])) throw std::runtime_error("spacing changed an accepted anchor");
        verified += actual.size();
    }
    std::cout << "Exact brute-force spacing reference: " << verified << " anchors, 36 cases\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
