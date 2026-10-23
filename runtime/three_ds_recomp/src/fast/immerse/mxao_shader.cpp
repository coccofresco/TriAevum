#include "mxao_shader.h"

// iMMERSE MXAO, ported from MartysMods_MXAO.fx of
// https://github.com/martymcmodding/iMMERSE at revision
// f57d3afa1ebe5d1fd6152d4f6fb9a2e75bd1d1cb.
//
// Copyright (c) Pascal Gilcher. All rights reserved.
// iMMERSE module of TriAevum (see LICENSE.md in this directory). Not covered
// by TriAevum's GPL: this file stays under the original iMMERSE licence
// (LICENSE-iMMERSE.txt) and is used under the author's explicit permission,
// granted exclusively to TriAevum.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// Changes from the ReShade original: native FP32 depth and the renderer's
// exact projection replace ReShade's linearized depth and assumed 60-degree
// field of view; native view-space normals replace normals reconstructed from
// depth, with the RGB8 decoding bound of the GTAO kernels; fade uses native
// distances; the compute path runs one texel per thread and taps use the
// position of the tile texel they read. Each slice's occlusion is measured
// against its own unoccluded integral (the original's ground-truth mode
// normalized by the slice count and darkened empty oblique planes), and
// occlusion rather than visibility is stored so an unoccluded pixel stays
// exactly zero through half-precision storage and the filter. An optional
// temporal mode rotates the jitter phase every frame and accumulates the
// occlusion with the renderer's motion vectors before the filter.
// The original's own normals from depth remain selectable, as do its shading
// rate (the evenly distributed pattern of its pixel-shader path), radius
// growing with distance and exponential fade. A debug program shows the
// inputs and terms of the estimate.

