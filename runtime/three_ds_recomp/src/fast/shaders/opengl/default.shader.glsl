@prism(type='fragment', name='Fast3D Fragment Shader', version='1.0.0', description='Ported shader to prism', author='Emill & Prism Team')

@{GLSL_VERSION}

@if(VERTEX_SHADER)
    @{attr} vec4 aVtxPos;

    @if(o_oot3d_native_transform)
        uniform mat4 uOot3dModelViewProjection;
    @end

    @for(i in 0..2)
        @if(o_textures[i])
            @{attr} vec2 aTexCoord@{i};
            @{out} vec2 vTexCoord@{i};
            @{update_floats(2)}
            @for(j in 0..2)
                @if(o_clamp[i][j])
                    @if(j == 0)
                        @{attr} float aTexClampS@{i};
                        @{out} float vTexClampS@{i};
                    @else
                        @{attr} float aTexClampT@{i};
                        @{out} float vTexClampT@{i};
                    @end
                    @{update_floats(1)}
                @end
            @end
        @end
    @end

    @if(o_fog)
        @{attr} vec4 aFog;
        @{out} vec4 vFog;
        @{update_floats(4)}
    @end

    @if(o_grayscale)
        @{attr} vec4 aGrayscaleColor;
        @{out} vec4 vGrayscaleColor;
        @{update_floats(4)}
    @end

    @for(i in 0..o_inputs)
        @if(o_alpha)
            @{attr} vec4 aInput@{i + 1};
            @{out} vec4 vInput@{i + 1};
            @{update_floats(4)}
        @else
            @{attr} vec3 aInput@{i + 1};
            @{out} vec3 vInput@{i + 1};
            @{update_floats(3)}
        @end
    @end

    @if(o_oot3d_pica_texture2_mult_add)
        @{attr} vec2 aOot3dPicaTexCoord2;
        @{out} vec2 vOot3dPicaTexCoord2;
        @{update_floats(2)}
    @end

    @if(o_oot3d_pica_shadow2d)
        @{attr} vec3 aOot3dShadowTexCoord;
        @{out} vec3 vOot3dShadowTexCoord;
        @{update_floats(3)}
    @end

    void main() {
        @for(i in 0..2)
            @if(o_textures[i])
                vTexCoord@{i} = aTexCoord@{i};
                @for(j in 0..2)
                    @if(o_clamp[i][j])
                        @if(j == 0)
                            vTexClampS@{i} = aTexClampS@{i};
                        @else
                            vTexClampT@{i} = aTexClampT@{i};
                        @end
                    @end
                @end
            @end
        @end
        @if(o_fog)
            vFog = aFog;
        @end
        @if(o_grayscale)
            vGrayscaleColor = aGrayscaleColor;
        @end
        @for(i in 0..o_inputs)
            vInput@{i + 1} = aInput@{i + 1};
        @end
        @if(o_oot3d_pica_texture2_mult_add)
            vOot3dPicaTexCoord2 = aOot3dPicaTexCoord2;
        @end
        @if(o_oot3d_pica_shadow2d)
            vOot3dShadowTexCoord = aOot3dShadowTexCoord;
        @end
        @if(o_oot3d_native_transform)
            gl_Position = uOot3dModelViewProjection * aVtxPos;
        @else
            gl_Position = aVtxPos;
        @end
        @if(opengles)
            gl_Position.z *= 0.3f;
        @end
    }
