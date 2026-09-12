#pragma once

#include <cstdlib>
#include <string>

namespace Fast::Oot3d {

// Framebuffer-only diagnostics, opt-in at launch; no settings or scene edits.
inline int PicaGuideDiagnosticMode() {
    const char* option = std::getenv("TRIAEVUM_GUIDE_DIAGNOSTIC_VIEW");
    const int mode = option ? std::atoi(option) : 0;
    return mode >= 1 && mode <= 7 ? mode : 0;
}
inline std::string PicaGuideDiagnosticShaderLibrary(int mode = PicaGuideDiagnosticMode()) {
    return "const int oot3d_guide_diagnostic_mode = " + std::to_string(mode) + ";\n" + R"glsl(
vec3 oot3d_guide_view_position(vec2 uv) {
    float depth = texture(scene_depth,uv).r;
    float n=scanout.near_plane, f=scanout.far_plane;
    float z=n*f/max(f-depth*(f-n),0.000001);
    vec2 ndc=oot3d_surface_view_ndc(uv,1u | (scanout.flip_y*2u));
    return vec3((ndc+vec2(scanout.projection_offset_x,scanout.projection_offset_y))*z /
                vec2(scanout.projection_scale_x,scanout.projection_scale_y),-z);
}
vec3 oot3d_guide_diagnostic(vec2 uv) {
    if(oot3d_guide_diagnostic_mode>=4) {
        vec4 guide=oot3d_outline_sample_geometry(uv);
        bool valid=dot(guide.xyz,guide.xyz)>=0.000001;
        if(oot3d_guide_diagnostic_mode==4) return valid ? guide.xyz*0.5+0.5 : vec3(0.0);
        if(oot3d_guide_diagnostic_mode==5) return vec3(oot3d_toon_outline_edge(
            uv,scanout.inverse_size,scanout.outline_width,scanout.outline_depth_sensitivity,
            scanout.outline_normal_sensitivity,scanout.outline_softness));
        if(oot3d_guide_diagnostic_mode==7) return vec3(oot3d_toon_outline_native_edge(
            uv,scanout.inverse_size,scanout.outline_width,scanout.outline_depth_sensitivity,
            scanout.outline_normal_sensitivity,scanout.outline_softness));
        return valid ? vec3(float(oot3d_outline_sample_scene_depth(uv)<guide.a-0.00001),
            1.0, float(oot3d_outline_sample_occlusion(uv)>0.0)) : vec3(0.0);
    }
    float depth=texture(scene_depth,uv).r;
    if(depth>=0.999999) return vec3(0.0);
    vec3 p=oot3d_guide_view_position(uv);
    if(oot3d_guide_diagnostic_mode==1) return vec3(-p.z/(-p.z+scanout.near_plane*10.0));
    vec3 normal=normalize(texture(normal_guide,uv).rgb*2.0-1.0);
    if(oot3d_guide_diagnostic_mode==2) return normal*0.5+0.5;
    vec3 du=oot3d_guide_view_position(uv+vec2(scanout.inverse_size.x,0.0))-p;
    vec3 dv=oot3d_guide_view_position(uv+vec2(0.0,scanout.inverse_size.y))-p;
    vec3 depth_normal=normalize(scanout.flip_y!=0u ? cross(du,dv) : cross(dv,du));
    float agreement=clamp(dot(normal,depth_normal),-1.0,1.0);
    return vec3(max(-agreement,0.0),max(agreement,0.0),0.0);
}
)glsl";
}

} // namespace Fast::Oot3d