namespace Fast::Immerse {

std::array<std::string, 6> BuildMxaoPassShaders(std::string_view debugLibrary, std::string_view geometryLibrary) {
    return {BuildMxaoDeinterleaveShader(geometryLibrary), BuildMxaoOcclusionShader(geometryLibrary),
            BuildMxaoFilterShader(false, geometryLibrary), BuildMxaoFilterShader(true, geometryLibrary),
            BuildMxaoTemporalShader(geometryLibrary), BuildMxaoDebugShader(debugLibrary, geometryLibrary)};
}
namespace {
constexpr const char* kCommon = R"glsl(#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(set=0,binding=0) uniform texture2D native_depth;
layout(set=0,binding=1) uniform texture2D view_normals;
layout(set=0,binding=2) uniform texture2D intermediate;
layout(set=0,binding=4) uniform sampler point_sampler;
layout(set=0,binding=5) uniform texture2D motion_vectors;
layout(set=0,binding=6) uniform texture2D history;
// The renderer's geometry guide (unsigned integer, fetched without a sampler).
layout(set=0,binding=7) uniform utexture2D geometry_guide;
layout(push_constant) uniform MxaoState {
    uvec2 extent;
    uint method;
    uint flags;
    vec4 view_x;
    vec4 view_y;
    vec4 depth;
    vec4 radius_fade;
    vec4 response;
    vec4 temporal;
    vec4 previous_depth;
} pc;
const int tiles=4;
const float pi=3.14159265358979323846;
// Sky and invalid depth: far enough to never occlude, finite for the math.
const float sky_z=1.0e8;
float view_depth(ivec2 pixel) {
    pixel=clamp(pixel,ivec2(0),ivec2(pc.extent)-1);
    float d=texelFetch(sampler2D(native_depth,point_sampler),pixel,0).r;
    if(d>=1.0 || d<0.0 || isnan(d)) return sky_z;
    return pc.depth.w!=0.0 ? pc.depth.x+d*pc.depth.z :
        pc.depth.x/max(pc.depth.y-d*pc.depth.z,1.0e-12);
}
ivec2 tile_size() {
    return (ivec2(pc.extent)+tiles-1)/tiles;
}
ivec2 deinterleave(ivec2 pixel) {
    return (pixel%tiles)*tile_size()+pixel/tiles;
}
ivec2 reinterleave(ivec2 tiled) {
    ivec2 size=tile_size();
    return (tiled%size)*tiles+tiled/size;
}
vec2 pixel_uv(ivec2 pixel) {
    return (vec2(pixel)+0.5)/vec2(pc.extent);
}
// View space mirrored along Z: MXAO's projection space with positive depth.
vec3 proj_position(ivec2 pixel,float z) {
    vec3 uv=vec3(pixel_uv(pixel),1.0);
    return vec3(dot(pc.view_x.xyz,uv)*z,dot(pc.view_y.xyz,uv)*z,z);
}
vec3 unit_normal(vec3 encoded) {
    vec3 n=encoded*2.0-1.0;
    return n*inversesqrt(max(dot(n,n),1.0e-12));
}
// Neighbour depths (left, right, top, bottom) and the original's inverse
// weights of the two depth steps around each diagonal; the edge weight marks
// silhouettes and grazing surfaces, which the filter smooths more. Steps are
// in MXAO's normalization (per mille of the far plane).
vec4 neighbour_depths(ivec2 pixel) {
    return vec4(view_depth(pixel-ivec2(1,0)),view_depth(pixel+ivec2(1,0)),
                view_depth(pixel-ivec2(0,1)),view_depth(pixel+ivec2(0,1)));
}
vec4 neighbour_weights(vec4 neighbours,float z) {
    vec4 steps=abs(neighbours-z)*(1000.0*pc.response.y);
    vec4 w=steps.xzyw+steps.zywx;
    return 1.0/(0.001+w*w);
}
// The original's normal from depth: the four cross products of neighbour
// deltas, each weighted against the depth steps it spans.
vec3 depth_normal(ivec2 pixel,vec3 center,vec4 neighbours,vec4 w) {
    vec3 dl=proj_position(pixel-ivec2(1,0),neighbours.x)-center;
    vec3 dr=proj_position(pixel+ivec2(1,0),neighbours.y)-center;
    vec3 dt=proj_position(pixel-ivec2(0,1),neighbours.z)-center;
    vec3 db=proj_position(pixel+ivec2(0,1),neighbours.w)-center;
    // At the image border the clamped neighbour repeats this depth: the
    // opposite side, mirrored, continues the surface instead.
    ivec2 last=ivec2(pc.extent)-1;
    if(pixel.x==0) dl=-dr;
    if(pixel.x==last.x) dr=-dl;
    if(pixel.y==0) dt=-db;
    if(pixel.y==last.y) db=-dt;
    vec3 n0=cross(dt,dl);
    vec3 n1=cross(dr,dt);
    vec3 n2=cross(db,dr);
    vec3 n3=cross(dl,db);
    vec4 weight=w*inversesqrt(max(vec4(dot(n0,n0),dot(n1,n1),dot(n2,n2),dot(n3,n3)),1.0e-30));
    vec3 n=n0*weight.x+n1*weight.y+n2*weight.z+n3*weight.w;
    return n*inversesqrt(dot(n,n)+1.0e-8);
}
// The renderer's normal in MXAO's projection space (view space mirrored along Z).
vec3 renderer_normal(vec4 guide) {
    vec3 n=unit_normal(guide.xyz);
    n.z=-n.z;
    return n;
}
float fade_factor(float z) {
    if((pc.flags&65536u)!=0u) {
        // The original: zero at the far plane, exponential beyond the distance.
        float d=z*pc.response.y;
        float fade=clamp(1.0-d*d,0.0,1.0);
        d/=max(pc.radius_fade.z,1.0e-4);
        return fade*clamp(exp2(-d*d),0.0,1.0);
    }
    return clamp((pc.radius_fade.w-z)/max(pc.radius_fade.w-pc.radius_fade.z,1.0e-5),0.0,1.0);
}
)glsl";

// The face normal of the geometry guide in MXAO's projection space (view
// space mirrored along Z); zero where the guide has none.
constexpr const char* kGeometryNormal = R"glsl(
vec3 geometry_normal(ivec2 pixel) {
    vec3 n=pica_geometry_decode_normal(texelFetch(geometry_guide,pixel,0).g);
    n.z=-n.z;
    return n;
}
)glsl";

std::string Common(std::string_view geometryLibrary) {
    std::string source = kCommon;
    source += geometryLibrary;
    source += kGeometryNormal;
    return source;
}
} // namespace

std::string BuildMxaoDeinterleaveShader(std::string_view geometryLibrary) {
    std::string source = Common(geometryLibrary);
    source += R"glsl(
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=3,r32f) uniform writeonly image2D tiled_depth;
void main() {
    ivec2 tiled=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(tiled,tile_size()*tiles))) return;
    ivec2 pixel=reinterleave(tiled);
    float z=all(lessThan(pixel,ivec2(pc.extent))) ? view_depth(pixel) : sky_z;
    imageStore(tiled_depth,tiled,vec4(z));
}
)glsl";
    return source;
}