@else
    @if(core_opengl || opengles)
    out vec4 vOutColor;
    @end

    @for(i in 0..2)
        @if(o_textures[i])
            @{attr} vec2 vTexCoord@{i};
            @for(j in 0..2)
                @if(o_clamp[i][j])
                    @if(j == 0)
                        @{attr} float vTexClampS@{i};
                    @else
                        @{attr} float vTexClampT@{i};
                    @end
                @end
            @end
        @end
    @end

    @if(o_fog) @{attr} vec4 vFog;
    @if(o_grayscale) @{attr} vec4 vGrayscaleColor;

    @for(i in 0..o_inputs)
        @if(o_alpha)
            @{attr} vec4 vInput@{i + 1};
        @else
            @{attr} vec3 vInput@{i + 1};
        @end
    @end

    @if(o_oot3d_pica_texture2_mult_add)
        @{attr} vec2 vOot3dPicaTexCoord2;
        uniform sampler2D uOot3dPicaTex2;
    @end

    @if(o_oot3d_pica_shadow2d)
        @{attr} vec3 vOot3dShadowTexCoord;
    @end

    @if(o_textures[0]) uniform sampler2D uTex0;
    @if(o_textures[1]) uniform sampler2D uTex1;

    @if(o_masks[0]) uniform sampler2D uTexMask0;
    @if(o_masks[1]) uniform sampler2D uTexMask1;

    @if(o_blend[0]) uniform sampler2D uTexBlend0;
    @if(o_blend[1]) uniform sampler2D uTexBlend1;

    uniform int frame_count;
    uniform float noise_scale;

    @if(o_prim_depth)
    uniform float prim_depth;
    @end

    uniform int texture_width[2];
    uniform int texture_height[2];
    uniform int texture_filtering[2];

    @if(o_oot3d_pica_shadow2d)
    uniform usampler2D uOot3dShadow2d;
    uniform int oot3d_shadow2d_texture_bias;
    uniform int oot3d_shadow2d_orthographic;
    uniform int oot3d_shadow2d_invert;
    @end

    @if(o_oot3d_pica_fog)
    uniform uint oot3d_pica_fog_lut[128];
    uniform int oot3d_pica_fog_flip;
    @end

    @if(o_oot3d_pica_alpha_test)
    uniform int oot3d_pica_alpha_test_enabled;
    uniform int oot3d_pica_alpha_test_func;
    uniform int oot3d_pica_alpha_test_ref;
    @end

    #define TEX_OFFSET(off) @{texture}(tex, texCoord - off / texSize)
    #define WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)

    @if(o_pica_texture_env_clamp)
    vec3 oot3dPicaByteRound(vec3 value) {
        return round(value * 255.0) * (1.0 / 255.0);
    }

    vec4 oot3dPicaByteRound(vec4 value) {
        return round(value * 255.0) * (1.0 / 255.0);
    }
    @end

    float random(in vec3 value) {
        float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));
        return fract(sin(random) * 143758.5453);
    }

    vec4 fromLinear(vec4 linearRGB){
        bvec3 cutoff = lessThan(linearRGB.rgb, vec3(0.0031308));
        vec3 higher = vec3(1.055)*pow(linearRGB.rgb, vec3(1.0/2.4)) - vec3(0.055);
        vec3 lower = linearRGB.rgb * vec3(12.92);
        return vec4(mix(higher, lower, cutoff), linearRGB.a);
    }

    vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {
        vec2 offset = fract(texCoord*texSize - vec2(0.5));
        offset -= step(1.0, offset.x + offset.y);
        vec4 c0 = TEX_OFFSET(offset);
        vec4 c1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y));
        vec4 c2 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)));
        return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);
    }

    vec4 hookTexture2D(in int id, sampler2D tex, in vec2 uv, in vec2 texSize) {
    @if(o_three_point_filtering)
        if(texture_filtering[id] == @{FILTER_THREE_POINT}) {
            return filter3point(tex, uv, texSize);
        }
    @end
        return @{texture}(tex, uv);
    }

    #define TEX_SIZE(tex) vec2(texture_width[tex], texture_height[tex])

    @if(o_oot3d_pica_fog)
    float oot3dDecodePicaFogValue(uint word) {
        return float((word >> 13u) & 0x7FFu) * (1.0 / 2048.0);
    }

    float oot3dDecodePicaFogDiff(uint word) {
        uint raw = word & 0x1FFFu;
        uint encoded = raw < 4096u ? raw + 4096u : raw - 4096u;
        return float(encoded) * (1.0 / 2048.0) - 2.0;
    }

    float oot3dSamplePicaFogFactor() {
        float depth = clamp(gl_FragCoord.z, 0.0, 1.0);
        float lutIndex = (oot3d_pica_fog_flip != 0 ? 1.0 - depth : depth) * 128.0;
        float lutFloor = clamp(floor(lutIndex), 0.0, 127.0);
        uint word = oot3d_pica_fog_lut[int(lutFloor)];
        return clamp(oot3dDecodePicaFogValue(word) +
                     oot3dDecodePicaFogDiff(word) * (lutIndex - lutFloor), 0.0, 1.0);
    }
    @end

    @if(o_oot3d_pica_shadow2d)
    float oot3dCompareShadow2d(uint pixel, uint z) {
        uint depth24 = pixel >> 8u;
        uint alpha8 = pixel & 0xFFu;
        return depth24 <= z ? 0.0 : float(alpha8) * (1.0 / 255.0);
    }

    float oot3dSampleShadow2dTap(ivec2 uv, uint z) {
        ivec2 size = textureSize(uOot3dShadow2d, 0);
        if (any(lessThan(uv, ivec2(0))) || any(greaterThanEqual(uv, size))) {
            return 1.0;
        }
        return oot3dCompareShadow2d(texelFetch(uOot3dShadow2d, uv, 0).x, z);
    }

    float oot3dMixShadow2d(vec4 s, vec2 a) {
        vec2 t = mix(s.xy, s.zw, a.yy);
        return mix(t.x, t.y, a.x);
    }

    vec3 oot3dSampleShadow2d(vec2 uv, float w) {
        if (oot3d_shadow2d_orthographic == 0) {
            uv /= w;
        }
        uint z = uint(max(0, int(min(abs(w), 1.0) * 16777215.0) - oot3d_shadow2d_texture_bias));
        ivec2 size = textureSize(uOot3dShadow2d, 0);
        vec2 coord = vec2(size) * uv - vec2(0.5);
        vec2 coordFloor = floor(coord);
        vec2 f = coord - coordFloor;
        ivec2 i = ivec2(coordFloor);
        vec4 taps = vec4(
            oot3dSampleShadow2dTap(i, z),
            oot3dSampleShadow2dTap(i + ivec2(1, 0), z),
            oot3dSampleShadow2dTap(i + ivec2(0, 1), z),
            oot3dSampleShadow2dTap(i + ivec2(1, 1), z));
        float value = oot3dMixShadow2d(taps, f);
        if (oot3d_shadow2d_invert != 0) {
            value = 1.0 - value;
        }
        return vec3(value);
    }
    @end

    void main() {
        @for(i in 0..2)
            @if(o_textures[i])
                @{s = o_clamp[i][0]}
                @{t = o_clamp[i][1]}

                vec2 texSize@{i} = TEX_SIZE(@{i});

                @if(!s && !t)
                    vec2 vTexCoordAdj@{i} = vTexCoord@{i};
                @else
                    @if(s && t)
                        vec2 vTexCoordAdj@{i} = clamp(vTexCoord@{i}, 0.5 / texSize@{i}, vec2(vTexClampS@{i}, vTexClampT@{i}));
                    @elseif(s)
                        vec2 vTexCoordAdj@{i} = vec2(clamp(vTexCoord@{i}.s, 0.5 / texSize@{i}.s, vTexClampS@{i}), vTexCoord@{i}.t);
                    @else
                        vec2 vTexCoordAdj@{i} = vec2(vTexCoord@{i}.s, clamp(vTexCoord@{i}.t, 0.5 / texSize@{i}.t, vTexClampT@{i}));
                    @end
                @end

                vec4 texVal@{i} = hookTexture2D(@{i}, uTex@{i}, vTexCoordAdj@{i}, texSize@{i});

                @if(o_masks[i])
                    @if(opengles) 
                        vec2 maskSize@{i} = vec2(textureSize(uTexMask@{i}, 0));
                    @else 
                        vec2 maskSize@{i} = textureSize(uTexMask@{i}, 0);
                    @end

                    vec4 maskVal@{i} = hookTexture2D(@{i}, uTexMask@{i}, vTexCoordAdj@{i}, maskSize@{i});

                    @if(o_blend[i])
                        vec4 blendVal@{i} = hookTexture2D(@{i}, uTexBlend@{i}, vTexCoordAdj@{i}, texSize@{i});
                    @else
                        vec4 blendVal@{i} = vec4(0, 0, 0, 0);
                    @end

                    texVal@{i} = mix(texVal@{i}, blendVal@{i}, maskVal@{i}.a);
                @end
            @end
        @end

        @if(o_alpha) 
            vec4 texel;
        @else 
            vec3 texel;
        @end

        @if(o_2cyc)
            @{f_range = 2}
        @else
            @{f_range = 1}
        @end

        @if(o_oot3d_pica_shadow2d)
            @if(o_alpha)
                vec4 oot3dShadowedInput1 = vec4(
                    vInput1.rgb * oot3dSampleShadow2d(vOot3dShadowTexCoord.xy, vOot3dShadowTexCoord.z),
                    vInput1.a);
            @else
                vec3 oot3dShadowedInput1 =
                    vInput1 * oot3dSampleShadow2d(vOot3dShadowTexCoord.xy, vOot3dShadowTexCoord.z);
            @end
        @end

        @if(o_pica_texture_env_clamp)
            @for(i in 0..o_inputs)
                @if(o_alpha)
                    @if(i == 0 && o_oot3d_pica_shadow2d)
                        vec4 oot3dPicaRoundedInput@{i + 1} = oot3dPicaByteRound(oot3dShadowedInput1);
                    @else
                        vec4 oot3dPicaRoundedInput@{i + 1} = oot3dPicaByteRound(vInput@{i + 1});
                    @end
                @else
                    @if(i == 0 && o_oot3d_pica_shadow2d)
                        vec3 oot3dPicaRoundedInput@{i + 1} = oot3dPicaByteRound(oot3dShadowedInput1);
                    @else
                        vec3 oot3dPicaRoundedInput@{i + 1} = oot3dPicaByteRound(vInput@{i + 1});
                    @end
                @end
                #define vInput@{i + 1} oot3dPicaRoundedInput@{i + 1}
            @end
        @elseif(o_oot3d_pica_shadow2d)
            #define vInput1 oot3dShadowedInput1
        @end

        @for(c in 0..f_range)
            @if(c == 1)
                @if(o_alpha)
                    @if(o_c[c][1][2] == SHADER_COMBINED)
                        @if(o_pica_texture_env_clamp)
                            texel.a = oot3dPicaByteRound(vec4(clamp(texel.a, 0.0, 1.0))).a;
                        @else
                            texel.a = WRAP(texel.a, -1.01, 1.01);
                        @end
                    @else
                        @if(o_pica_texture_env_clamp)
                            texel.a = oot3dPicaByteRound(vec4(clamp(texel.a, 0.0, 1.0))).a;
                        @else
                            texel.a = WRAP(texel.a, -0.51, 1.51);
                        @end
                    @end
                @end

                @if(o_c[c][0][2] == SHADER_COMBINED)
                    @if(o_pica_texture_env_clamp)
                        texel.rgb = oot3dPicaByteRound(clamp(texel.rgb, 0.0, 1.0));
                    @else
                        texel.rgb = WRAP(texel.rgb, -1.01, 1.01);
                    @end
                @else
                    @if(o_pica_texture_env_clamp)
                        texel.rgb = oot3dPicaByteRound(clamp(texel.rgb, 0.0, 1.0));
                    @else
                        texel.rgb = WRAP(texel.rgb, -0.51, 1.51);
                    @end
                @end
            @end

            @if(!o_color_alpha_same[c] && o_alpha)
                texel = vec4(@{
                append_formula(o_c[c], o_do_single[c][0],
                            o_do_multiply[c][0], o_do_mix[c][0], false, false, true, c == 0)
                }, @{append_formula(o_c[c], o_do_single[c][1],
                            o_do_multiply[c][1], o_do_mix[c][1], true, true, true, c == 0)
                });
            @else
                texel = @{append_formula(o_c[c], o_do_single[c][0],
                            o_do_multiply[c][0], o_do_mix[c][0], o_alpha, false,
                            o_alpha, c == 0)};
            @end
        @end

        @if(o_pica_texture_env_clamp)
            @for(i in 0..o_inputs)
                #undef vInput@{i + 1}
            @end
        @elseif(o_oot3d_pica_shadow2d)
            #undef vInput1
        @end

        @if(o_oot3d_pica_texture2_mult_add)
            texel.rgb = clamp(texel.rgb, 0.0, 1.0);
            texel.rgb = clamp(@{texture}(uOot3dPicaTex2, vOot3dPicaTexCoord2).rgb * texVal1.rgb +
                              texel.rgb, 0.0, 1.0);
        @end

        @if(o_pica_texture_env_post_multiply)
            texel.rgb *= vInput1.rgb;
        @end

        @if(o_pica_texture_env_clamp)
            texel = oot3dPicaByteRound(clamp(texel, 0.0, 1.0));
        @else
            texel = WRAP(texel, -0.51, 1.51);
            texel = clamp(texel, 0.0, 1.0);
        @end
        // TODO discard if alpha is 0?
        @if(o_fog)
            @if(o_oot3d_pica_fog)
                float oot3dPicaFogFactor = oot3dSamplePicaFogFactor();
                @if(o_alpha)
                    texel = vec4(mix(vFog.rgb, texel.rgb, oot3dPicaFogFactor), texel.a);
                @else
                    texel = mix(vFog.rgb, texel, oot3dPicaFogFactor);
                @end
            @else
                @if(o_alpha)
                    texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);
                @else
                    texel = mix(texel, vFog.rgb, vFog.a);
                @end
            @end
        @end

        @if(o_texture_edge && o_alpha)
            if (texel.a > 0.19) texel.a = 1.0; else discard;
        @end

        @if(o_alpha && o_noise)
            texel.a *= floor(clamp(random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + texel.a, 0.0, 1.0));
        @end

        @if(o_grayscale)
            float intensity = (texel.r + texel.g + texel.b) / 3.0;
            vec3 new_texel = vGrayscaleColor.rgb * intensity;
            texel.rgb = mix(texel.rgb, new_texel, vGrayscaleColor.a);
        @end

        @if(o_alpha)
            @if(o_alpha_threshold)
                @if(o_oot3d_pica_alpha_test)
                    if (oot3d_pica_alpha_test_enabled != 0) {
                        int alpha_u8 = int(clamp(texel.a, 0.0, 1.0) * 255.0);
                        bool alpha_pass = false;
                        if (oot3d_pica_alpha_test_func == 1) alpha_pass = true;
                        else if (oot3d_pica_alpha_test_func == 2) alpha_pass = alpha_u8 == oot3d_pica_alpha_test_ref;
                        else if (oot3d_pica_alpha_test_func == 3) alpha_pass = alpha_u8 != oot3d_pica_alpha_test_ref;
                        else if (oot3d_pica_alpha_test_func == 4) alpha_pass = alpha_u8 < oot3d_pica_alpha_test_ref;
                        else if (oot3d_pica_alpha_test_func == 5) alpha_pass = alpha_u8 <= oot3d_pica_alpha_test_ref;
                        else if (oot3d_pica_alpha_test_func == 6) alpha_pass = alpha_u8 > oot3d_pica_alpha_test_ref;
                        else if (oot3d_pica_alpha_test_func == 7) alpha_pass = alpha_u8 >= oot3d_pica_alpha_test_ref;
                        if (!alpha_pass) discard;
                    } else if (texel.a < 8.0 / 256.0) discard;
                @else
                    if (texel.a < 8.0 / 256.0) discard;
                @end
            @end
            @if(o_invisible)
                texel.a = 0.0;
            @end
            @{vOutColor} = texel;
        @else
            @{vOutColor} = vec4(texel, 1.0);
        @end

        @if(srgb_mode)
            @{vOutColor} = fromLinear(@{vOutColor});
        @end

        @if(o_prim_depth)
            gl_FragDepth = prim_depth;
        @end
    }
@end
