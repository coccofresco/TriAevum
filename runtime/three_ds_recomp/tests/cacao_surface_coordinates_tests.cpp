#include "fast/oot3d/pica_surface_coordinates.h"
#include "fast/oot3d/cacao_normal_input.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Fast::Oot3d;
using V3 = std::array<float, 3>;

static void near(float a, float b) {
    if (std::abs(a - b) > 0.002F) throw std::runtime_error("coordinate/normal mismatch");
}
static V3 sub(V3 a, V3 b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
static V3 normalized(V3 a) {
    float length = std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
    return {a[0]/length,a[1]/length,a[2]/length};
}
static V3 cross(V3 a,V3 b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }

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
    std::array<float,16> projection{};
    projection[0]=1.7F; projection[5]=2.9F; projection[11]=-1.0F;
    for (bool flip : {false,true}) {
        const PicaSurfaceCoordinates coords{flip};
        const auto p=coords.CacaoProjection(projection);
        const auto m=coords.CacaoNormalTransform(projection[11]);
        for (V3 n : {V3{0,0,1}, V3{0.2F,0.3F,1},V3{-0.4F,0.15F,1}}) {
            n=normalized(n);
            // Intersect camera rays with an oblique plane, then reconstruct
            // CACAO's +depth positions exactly as NDCToViewSpace does.
            const auto point=[&](float u,float v) {
                auto ndc=coords.StorageToViewNdc({u,v});
                float depth=10*n[2]/(n[2]-n[0]*ndc[0]/projection[0]-n[1]*ndc[1]/projection[5]);
                return V3{(2*u-1)*depth/p[0],(1-2*v)*depth/p[5],depth};
            };
            auto center=point(.37F,.62F);
            auto fromDepth=normalized(cross(sub(point(.369F,.62F),center),sub(point(.37F,.619F),center)));
            V3 fromGuide{};
            // HLSL column-major buffer + mul(normal, matrix).
            for (int j=0;j<3;++j) for (int i=0;i<3;++i) fromGuide[j]+=n[i]*m[j*4+i];
            for (int i=0;i<3;++i) near(fromGuide[i],fromDepth[i]);
        }
    }
    near(CacaoViewSpaceNormalTransform(projection)[10],-1);
    projection[11]=1;
    near(CacaoViewSpaceNormalTransform(projection)[10],1);
    std::cout << "128 transfer combinations, 2048 UV/SSSR clip roundtrips, 6 depth/normal planes passed\n";
}