std::string BuildMxaoOcclusionShader(std::string_view geometryLibrary) {
    std::string source = Common(geometryLibrary);
    source += R"glsl(
layout(local_size_x=16,local_size_y=16,local_size_z=1) in;
layout(set=0,binding=3,rg16f) uniform writeonly image2D raw_occlusion;
//  slices/steps    preset      samples
const uvec2 presets[7]=uvec2[7](
    uvec2(2,2),     // Low        8
    uvec2(2,4),     // Medium     16
    uvec2(2,10),    // High       40
    uvec2(3,12),    // Very High  72
    uvec2(4,14),    // Ultra      112
    uvec2(6,16),    // Extreme    192
    uvec2(8,20));   // IDGAF      320
float fast_acos(float x) {
    float o=sqrt(max(abs(x)*(abs(x)*0.5405464-3.0079475)+2.4674011,0.0));
    return x<0.0 ? pi-o : o;
}
vec2 fast_acos(vec2 x) {
    return vec2(fast_acos(x.x),fast_acos(x.y));
}
void main() {
    ivec2 tiled=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(tiled,tile_size()*tiles))) return;
    ivec2 pixel=reinterleave(tiled);
    if(any(greaterThanEqual(pixel,ivec2(pc.extent)))) return;
    // Shading rate: this frame estimates one tile parity class, the others
    // keep their previous estimate.
    uint shading_rate=(pc.flags>>12u)&3u;
    if(shading_rate!=0u) {
        uvec2 tile=uvec2(pixel%tiles);
        uint shading_phase=(pc.flags>>14u)&3u;
        if(shading_rate==1u ? ((tile.x+tile.y)&1u)!=(shading_phase&1u)
                            : (tile.x&1u)+(tile.y&1u)*2u!=shading_phase) return;
    }
    float z=texelFetch(sampler2D(intermediate,point_sampler),tiled,0).r;
    float depth=z>=sky_z ? 1.0 : clamp(z*pc.response.y,0.0,1.0);
    vec4 guide=texelFetch(sampler2D(view_normals,point_sampler),pixel,0);
    float fade=fade_factor(z);
    if(z>=sky_z || z<=0.0 || guide.a<=0.0 || fade<0.001 || pc.response.x<=0.0) {
        imageStore(raw_occlusion,pixel,vec4(0.0,depth,0.0,0.0));
        return;
    }

    vec4 neighbours=neighbour_depths(pixel);
    vec4 w=neighbour_weights(neighbours,z);
    float edge_weight=clamp(1.0-dot(w,vec4(1.0)),0.0,1.0);

    vec3 p=proj_position(pixel,z);
    bool from_depth=(pc.flags&64u)!=0u;
    vec3 n=from_depth ? depth_normal(pixel,p,neighbours,w) : renderer_normal(guide);
    if((pc.flags&131072u)!=0u) {
        vec3 face=geometry_normal(pixel);
        n=dot(face,face)>0.0 ? face : depth_normal(pixel,p,neighbours,w);
    }
    p*=0.996;
    vec3 v=normalize(-p);
    if(dot(n,v)<0.0) n=-n;

    uint preset_index=min(pc.flags&7u,5u);
    uint slice_count=presets[preset_index].x;
    uint sample_count=presets[preset_index].y;

    uint jitter_index=uint(pixel.x%tiles)+uint(tiles)*uint(pixel.y%tiles);
    jitter_index=jitter_index*11u+((pc.flags>>8u)&15u);
    float jitter=(float(jitter_index%uint(tiles*tiles))+0.5)/float(tiles*tiles);

    vec2 slice_dir=vec2(sin(jitter*pi*(6.0/float(slice_count))),cos(jitter*pi*(6.0/float(slice_count))));
    float rotation_sin=sin(pi/float(slice_count));
    float rotation_cos=cos(pi/float(slice_count));

    // Screen offset of a lateral view-space displacement at this depth.
    mat2 projection=mat2(pc.view_x.x,pc.view_y.x,pc.view_x.y,pc.view_y.y);
    mat2 unprojection=inverse(projection);

    // Increase Radius with Distance: a constant screen radius; its world
    // radius at this depth drives the falloff and the bitmask thickness.
    bool screen_radius=(pc.flags&128u)!=0u;
    float radius=screen_radius ? max(pc.response.w*p.z*length(vec2(pc.view_x.x,pc.view_y.x)),1.0e-4)
                               : max(pc.radius_fade.x,1.0e-4);
    float thickness=screen_radius ? log(1.0+radius)/3.0 : pc.radius_fade.y;
    float falloff_factor=1.0/(radius*radius);
    // Taps within the normal's angular uncertainty of its tangent plane are
    // not occluders: the guide's decoding bound, which also covers the
    // rounding of depth normals reconstructed from neighbouring samples.
    float normal_error=pc.response.z;
    vec2 vcrossn_xy=v.yz*n.zx-v.zx*n.yz;
    float ndotv=dot(n,v);
    // Occlusion is accumulated against each slice's unoccluded integral, so
    // an empty hemisphere is exactly zero whatever the slice quadrature.
    float occlusion_sum=0.0;
    float slicesum=0.0;

    for(uint slice=0u;slice<slice_count;++slice) {
        slice_dir=vec2(slice_dir.x*rotation_cos+slice_dir.y*rotation_sin,
                       -slice_dir.x*rotation_sin+slice_dir.y*rotation_cos);
        // The screen radius is a circle of response.w widths, as the original's
        // aspect-corrected offset, along this slice's screen direction.
        vec2 radius_pixels=screen_radius
            ? normalize((unprojection*slice_dir)*vec2(pc.extent))*(pc.response.w*float(pc.extent.x))
            : (unprojection*(slice_dir*(radius/p.z)))*vec2(pc.extent);

        float sdotv=dot(slice_dir,v.xy);
        float sdotn=dot(slice_dir,n.xy);
        float ndotns=dot(slice_dir,vcrossn_xy)*inversesqrt(max(1.0-sdotv*sdotv,1.0e-12));
        float sliceweight=sqrt(clamp(1.0-ndotns*ndotns,0.0,1.0));
        float cosn=sliceweight>0.0 ? clamp(ndotv/sliceweight,0.0,1.0) : 1.0;
        float normal_angle=fast_acos(cosn);
        normal_angle=sdotn<sdotv*ndotv ? -normal_angle : normal_angle;

        vec2 unoccludedhorizoncos=vec2(sin(normal_angle),-sin(normal_angle));
        vec2 maxhorizoncos=unoccludedhorizoncos;
        uint occlusion_bitfield=0xFFFFFFFFu;
        vec2 direction=radius_pixels;
        for(int side=0;side<2;++side) {
            maxhorizoncos=maxhorizoncos.yx;
            float lowesthorizoncos=maxhorizoncos.x;
            for(uint s_index=0u;s_index<sample_count;s_index+=2u) {
                vec2 s=(float(s_index)+vec2(0.0,1.0)+jitter)/float(sample_count);
                s*=s;
                // Taps stay on this pixel's tile lattice, like the original's
                // point-sampled deinterleaved texture.
                ivec2 taps[2]=ivec2[2](pixel+ivec2(round(s.x*direction/float(tiles)))*tiles,
                                       pixel+ivec2(round(s.y*direction/float(tiles)))*tiles);
                if(any(lessThan(taps[1],ivec2(0))) || any(greaterThanEqual(taps[1],ivec2(pc.extent)))) break;
                for(int pair=0;pair<2;++pair) {
                    ivec2 tap=taps[pair];
                    float tap_z=texelFetch(sampler2D(intermediate,point_sampler),deinterleave(tap),0).r;
                    vec3 deltavec=proj_position(tap,tap_z)-p;
                    float ddotd=dot(deltavec,deltavec);
                    // Native RGB8 normals have finite angular precision: a tap
                    // inside that uncertainty of the tangent plane is not an
                    // occluder (the same bound as the GTAO kernels).
                    float height=dot(deltavec,n);
                    if(height<=max(z*1.0e-5,1.0e-5) || height*height<=ddotd*normal_error) continue;
                    if(pc.method<2u) {
                        float samplehorizoncos=dot(deltavec,v)*inversesqrt(ddotd);
                        float falloff=1.0/(1.0+ddotd*falloff_factor);
                        samplehorizoncos=mix(lowesthorizoncos,samplehorizoncos,falloff);
                        maxhorizoncos.x=max(maxhorizoncos.x,samplehorizoncos);
                    } else {
                        float ddotv=dot(deltavec,v);
                        vec2 h_frontback=vec2(ddotv,ddotv-thickness)*
                            inversesqrt(max(vec2(ddotd,ddotd-2.0*thickness*ddotv+thickness*thickness),1.0e-20));
                        h_frontback=fast_acos(clamp(h_frontback,-1.0,1.0));
                        h_frontback=side==1 ? h_frontback : -h_frontback.yx;
                        h_frontback=clamp((h_frontback+normal_angle)/pi+0.5,0.0,1.0);
                        // Approximates inverse transform sampling of the cosine lobe.
                        if(pc.method==2u) h_frontback=h_frontback*h_frontback*(3.0-2.0*h_frontback);
                        uint a=min(uint(h_frontback.x*32.0),32u);
                        uint b=min(uint(h_frontback.y*32.0),32u)-a;
                        uint occlusion=a>=32u ? 0u : (b>=32u ? 0xFFFFFFFFu : ((1u<<b)-1u))<<a;
                        occlusion_bitfield&=~occlusion;
                    }
                }
            }
            direction=-direction;
        }
        if(pc.method==0u) {
            // The original normalizes by the slice count; a two-slice
            // quadrature then darkened empty oblique planes by up to 16%.
            vec2 max_horizon_angle=fast_acos(clamp(maxhorizoncos,-1.0,1.0));
            vec2 open_angle=fast_acos(clamp(unoccludedhorizoncos,-1.0,1.0));
            vec2 h=vec2(-max_horizon_angle.x,max_horizon_angle.y);
            vec2 open=vec2(-open_angle.x,open_angle.y);
            vec2 arc=cosn+2.0*h*sin(normal_angle)-cos(2.0*h-normal_angle);
            vec2 open_arc=cosn+2.0*open*sin(normal_angle)-cos(2.0*open-normal_angle);
            occlusion_sum+=max((open_arc.x+open_arc.y)-(arc.x+arc.y),0.0)*sliceweight;
            slicesum+=(open_arc.x+open_arc.y)*sliceweight;
        } else if(pc.method==1u) {
            vec2 max_horizon_angle=fast_acos(clamp(maxhorizoncos,-1.0,1.0));
            vec2 open_angle=fast_acos(clamp(unoccludedhorizoncos,-1.0,1.0));
            occlusion_sum+=max((open_angle.x+open_angle.y)-(max_horizon_angle.x+max_horizon_angle.y),0.0)*
                           sliceweight;
            slicesum+=(open_angle.x+open_angle.y)*sliceweight;
        } else {
            occlusion_sum+=float(32u-min(uint(bitCount(occlusion_bitfield)),32u))/32.0*sliceweight;
            slicesum+=sliceweight;
        }
    }
    float ao=1.0-clamp(occlusion_sum/max(slicesum,1.0e-6),0.0,1.0);

    float amount=pc.response.x;
    ao=mix(1.0,ao,clamp(amount,0.0,1.0));
    // Above 1 the original switches to a gamma response.
    if(amount>1.0) ao=mix(ao,ao*ao,clamp(amount-1.0,0.0,1.0));
    // Stored as occlusion: an unoccluded pixel stays exactly zero through
    // half-precision storage and the (affine) regression filter.
    float occlusion=clamp((1.0-ao)*fade,0.0,1.0);
    imageStore(raw_occlusion,pixel,vec4(occlusion,edge_weight>0.5 ? -depth : depth,0.0,0.0));
}
)glsl";
    return source;
}

