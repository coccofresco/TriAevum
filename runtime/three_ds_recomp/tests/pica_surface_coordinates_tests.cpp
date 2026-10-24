#include "fast/extension/pica_surface_coordinates.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Fast::Extension;

static void near(float a, float b) {
    if (std::abs(a - b) > 0.002F) throw std::runtime_error("coordinate/normal mismatch");
}

int main() {
    for (uint32_t flags=0; flags<128; ++flags) {
        const auto coords=PicaSurfaceCoordinates::FromTransferFlags(flags);
        for (float x : {0.0F,0.17F,0.5F,1.0F}) for (float y : {0.0F,0.33F,0.5F,1.0F}) {
            auto ndc=coords.StorageToViewNdc(coords.PresentationToStorage({x,y}));
            near(ndc[0],2*x-1); near(ndc[1],1-2*y);
            const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            const auto clip=coords.FidelityFxClip(identity);
            auto uv=coords.PresentationToStorage({x,y});
            near((clip[0]*ndc[0]+clip[4]*ndc[1])*.5F+.5F,uv[0]);
            near(.5F-(clip[1]*ndc[0]+clip[5]*ndc[1])*.5F,uv[1]);
            near(clip[10],1); near(clip[15],1);
        }
    }
    std::cout << "128 transfer combinations and 2048 UV/SSSR clip roundtrips passed\n";
}