std::string BuildMxaoFilterShader(bool finalPass, std::string_view geometryLibrary) {
    std::string source = Common(geometryLibrary);
    source += finalPass ? "layout(set=0,binding=3,r32f) uniform writeonly image2D filtered;\n"
                        : "layout(set=0,binding=3,rg16f) uniform writeonly image2D filtered;\n";
    source += R"glsl(
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
vec2 occlusion(ivec2 pixel) {
    pixel=clamp(pixel,ivec2(0),ivec2(pc.extent)-1);
    return texelFetch(sampler2D(intermediate,point_sampler),pixel,0).rg;
}
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(pc.extent)))) return;
    vec2 center=occlusion(pixel);
    vec2 result=center;
    if((pc.flags&32u)==0u) {
        // Linear regression of occlusion on depth over a 4x4 block; the
        // mirrored second iteration makes the two-pass footprint symmetric.
        ivec2 origin=pixel-((pc.flags&16u)!=0u ? ivec2(2) : ivec2(1));
        vec4 moments=vec4(0.0);
        for(int y=0;y<4;++y) {
            for(int x=0;x<4;++x) {
                vec2 texel=occlusion(origin+ivec2(x,y));
                float d=abs(texel.y);
                moments+=vec4(d,d*d,texel.x,texel.x*d);
            }
        }
        moments/=16.0;
        bool blurry=center.y<0.0;
        float b=(moments.w-moments.x*moments.z)/max(moments.y-moments.x*moments.x,exp2(blurry ? -12.0 : -30.0));
        float a=moments.z-b*moments.x;
        result=vec2(clamp(b*abs(center.y)+a,0.0,1.0),center.y);
    }
)glsl";
    source += finalPass ? "    imageStore(filtered,pixel,vec4(1.0-result.x));\n}\n"
                        : "    imageStore(filtered,pixel,vec4(result,0.0,0.0));\n}\n";
    return source;
}

std::string BuildMxaoDebugShader(std::string_view packNormal, std::string_view geometryLibrary) {
    std::string source = Common(geometryLibrary);
    source += "layout(set=0,binding=3,r32f) uniform writeonly image2D debug_view;\n";
    source.append(packNormal);
    source += R"glsl(
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(pc.extent)))) return;
    uint view=(pc.flags>>20u)&15u;
    float z=view_depth(pixel);
    vec4 guide=texelFetch(sampler2D(view_normals,point_sampler),pixel,0);
    float value=0.0;
    if(view==8u) {
        // Accumulated frames over the maximum history length.
        vec4 accumulated=texelFetch(sampler2D(intermediate,point_sampler),pixel,0);
        value=clamp(accumulated.b/max(pc.temporal.y,1.0),0.0,1.0);
    } else if(view==5u) {
        // Depth over the fade range (the original fade: over the far plane).
        value=z>=sky_z ? 1.0 : clamp((pc.flags&65536u)!=0u ? z*pc.response.y : z/max(pc.radius_fade.w,1.0e-5),
                                     0.0,1.0);
    } else if(view==7u) {
        value=z>=sky_z ? 0.0 : fade_factor(z);
    } else if(z<sky_z && z>0.0) {
        vec3 p=proj_position(pixel,z);
        vec4 neighbours=neighbour_depths(pixel);
        vec4 w=neighbour_weights(neighbours,z);
        vec3 v=normalize(-p);
        vec3 from_depth=depth_normal(pixel,p,neighbours,w);
        vec3 from_renderer=renderer_normal(guide);
        vec3 from_geometry=geometry_normal(pixel);
        bool has_geometry=dot(from_geometry,from_geometry)>0.0;
        if(dot(from_depth,v)<0.0) from_depth=-from_depth;
        if(dot(from_renderer,v)<0.0) from_renderer=-from_renderer;
        if(dot(from_geometry,v)<0.0) from_geometry=-from_geometry;
        vec3 used=(pc.flags&131072u)!=0u ? (has_geometry ? from_geometry : from_depth)
                 : (pc.flags&64u)!=0u ? from_depth : from_renderer;
        // Displayed in view space (Z toward the viewer).
        vec3 shown=view==1u ? used : view==2u ? from_renderer : view==9u ? from_geometry : from_depth;
        if(view<=3u || view==9u)
            value=mxao_debug_pack_normal(vec3(shown.xy,-shown.z));
        else if(view==4u)
            value=clamp(acos(clamp(dot(from_depth,from_renderer),-1.0,1.0))/(0.5*pi),0.0,1.0);
        else if(view==6u)
            value=clamp(1.0-dot(w,vec4(1.0)),0.0,1.0);
        else if(view==10u) {
            // Grey: the angle over 10 degrees (atan2 of the cross and dot
            // products keeps small angles exact); magenta: no face normal.
            float angle=clamp(atan(length(cross(from_geometry,from_depth)),dot(from_geometry,from_depth))/
                              radians(10.0),0.0,1.0);
            value=mxao_debug_pack_normal(has_geometry ? vec3(angle*2.0-1.0) : vec3(1.0,-1.0,1.0));
        }
    }
    imageStore(debug_view,pixel,vec4(value));
}
)glsl";
    return source;
}

std::string BuildMxaoTemporalShader(std::string_view geometryLibrary) {
    std::string source = Common(geometryLibrary);
    source += R"glsl(
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=3,rgba16f) uniform writeonly image2D accumulated;
// The group's raw occlusion and depth, edge-clamped, with the border its
// pixels read: one texel for the depth slope, the 4x4 window (-1..+2) for the
// clamp. Each texel is fetched once instead of up to twenty times.
shared vec2 tile[11][11];
vec2 raw_occlusion(ivec2 local) {
    return tile[local.y+1][local.x+1];
}
void main() {
    ivec2 origin=ivec2(gl_WorkGroupID.xy)*8;
    for(uint i=gl_LocalInvocationIndex;i<121u;i+=64u) {
        ivec2 t=ivec2(int(i%11u),int(i/11u));
        ivec2 source=clamp(origin+t-1,ivec2(0),ivec2(pc.extent)-1);
        tile[t.y][t.x]=texelFetch(sampler2D(intermediate,point_sampler),source,0).rg;
    }
    memoryBarrierShared();
    barrier();
    ivec2 local=ivec2(gl_LocalInvocationID.xy);
    ivec2 pixel=origin+local;
    if(any(greaterThanEqual(pixel,ivec2(pc.extent)))) return;
    vec2 current=raw_occlusion(local);
    float depth=abs(current.y);
    vec4 result=vec4(current,1.0,0.0);
    if(pc.temporal.x>0.5 && depth<1.0) {
        vec2 uv=pixel_uv(pixel);
        // The motion vectors keep the scene target's extent.
        ivec2 motion_extent=textureSize(sampler2D(motion_vectors,point_sampler),0);
        vec4 motion=texelFetch(sampler2D(motion_vectors,point_sampler),
            min(ivec2(uv*vec2(motion_extent)),motion_extent-1),0);
        vec2 history_uv=uv+motion.xy;
        if(motion.b<0.5 && all(greaterThanEqual(history_uv,vec2(0.0))) &&
           all(lessThanEqual(history_uv,vec2(1.0)))) {
            // This surface point's depth in the previous view. The tolerance
            // follows the surface's own slope (the smaller one-sided step, so
            // a silhouette does not widen it) over the bilinear footprint.
            float expected=dot(pc.previous_depth,vec4(proj_position(pixel,depth/pc.response.y),1.0));
            float slope_x=min(abs(abs(raw_occlusion(local+ivec2(1,0)).y)-depth),
                              abs(depth-abs(raw_occlusion(local-ivec2(1,0)).y)));
            float slope_y=min(abs(abs(raw_occlusion(local+ivec2(0,1)).y)-depth),
                              abs(depth-abs(raw_occlusion(local-ivec2(0,1)).y)));
            float tolerance=pc.temporal.z*expected+2.0*max(slope_x,slope_y);
            // The history texel under the reprojected position: resampling it
            // bilinearly every frame would blur fine occlusion while the view
            // moves.
            ivec2 tap=clamp(ivec2(floor(history_uv*vec2(pc.extent))),ivec2(0),ivec2(pc.extent)-1);
            vec4 h=texelFetch(sampler2D(history,point_sampler),tap,0);
            if(h.b>0.0 && abs(abs(h.g)-expected)<=tolerance) {
                // Every 4x4 window holds all sixteen jitter phases of this
                // frame: the converged occlusion lies within their range.
                float low=current.x, high=current.x;
                for(int y=-1;y<3;++y) {
                    for(int x=-1;x<3;++x) {
                        float o=raw_occlusion(local+ivec2(x,y)).x;
                        low=min(low,o);
                        high=max(high,o);
                    }
                }
                float history_occlusion=clamp(h.r,low,high);
                // Moving content keeps a shorter history, down to the
                // minimum at a few pixels per frame: it lags its motion less.
                float speed=length(motion.xy*vec2(pc.extent));
                float history_length=min(h.b+1.0,max(pc.temporal.w,pc.temporal.y/(1.0+speed)));
                result=vec4(mix(history_occlusion,current.x,1.0/history_length),current.y,history_length,0.0);
            }
        }
    }
    imageStore(accumulated,pixel,result);
}
)glsl";
    return source;
}

} // namespace Fast::Immerse
