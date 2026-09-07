#include "ship/window/Window.h"
#ifdef ENABLE_OPENGL

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <algorithm>
#include <map>
#include <unordered_map>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif

#ifdef __MINGW32__
#define FOR_WINDOWS 1
#else
#define FOR_WINDOWS 0
#endif

#include "fast/backends/gfx_opengl.h"
#include "ship/window/gui/Gui.h"
#include <prism/processor.h>
#include <fstream>
#include "ship/Context.h"
#include "ship/resource/factory/ShaderFactory.h"
#include "fast/interpreter.h"
#include "ship/config/ConsoleVariable.h"

namespace Fast {
namespace {
constexpr uint32_t kOot3dShadow2dTextureUnit = 6;
constexpr size_t kOot3dPicaFogLutEntryCount = 128;
constexpr size_t kOot3dCachedVertexBufferLimit = 1024;

bool IsPicaTextureEnvShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE_ENV ||
           shaderId == SHADER_ID_PICA_TEXTURE_ENV_SHADOW2D ||
           shaderId == SHADER_ID_PICA_TEXTURE_ENV_POST_MULTIPLY ||
           shaderId == SHADER_ID_PICA_TEXTURE2_MULT_ADD;
}

bool IsOot3dPicaShadow2dShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE_ENV_SHADOW2D;
}

bool IsPicaTextureEnvPostMultiplyShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE_ENV_POST_MULTIPLY;
}

bool IsOot3dPicaTexture2MultAddShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE2_MULT_ADD;
}

bool IsOot3dPicaFogShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_FOG || IsPicaTextureEnvShader(shaderId);
}

bool IsOot3dPicaAlphaTestShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_ALPHA_TEST || IsOot3dPicaFogShader(shaderId);
}

bool IsOot3dNativeTransformShader(int16_t shaderId) {
    return shaderId == SHADER_ID_OOT3D_NATIVE_DEFAULT ||
           shaderId == SHADER_ID_PICA_TEXTURE_ENV ||
           shaderId == SHADER_ID_PICA_TEXTURE_ENV_SHADOW2D ||
           shaderId == SHADER_ID_PICA_TEXTURE_ENV_POST_MULTIPLY ||
           shaderId == SHADER_ID_PICA_FOG ||
           shaderId == SHADER_ID_PICA_ALPHA_TEST ||
           shaderId == SHADER_ID_PICA_TEXTURE2_MULT_ADD;
}

GLenum NativeBlendEquationToOpenGL(GfxNativeBlendEquation equation) {
    switch (equation) {
        case GfxNativeBlendEquation::Add:
            return GL_FUNC_ADD;
        case GfxNativeBlendEquation::Subtract:
            return GL_FUNC_SUBTRACT;
        case GfxNativeBlendEquation::ReverseSubtract:
            return GL_FUNC_REVERSE_SUBTRACT;
        case GfxNativeBlendEquation::Min:
            return GL_MIN;
        case GfxNativeBlendEquation::Max:
            return GL_MAX;
    }
    return GL_FUNC_ADD;
}

GLenum NativeBlendFactorToOpenGL(GfxNativeBlendFactor factor) {
    switch (factor) {
        case GfxNativeBlendFactor::Zero:
            return GL_ZERO;
        case GfxNativeBlendFactor::One:
            return GL_ONE;
        case GfxNativeBlendFactor::SourceColor:
            return GL_SRC_COLOR;
        case GfxNativeBlendFactor::OneMinusSourceColor:
            return GL_ONE_MINUS_SRC_COLOR;
        case GfxNativeBlendFactor::DestColor:
            return GL_DST_COLOR;
        case GfxNativeBlendFactor::OneMinusDestColor:
            return GL_ONE_MINUS_DST_COLOR;
        case GfxNativeBlendFactor::SourceAlpha:
            return GL_SRC_ALPHA;
        case GfxNativeBlendFactor::OneMinusSourceAlpha:
            return GL_ONE_MINUS_SRC_ALPHA;
        case GfxNativeBlendFactor::DestAlpha:
            return GL_DST_ALPHA;
        case GfxNativeBlendFactor::OneMinusDestAlpha:
            return GL_ONE_MINUS_DST_ALPHA;
        case GfxNativeBlendFactor::ConstantColor:
            return GL_CONSTANT_COLOR;
        case GfxNativeBlendFactor::OneMinusConstantColor:
            return GL_ONE_MINUS_CONSTANT_COLOR;
        case GfxNativeBlendFactor::ConstantAlpha:
            return GL_CONSTANT_ALPHA;
        case GfxNativeBlendFactor::OneMinusConstantAlpha:
            return GL_ONE_MINUS_CONSTANT_ALPHA;
        case GfxNativeBlendFactor::SourceAlphaSaturate:
            return GL_SRC_ALPHA_SATURATE;
    }
    return GL_ONE;
}
} // namespace

int GfxRenderingAPIOGL::GetMaxTextureSize() {
    GLint max_texture_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    return max_texture_size;
}

const char* GfxRenderingAPIOGL::GetName() {
    return "OpenGL";
}

GfxClipParameters GfxRenderingAPIOGL::GetClipParameters() {
    return { false, mFrameBuffers[mCurrentFrameBuffer].invertY };
}

static void VertexArraySetAttribs(ShaderProgram* prg) {
    size_t numFloats = prg->numFloats;
    size_t pos = 0;

    for (int i = 0; i < prg->numAttribs; i++) {
        if (prg->attribLocations[i] >= 0) {
            glEnableVertexAttribArray(prg->attribLocations[i]);
            glVertexAttribPointer(prg->attribLocations[i], prg->attribSizes[i], GL_FLOAT, GL_FALSE,
                                  numFloats * sizeof(float), (void*)(pos * sizeof(float)));
        }
        pos += prg->attribSizes[i];
    }
}

void GfxRenderingAPIOGL::SetUniforms(ShaderProgram* prg) const {
    glUniform1i(prg->frameCountLocation, mFrameCount);
    glUniform1f(prg->noiseScaleLocation, mCurrentNoiseScale);
}

void GfxRenderingAPIOGL::SetPerDrawUniforms() {
    glUniform1f(mCurrentShaderProgram->prim_depth_location, mCurrentPrimDepth);

    if (mCurrentShaderProgram->usedTextures[0] || mCurrentShaderProgram->usedTextures[1]) {
        GLint filtering[2] = { textures[mCurrentTextureIds[0]].filtering, textures[mCurrentTextureIds[1]].filtering };
        glUniform1iv(mCurrentShaderProgram->texture_filtering_location, 2, filtering);

        GLint width[2] = { textures[mCurrentTextureIds[0]].width, textures[mCurrentTextureIds[1]].width };
        glUniform1iv(mCurrentShaderProgram->texture_width_location, 2, width);

        GLint height[2] = { textures[mCurrentTextureIds[0]].height, textures[mCurrentTextureIds[1]].height };
        glUniform1iv(mCurrentShaderProgram->texture_height_location, 2, height);
    }
}

void GfxRenderingAPIOGL::UnloadShader(ShaderProgram* old_prg) {
    if (old_prg != nullptr && old_prg == mLastLoadedShader) {
        for (unsigned int i = 0; i < old_prg->numAttribs; i++) {
            if (old_prg->attribLocations[i] >= 0) {
                glDisableVertexAttribArray(old_prg->attribLocations[i]);
            }
        }
        mLastLoadedShader = nullptr;
    }
}

void GfxRenderingAPIOGL::LoadShader(ShaderProgram* new_prg) {
    // if (!new_prg) return;
    mCurrentShaderProgram = new_prg;
    if (new_prg != mLastLoadedShader) {
        glUseProgram(new_prg->openglProgramId);
        VertexArraySetAttribs(new_prg);
        mLastLoadedShader = new_prg;
    }
    SetUniforms(new_prg);
}

#define RAND_NOISE "((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)"

static const char* shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha,
                                      bool first_cycle, bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_1:
                return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_TEXEL0:
                return first_cycle ? (with_alpha ? "texVal0" : "texVal0.rgb")
                                   : (with_alpha ? "texVal1" : "texVal1.rgb");
            case SHADER_TEXEL0A:
                return first_cycle
                           ? (hint_single_element ? "texVal0.a"
                                                  : (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)"
                                                                : "vec3(texVal0.a, texVal0.a, texVal0.a)"))
                           : (hint_single_element ? "texVal1.a"
                                                  : (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)"
                                                                : "vec3(texVal1.a, texVal1.a, texVal1.a)"));
            case SHADER_TEXEL1A:
                return first_cycle
                           ? (hint_single_element ? "texVal1.a"
                                                  : (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)"
                                                                : "vec3(texVal1.a, texVal1.a, texVal1.a)"))
                           : (hint_single_element ? "texVal0.a"
                                                  : (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)"
                                                                : "vec3(texVal0.a, texVal0.a, texVal0.a)"));
            case SHADER_TEXEL1:
                return first_cycle ? (with_alpha ? "texVal1" : "texVal1.rgb")
                                   : (with_alpha ? "texVal0" : "texVal0.rgb");
            case SHADER_COMBINED:
                return with_alpha ? "texel" : "texel.rgb";
            case SHADER_NOISE:
                return with_alpha ? "vec4(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")"
                                  : "vec3(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")";
        }
    } else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_1:
                return "1.0";
            case SHADER_INPUT_1:
                return "vInput1.a";
            case SHADER_INPUT_2:
                return "vInput2.a";
            case SHADER_INPUT_3:
                return "vInput3.a";
            case SHADER_INPUT_4:
                return "vInput4.a";
            case SHADER_TEXEL0:
                return first_cycle ? "texVal0.a" : "texVal1.a";
            case SHADER_TEXEL0A:
                return first_cycle ? "texVal0.a" : "texVal1.a";
            case SHADER_TEXEL1A:
                return first_cycle ? "texVal1.a" : "texVal0.a";
            case SHADER_TEXEL1:
                return first_cycle ? "texVal1.a" : "texVal0.a";
            case SHADER_COMBINED:
                return "texel.a";
            case SHADER_NOISE:
                return RAND_NOISE;
        }
    }
    return "";
}

bool get_bool(prism::ContextTypes* value) {
    if (std::holds_alternative<int>(*value)) {
        return std::get<int>(*value) == 1;
    }
    return false;
}

prism::ContextTypes* append_formula(prism::ContextTypes* _, prism::ContextTypes* a_arg, prism::ContextTypes* a_single,
                                    prism::ContextTypes* a_mult, prism::ContextTypes* a_mix,
                                    prism::ContextTypes* a_with_alpha, prism::ContextTypes* a_only_alpha,
                                    prism::ContextTypes* a_alpha, prism::ContextTypes* a_first_cycle) {
    auto c = std::get<prism::MTDArray<int>>(*a_arg);
    bool do_single = get_bool(a_single);
    bool do_multiply = get_bool(a_mult);
    bool do_mix = get_bool(a_mix);
    bool with_alpha = get_bool(a_with_alpha);
    bool only_alpha = get_bool(a_only_alpha);
    bool opt_alpha = get_bool(a_alpha);
    bool first_cycle = get_bool(a_first_cycle);
    std::string out = "";
    if (do_single) {
        out += shader_item_to_str(c.at(only_alpha, 3), with_alpha, only_alpha, opt_alpha, first_cycle, false);
    } else if (do_multiply) {
        out += shader_item_to_str(c.at(only_alpha, 0), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += " * ";
        out += shader_item_to_str(c.at(only_alpha, 2), with_alpha, only_alpha, opt_alpha, first_cycle, true);
    } else if (do_mix) {
        out += "mix(";
        out += shader_item_to_str(c.at(only_alpha, 1), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += ", ";
        out += shader_item_to_str(c.at(only_alpha, 0), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += ", ";
        out += shader_item_to_str(c.at(only_alpha, 2), with_alpha, only_alpha, opt_alpha, first_cycle, true);
        out += ")";
    } else {
        out += "(";
        out += shader_item_to_str(c.at(only_alpha, 0), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += " - ";
        out += shader_item_to_str(c.at(only_alpha, 1), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += ") * ";
        out += shader_item_to_str(c.at(only_alpha, 2), with_alpha, only_alpha, opt_alpha, first_cycle, true);
        out += " + ";
        out += shader_item_to_str(c.at(only_alpha, 3), with_alpha, only_alpha, opt_alpha, first_cycle, false);
    }
    return new prism::ContextTypes{ out };
}

std::optional<std::string> opengl_include_fs(const std::string& path) {
    auto init = std::make_shared<Ship::ResourceInitData>();
    init->Type = (uint32_t)Ship::ResourceType::Shader;
    init->ByteOrder = Ship::Endianness::Native;
    init->Format = RESOURCE_FORMAT_BINARY;
    auto res = std::static_pointer_cast<Ship::Shader>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path, true, init));
    if (res == nullptr) {
        return std::nullopt;
    }
    auto inc = static_cast<std::string*>(res->GetRawPointer());
    return *inc;
}

std::string GfxRenderingAPIOGL::BuildFsShader(const CCFeatures& cc_features) {
    prism::Processor processor;
    prism::ContextItems mContext = {
        { "VERTEX_SHADER", false },
        { "o_c", M_ARRAY(cc_features.c, int, 2, 2, 4) },
        { "o_alpha", cc_features.opt_alpha },
        { "o_fog", cc_features.opt_fog },
        { "o_texture_edge", cc_features.opt_texture_edge },
        { "o_noise", cc_features.opt_noise },
        { "o_2cyc", cc_features.opt_2cyc },
        { "o_alpha_threshold", cc_features.opt_alpha_threshold },
        { "o_invisible", cc_features.opt_invisible },
        { "o_grayscale", cc_features.opt_grayscale },
        { "o_prim_depth", cc_features.opt_prim_depth },
        { "o_textures", M_ARRAY(cc_features.usedTextures, bool, 2) },
        { "o_masks", M_ARRAY(cc_features.used_masks, bool, 2) },
        { "o_blend", M_ARRAY(cc_features.used_blend, bool, 2) },
        { "o_clamp", M_ARRAY(cc_features.clamp, bool, 2, 2) },
        { "o_inputs", cc_features.numInputs },
        { "o_do_mix", M_ARRAY(cc_features.do_mix, bool, 2, 2) },
        { "o_do_single", M_ARRAY(cc_features.do_single, bool, 2, 2) },
        { "o_do_multiply", M_ARRAY(cc_features.do_multiply, bool, 2, 2) },
        { "o_color_alpha_same", M_ARRAY(cc_features.color_alpha_same, bool, 2) },
        { "o_pica_texture_env_clamp", IsPicaTextureEnvShader(cc_features.shader_id) },
        { "o_pica_texture_env_post_multiply",
          IsPicaTextureEnvPostMultiplyShader(cc_features.shader_id) },
        { "o_oot3d_pica_texture2_mult_add",
          IsOot3dPicaTexture2MultAddShader(cc_features.shader_id) },
        { "o_oot3d_pica_shadow2d", IsOot3dPicaShadow2dShader(cc_features.shader_id) },
        { "o_oot3d_pica_fog",
          cc_features.opt_fog && IsOot3dPicaFogShader(cc_features.shader_id) },
        { "o_oot3d_pica_alpha_test",
          cc_features.opt_alpha_threshold && IsOot3dPicaAlphaTestShader(cc_features.shader_id) },
        { "FILTER_THREE_POINT", FILTER_THREE_POINT },
        { "FILTER_LINEAR", FILTER_LINEAR },
        { "FILTER_NONE", FILTER_NONE },
        { "srgb_mode", mSrgbMode },
        { "SHADER_0", SHADER_0 },
        { "SHADER_INPUT_1", SHADER_INPUT_1 },
        { "SHADER_INPUT_2", SHADER_INPUT_2 },
        { "SHADER_INPUT_3", SHADER_INPUT_3 },
        { "SHADER_INPUT_4", SHADER_INPUT_4 },
        { "SHADER_INPUT_5", SHADER_INPUT_5 },
        { "SHADER_INPUT_6", SHADER_INPUT_6 },
        { "SHADER_INPUT_7", SHADER_INPUT_7 },
        { "SHADER_TEXEL0", SHADER_TEXEL0 },
        { "SHADER_TEXEL0A", SHADER_TEXEL0A },
        { "SHADER_TEXEL1", SHADER_TEXEL1 },
        { "SHADER_TEXEL1A", SHADER_TEXEL1A },
        { "SHADER_1", SHADER_1 },
        { "SHADER_COMBINED", SHADER_COMBINED },
        { "SHADER_NOISE", SHADER_NOISE },
        { "o_three_point_filtering", mCurrentFilterMode == FILTER_THREE_POINT },
        { "append_formula", (InvokeFunc)append_formula },
#if defined(__APPLE__) || defined(__SWITCH__)
        { "GLSL_VERSION", "#version 410 core" },
        { "attr", "in" },
        { "opengles", false },
        { "core_opengl", true },
        { "texture", "texture" },
        { "vOutColor", "vOutColor" },
#elif defined(USE_OPENGLES)
        { "GLSL_VERSION", "#version 300 es\nprecision mediump float;" },
        { "attr", "in" },
        { "opengles", true },
        { "core_opengl", false },
        { "texture", "texture" },
        { "vOutColor", "vOutColor" },
#else
        { "GLSL_VERSION", "#version 130" },
        { "attr", "varying" },
        { "opengles", false },
        { "core_opengl", false },
        { "texture", "texture2D" },
        { "vOutColor", "gl_FragColor" },
#endif
    };
    processor.populate(mContext);
    auto init = std::make_shared<Ship::ResourceInitData>();
    init->Type = (uint32_t)Ship::ResourceType::Shader;
    init->ByteOrder = Ship::Endianness::Native;
    init->Format = RESOURCE_FORMAT_BINARY;
    const char* shaderName = Fast::gfx_get_shader(cc_features.shader_id);
    std::string path = "shaders/opengl/default.shader.glsl";

    if (nullptr != shaderName) {
        path = std::string(shaderName) + ".glsl";
    }

    auto res = static_pointer_cast<Ship::Shader>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path, true, init));

    if (res == nullptr) {
        SPDLOG_ERROR("Failed to load default fragment shader, missing f3d.o2r?");
        abort();
    }

    auto shader = static_cast<std::string*>(res->GetRawPointer());
    processor.load(*shader);
    processor.bind_include_loader(opengl_include_fs);
    auto result = processor.process();
    // SPDLOG_INFO("=========== FRAGMENT SHADER ============");
    // SPDLOG_INFO(result);
    // SPDLOG_INFO("========================================");
    return result;
}

static size_t numFloats = 0;

static prism::ContextTypes* UpdateFloats(prism::ContextTypes* _, prism::ContextTypes* num) {
    numFloats += std::get<int>(*num);
    return nullptr;
}

static std::string BuildVsShader(const CCFeatures& cc_features) {
    numFloats = 4;
    prism::Processor processor;
    prism::ContextItems mContext = { { "VERTEX_SHADER", true },
                                     { "o_textures", M_ARRAY(cc_features.usedTextures, bool, 2) },
                                     { "o_clamp", M_ARRAY(cc_features.clamp, bool, 2, 2) },
                                     { "o_fog", cc_features.opt_fog },
                                     { "o_grayscale", cc_features.opt_grayscale },
                                     { "o_alpha", cc_features.opt_alpha },
                                     { "o_inputs", cc_features.numInputs },
                                     { "o_pica_texture_env_clamp",
                                       IsPicaTextureEnvShader(cc_features.shader_id) },
                                     { "o_pica_texture_env_post_multiply",
                                       IsPicaTextureEnvPostMultiplyShader(cc_features.shader_id) },
                                     { "o_oot3d_pica_texture2_mult_add",
                                       IsOot3dPicaTexture2MultAddShader(cc_features.shader_id) },
                                     { "o_oot3d_pica_shadow2d",
                                       IsOot3dPicaShadow2dShader(cc_features.shader_id) },
                                     { "o_oot3d_pica_fog",
                                       cc_features.opt_fog && IsOot3dPicaFogShader(cc_features.shader_id) },
                                     { "o_oot3d_native_transform",
                                       IsOot3dNativeTransformShader(cc_features.shader_id) },
                                     { "update_floats", (InvokeFunc)UpdateFloats },
#if defined(__APPLE__) || defined(__SWITCH__)
                                     { "GLSL_VERSION", "#version 410 core" },
                                     { "attr", "in" },
                                     { "out", "out" },
                                     { "opengles", false }
#elif defined(USE_OPENGLES)
                                     { "GLSL_VERSION", "#version 300 es" },
                                     { "attr", "in" },
                                     { "out", "out" },
                                     { "opengles", true }
#else
                                     { "GLSL_VERSION", "#version 110" },
                                     { "attr", "attribute" },
                                     { "out", "varying" },
                                     { "opengles", false }
#endif
    };
    processor.populate(mContext);

    auto init = std::make_shared<Ship::ResourceInitData>();
    init->Type = (uint32_t)Ship::ResourceType::Shader;
    init->ByteOrder = Ship::Endianness::Native;
    init->Format = RESOURCE_FORMAT_BINARY;
    const char* shaderName = Fast::gfx_get_shader(cc_features.shader_id);
    std::string path = "shaders/opengl/default.shader.glsl";

    if (nullptr != shaderName) {
        path = std::string(shaderName) + ".glsl";
    }

    auto res = static_pointer_cast<Ship::Shader>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path, true, init));

    if (res == nullptr) {
        SPDLOG_ERROR("Failed to load default vertex shader, missing f3d.o2r?");
        abort();
    }

    auto shader = static_cast<std::string*>(res->GetRawPointer());
    processor.load(*shader);
    processor.bind_include_loader(opengl_include_fs);
    auto result = processor.process();
    // SPDLOG_INFO("=========== VERTEX SHADER ============");
    // SPDLOG_INFO(result);
    // SPDLOG_INFO("========================================");
    return result;
}

void GfxRenderingAPIOGL::ClearShaderCache() {
    mShaderProgramPool.clear();
}

ShaderProgram* GfxRenderingAPIOGL::CreateAndLoadNewShader(uint64_t shader_id0, uint64_t shader_id1) {
    CCFeatures cc_features;
    gfx_cc_get_features(shader_id0, shader_id1, &cc_features);
    const auto fs_buf = BuildFsShader(cc_features);
    const auto vs_buf = BuildVsShader(cc_features);
    const GLchar* sources[2] = { vs_buf.data(), fs_buf.data() };
    const GLint lengths[2] = { (GLint)vs_buf.size(), (GLint)fs_buf.size() };
    GLint success;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 0;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        // fprintf(stderr, "Vertex shader compilation failed\n");
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
        // fprintf(stderr, "%s\n", &error_log[0]);
        abort();
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 0;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        fprintf(stderr, "Fragment shader compilation failed\n");
        glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
        fprintf(stderr, "%s\n", &error_log[0]);
        abort();
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    size_t cnt = 0;

    struct ShaderProgram* prg = &mShaderProgramPool[std::make_pair(shader_id0, shader_id1)];
    prg->attribLocations[cnt] = glGetAttribLocation(shader_program, "aVtxPos");
    prg->attribSizes[cnt] = 4;
    ++cnt;

    for (int i = 0; i < 2; i++) {
        if (cc_features.usedTextures[i]) {
            char name[32];
            snprintf(name, sizeof(name), "aTexCoord%d", i);
            prg->attribLocations[cnt] = glGetAttribLocation(shader_program, name);
            prg->attribSizes[cnt] = 2;
            ++cnt;

            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    snprintf(name, sizeof(name), "aTexClamp%s%d", j == 0 ? "S" : "T", i);
                    prg->attribLocations[cnt] = glGetAttribLocation(shader_program, name);
                    prg->attribSizes[cnt] = 1;
                    ++cnt;
                }
            }
        }
    }

    if (IsOot3dPicaTexture2MultAddShader(cc_features.shader_id)) {
        prg->attribLocations[cnt] = glGetAttribLocation(shader_program, "aOot3dPicaTexCoord2");
        prg->attribSizes[cnt] = 2;
        ++cnt;
    }

    if (cc_features.opt_fog) {
        prg->attribLocations[cnt] = glGetAttribLocation(shader_program, "aFog");
        prg->attribSizes[cnt] = 4;
        ++cnt;
    }

    if (cc_features.opt_grayscale) {
        prg->attribLocations[cnt] = glGetAttribLocation(shader_program, "aGrayscaleColor");
        prg->attribSizes[cnt] = 4;
        ++cnt;
    }

    for (int i = 0; i < cc_features.numInputs; i++) {
        char name[16];
        snprintf(name, sizeof(name), "aInput%d", i + 1);
        prg->attribLocations[cnt] = glGetAttribLocation(shader_program, name);
        prg->attribSizes[cnt] = cc_features.opt_alpha ? 4 : 3;
        ++cnt;
    }

    if (IsOot3dPicaShadow2dShader(cc_features.shader_id)) {
        prg->attribLocations[cnt] = glGetAttribLocation(shader_program, "aOot3dShadowTexCoord");
        prg->attribSizes[cnt] = 3;
        ++cnt;
    }

    prg->openglProgramId = shader_program;
    prg->numInputs = cc_features.numInputs;
    prg->usedTextures[0] = cc_features.usedTextures[0];
    prg->usedTextures[1] = cc_features.usedTextures[1];
    prg->usedTextures[2] = cc_features.used_masks[0];
    prg->usedTextures[3] = cc_features.used_masks[1];
    prg->usedTextures[4] = cc_features.used_blend[0];
    prg->usedTextures[5] = cc_features.used_blend[1];
    prg->numFloats = numFloats;
    prg->numAttribs = cnt;

    prg->frameCountLocation = glGetUniformLocation(shader_program, "frame_count");
    prg->noiseScaleLocation = glGetUniformLocation(shader_program, "noise_scale");
    prg->prim_depth_location = glGetUniformLocation(shader_program, "prim_depth");
    prg->texture_width_location = glGetUniformLocation(shader_program, "texture_width");
    prg->texture_height_location = glGetUniformLocation(shader_program, "texture_height");
    prg->texture_filtering_location = glGetUniformLocation(shader_program, "texture_filtering");
    prg->oot3d_shadow2d_texture_bias_location =
        glGetUniformLocation(shader_program, "oot3d_shadow2d_texture_bias");
    prg->oot3d_shadow2d_orthographic_location =
        glGetUniformLocation(shader_program, "oot3d_shadow2d_orthographic");
    prg->oot3d_shadow2d_invert_location =
        glGetUniformLocation(shader_program, "oot3d_shadow2d_invert");
    prg->oot3d_pica_fog_lut_location =
        glGetUniformLocation(shader_program, "oot3d_pica_fog_lut");
    prg->oot3d_pica_fog_flip_location =
        glGetUniformLocation(shader_program, "oot3d_pica_fog_flip");
    prg->oot3d_pica_alpha_test_enabled_location =
        glGetUniformLocation(shader_program, "oot3d_pica_alpha_test_enabled");
    prg->oot3d_pica_alpha_test_func_location =
        glGetUniformLocation(shader_program, "oot3d_pica_alpha_test_func");
    prg->oot3d_pica_alpha_test_ref_location =
        glGetUniformLocation(shader_program, "oot3d_pica_alpha_test_ref");
    prg->oot3d_native_transform_location =
        glGetUniformLocation(shader_program, "uOot3dModelViewProjection");
    prg->oot3d_pica_fog_state_key = 0;
    prg->oot3d_pica_fog_state_key_valid = false;

    LoadShader(prg);

    if (cc_features.usedTextures[0]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex0");
        glUniform1i(sampler_location, 0);
    }
    if (cc_features.usedTextures[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex1");
        glUniform1i(sampler_location, 1);
    }
    if (IsOot3dPicaTexture2MultAddShader(cc_features.shader_id)) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uOot3dPicaTex2");
        glUniform1i(sampler_location, 2);
    }
    if (cc_features.used_masks[0]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTexMask0");
        glUniform1i(sampler_location, 2);
    }
    if (cc_features.used_masks[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTexMask1");
        glUniform1i(sampler_location, 3);
    }
    if (cc_features.used_blend[0]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTexBlend0");
        glUniform1i(sampler_location, 4);
    }
    if (cc_features.used_blend[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTexBlend1");
        glUniform1i(sampler_location, 5);
    }
    if (IsOot3dPicaShadow2dShader(cc_features.shader_id)) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uOot3dShadow2d");
        glUniform1i(sampler_location, kOot3dShadow2dTextureUnit);
    }

    return prg;
}

struct ShaderProgram* GfxRenderingAPIOGL::LookupShader(uint64_t shader_id0, uint64_t shader_id1) {
    auto it = mShaderProgramPool.find(std::make_pair(shader_id0, shader_id1));
    return it == mShaderProgramPool.end() ? nullptr : &it->second;
}

void GfxRenderingAPIOGL::ShaderGetInfo(struct ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) {
    *numInputs = prg->numInputs;
    usedTextures[0] = prg->usedTextures[0];
    usedTextures[1] = prg->usedTextures[1];
}

GLuint GfxRenderingAPIOGL::NewTexture() {
    GLuint ret;
    glGenTextures(1, &ret);
    textures.resize(std::max(textures.size(), (size_t)ret + 1));
    return ret;
}

void GfxRenderingAPIOGL::DeleteTexture(uint32_t texID) {
    glDeleteTextures(1, &texID);
}

void GfxRenderingAPIOGL::SelectTexture(int tile, GLuint texture_id) {
    if (mLastActiveTexture != tile) {
        mLastActiveTexture = tile;
        glActiveTexture(GL_TEXTURE0 + tile);
    }
    if (mLastBoundTextures[tile] != texture_id) {
        mLastBoundTextures[tile] = texture_id;
        glBindTexture(GL_TEXTURE_2D, texture_id);
    }
    mCurrentTextureIds[tile] = texture_id;
    mCurrentTile = tile;
}

void GfxRenderingAPIOGL::UploadTexture(const uint8_t* rgba32_buf, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
    textures[mCurrentTextureIds[mCurrentTile]].width = width;
    textures[mCurrentTextureIds[mCurrentTile]].height = height;
}

bool GfxRenderingAPIOGL::UploadTextureMipLevel(uint32_t level, const uint8_t* rgba32_buf,
                                               uint32_t width, uint32_t height) {
    if (level == 0 || width == 0 || height == 0 || rgba32_buf == nullptr) {
        return false;
    }
    glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
    return true;
}

#if defined(USE_OPENGLES) || defined(__SWITCH__)
#define GL_MIRROR_CLAMP_TO_EDGE 0x8743
#endif

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    switch (val) {
        case G_TX_NOMIRROR | G_TX_CLAMP:
            return GL_CLAMP_TO_EDGE;
        case G_TX_MIRROR | G_TX_WRAP:
            return GL_MIRRORED_REPEAT;
        case G_TX_MIRROR | G_TX_CLAMP:
            return GL_MIRROR_CLAMP_TO_EDGE;
        case G_TX_NOMIRROR | G_TX_WRAP:
            return GL_REPEAT;
    }
    return 0;
}

static GLint gfx_native_filter_to_opengl(GfxNativeTextureFilter filter) {
    switch (filter) {
        case GfxNativeTextureFilter::Nearest:
            return GL_NEAREST;
        case GfxNativeTextureFilter::Linear:
            return GL_LINEAR;
        case GfxNativeTextureFilter::NearestMipmapNearest:
            return GL_NEAREST_MIPMAP_NEAREST;
        case GfxNativeTextureFilter::LinearMipmapNearest:
            return GL_LINEAR_MIPMAP_NEAREST;
        case GfxNativeTextureFilter::NearestMipmapLinear:
            return GL_NEAREST_MIPMAP_LINEAR;
        case GfxNativeTextureFilter::LinearMipmapLinear:
            return GL_LINEAR_MIPMAP_LINEAR;
    }
    return GL_NEAREST;
}

static GLint gfx_native_wrap_to_opengl(GfxNativeTextureWrap wrap) {
    switch (wrap) {
        case GfxNativeTextureWrap::ClampToEdge:
            return GL_CLAMP_TO_EDGE;
        case GfxNativeTextureWrap::Repeat:
            return GL_REPEAT;
        case GfxNativeTextureWrap::MirroredRepeat:
            return GL_MIRRORED_REPEAT;
    }
    return GL_REPEAT;
}

void GfxRenderingAPIOGL::SetSamplerParameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    if (mLastActiveTexture != tile) {
        mLastActiveTexture = tile;
        glActiveTexture(GL_TEXTURE0 + tile);
    }
    const GLint filter = linear_filter && mCurrentFilterMode == FILTER_LINEAR ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    textures[mCurrentTextureIds[tile]].filtering = !linear_filter ? FILTER_LINEAR : FILTER_THREE_POINT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
}

bool GfxRenderingAPIOGL::SetNativeSamplerParameters(int tile, const GfxNativeSamplerState& state) {
    if (mLastActiveTexture != tile) {
        mLastActiveTexture = tile;
        glActiveTexture(GL_TEXTURE0 + tile);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    gfx_native_filter_to_opengl(state.MinFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    gfx_native_filter_to_opengl(state.MagFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                    gfx_native_wrap_to_opengl(state.WrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    gfx_native_wrap_to_opengl(state.WrapT));

    bool complete = true;
#ifdef GL_TEXTURE_BASE_LEVEL
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
#else
    complete = false;
#endif
#ifdef GL_TEXTURE_MAX_LEVEL
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(state.MaxMipLevel));
#else
    complete = false;
#endif
#ifdef GL_TEXTURE_LOD_BIAS
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, state.LodBias);
#else
    complete = complete && state.LodBias == 0.0f;
#endif
    textures[mCurrentTextureIds[tile]].filtering = FILTER_LINEAR;
    return complete;
}

void GfxRenderingAPIOGL::SetDepthTestAndMask(bool depth_test, bool z_upd) {
    mCurrentDepthTest = depth_test;
    mCurrentDepthMask = z_upd;
}

void GfxRenderingAPIOGL::SetCurrentPrimDepth(float depth) {
    if (depth != mCurrentPrimDepth) {
        mCurrentPrimDepth = depth;
        mPrimDepthDirty = true;
    }
}

void GfxRenderingAPIOGL::SetZmodeDecal(bool zmode_decal) {
    mCurrentZmodeDecal = zmode_decal;
}

void GfxRenderingAPIOGL::SetViewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

void GfxRenderingAPIOGL::SetScissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
}

void GfxRenderingAPIOGL::SetUseAlpha(bool use_alpha) {
    int8_t val = use_alpha ? 1 : 0;
    if (mLastBlendEnabled != val) {
        mLastBlendEnabled = val;
        if (use_alpha) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
    }
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendColor(0.0f, 0.0f, 0.0f, 0.0f);
}

bool GfxRenderingAPIOGL::SetNativeBlendState(const GfxNativeBlendState& state) {
    if (!state.Enabled) {
        SetUseAlpha(false);
        return true;
    }

    if (mLastBlendEnabled != 1) {
        mLastBlendEnabled = 1;
        glEnable(GL_BLEND);
    }
    glBlendEquationSeparate(NativeBlendEquationToOpenGL(state.EquationRgb),
                            NativeBlendEquationToOpenGL(state.EquationAlpha));
    glBlendFuncSeparate(NativeBlendFactorToOpenGL(state.SourceRgb),
                        NativeBlendFactorToOpenGL(state.DestRgb),
                        NativeBlendFactorToOpenGL(state.SourceAlpha),
                        NativeBlendFactorToOpenGL(state.DestAlpha));
    glBlendColor(state.ConstantColor[0], state.ConstantColor[1],
                 state.ConstantColor[2], state.ConstantColor[3]);
    return true;
}

bool GfxRenderingAPIOGL::SetNativeCullMode(GfxNativeCullMode mode) {
    if (mode == GfxNativeCullMode::KeepAll) {
        glDisable(GL_CULL_FACE);
        return true;
    }

    const bool keepClockwise = mode == GfxNativeCullMode::KeepClockwise;
    const bool invertY = mFrameBuffers[mCurrentFrameBuffer].invertY;
    const bool frontClockwise = keepClockwise != invertY;
    glEnable(GL_CULL_FACE);
    glFrontFace(frontClockwise ? GL_CW : GL_CCW);
    glCullFace(GL_BACK);
    return true;
}

bool GfxRenderingAPIOGL::SetOot3dNativeTransform(const float* row_major_matrix) {
    if (mCurrentShaderProgram == nullptr ||
        mCurrentShaderProgram->oot3d_native_transform_location < 0) {
        return false;
    }

    GLfloat columnMajorMatrix[16];
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            columnMajorMatrix[column * 4 + row] = row_major_matrix[row * 4 + column];
        }
    }
    glUniformMatrix4fv(mCurrentShaderProgram->oot3d_native_transform_location, 1, GL_FALSE,
                       columnMajorMatrix);
    return true;
}

void GfxRenderingAPIOGL::PrepareTriangleDraw() {
    if (mCurrentDepthTest != mLastDepthTest || mCurrentDepthMask != mLastDepthMask) {
        mLastDepthTest = mCurrentDepthTest;
        mLastDepthMask = mCurrentDepthMask;

        if (mCurrentDepthTest || mLastDepthMask) {
            glEnable(GL_DEPTH_TEST);
            glDepthMask(mLastDepthMask ? GL_TRUE : GL_FALSE);
            glDepthFunc(mCurrentDepthTest ? (mCurrentZmodeDecal ? GL_LEQUAL : GL_LESS) : GL_ALWAYS);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
    }

    if (mCurrentZmodeDecal != mLastZmodeDecal) {
        mLastZmodeDecal = mCurrentZmodeDecal;
        if (mCurrentZmodeDecal) {
            // SSDB = SlopeScaledDepthBias 120 leads to -2 at 240p which is the same as N64 mode which has very little
            // fighting
            const int n64modeFactor = 120;
            const int noVanishFactor = 100;
            GLfloat SSDB = -2;
            switch (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_Z_FIGHTING_MODE, 0)) {
                // scaled z-fighting (N64 mode like)
                case 1:
                    if (mFrameBuffers.size() >
                        mCurrentFrameBuffer) { // safety check for vector size can probably be removed
                        SSDB = -1.0f * (GLfloat)mFrameBuffers[mCurrentFrameBuffer].height / n64modeFactor;
                    }
                    break;
                // no vanishing paths
                case 2:
                    if (mFrameBuffers.size() >
                        mCurrentFrameBuffer) { // safety check for vector size can probably be removed
                        SSDB = -1.0f * (GLfloat)mFrameBuffers[mCurrentFrameBuffer].height / noVanishFactor;
                    }
                    break;
                // disabled
                case 0:
                default:
                    SSDB = -2;
            }
            glPolygonOffset(SSDB, -2);
            glEnable(GL_POLYGON_OFFSET_FILL);
        } else {
            glPolygonOffset(0, 0);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }

    SetPerDrawUniforms();
}

void GfxRenderingAPIOGL::BindArrayBufferForCurrentShader(GLuint buffer) {
    if (buffer == mCurrentArrayBuffer) {
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    mCurrentArrayBuffer = buffer;
    if (mCurrentShaderProgram != nullptr) {
        VertexArraySetAttribs(mCurrentShaderProgram);
    }
}

void GfxRenderingAPIOGL::DrawTriangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    PrepareTriangleDraw();
    BindArrayBufferForCurrentShader(mOpenglVbo);

    // printf("flushing %d tris\n", buf_vbo_num_tris);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);
}

bool GfxRenderingAPIOGL::DrawTrianglesCached(uint64_t cache_id, uint64_t content_version,
                                             const float* buf_vbo, size_t buf_vbo_len,
                                             size_t buf_vbo_num_tris) {
    if (cache_id == 0 || buf_vbo == nullptr || buf_vbo_len == 0 || buf_vbo_num_tris == 0) {
        return false;
    }

    auto cacheIt = mOot3dCachedVertexBuffers.find(cache_id);
    if (cacheIt == mOot3dCachedVertexBuffers.end()) {
        if (mOot3dCachedVertexBuffers.size() >= kOot3dCachedVertexBufferLimit) {
            const auto oldest = std::min_element(
                mOot3dCachedVertexBuffers.begin(), mOot3dCachedVertexBuffers.end(),
                [](const auto& left, const auto& right) {
                    return left.second.LastUsedFrame < right.second.LastUsedFrame;
                });
            if (oldest != mOot3dCachedVertexBuffers.end()) {
                if (mCurrentArrayBuffer == oldest->second.Buffer) {
                    BindArrayBufferForCurrentShader(mOpenglVbo);
                }
                glDeleteBuffers(1, &oldest->second.Buffer);
                mOot3dCachedVertexBuffers.erase(oldest);
            }
        }

        CachedVertexBuffer cached;
        glGenBuffers(1, &cached.Buffer);
        cacheIt = mOot3dCachedVertexBuffers.emplace(cache_id, cached).first;
    }

    auto& cached = cacheIt->second;
    PrepareTriangleDraw();
    BindArrayBufferForCurrentShader(cached.Buffer);
    if (cached.ContentVersion != content_version || cached.FloatCount != buf_vbo_len ||
        cached.TriangleCount != buf_vbo_num_tris) {
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STATIC_DRAW);
        cached.ContentVersion = content_version;
        cached.FloatCount = buf_vbo_len;
        cached.TriangleCount = buf_vbo_num_tris;
    }
    cached.LastUsedFrame = mFrameCount;
    glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);
    return true;
}

void GfxRenderingAPIOGL::Init() {
#if !defined(__linux__) && !defined(__OpenBSD__) && !defined(__SWITCH__)
    glewInit();
#endif

    glGenBuffers(1, &mOpenglVbo);
    glBindBuffer(GL_ARRAY_BUFFER, mOpenglVbo);
    mCurrentArrayBuffer = mOpenglVbo;

#if defined(__APPLE__) || defined(__SWITCH__) || defined(USE_OPENGLES)
    glGenVertexArrays(1, &mOpenglVao);
    glBindVertexArray(mOpenglVao);
#endif

#ifndef USE_OPENGLES // not supported on gles
    glEnable(GL_DEPTH_CLAMP);
#endif
    glDepthFunc(GL_LEQUAL);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendColor(0.0f, 0.0f, 0.0f, 0.0f);

    mFrameBuffers.resize(1); // for the default screen buffer

    glGenRenderbuffers(1, &mPixelDepthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, mPixelDepthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 1, 1);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, &mPixelDepthFb);
    glBindFramebuffer(GL_FRAMEBUFFER, mPixelDepthFb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mPixelDepthRb);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    mPixelDepthRbSize = 1;

    glGetIntegerv(GL_MAX_SAMPLES, &mMaxMsaaLevel);
    InitNativePica();
}

void GfxRenderingAPIOGL::Shutdown() {
    ShutdownNativePica();
}

void GfxRenderingAPIOGL::OnResize() {
}

void GfxRenderingAPIOGL::StartFrame() {
    mFrameCount++;
}

void GfxRenderingAPIOGL::EndFrame() {
    glFlush();
}

void GfxRenderingAPIOGL::FinishRender() {
}

int GfxRenderingAPIOGL::CreateFramebuffer() {
    GLuint clrbuf;
    glGenTextures(1, &clrbuf);
    glBindTexture(GL_TEXTURE_2D, clrbuf);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLuint clrbufMsaa;
    glGenRenderbuffers(1, &clrbufMsaa);

    GLuint rbo;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 1, 1);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    GLuint fbo;
    glGenFramebuffers(1, &fbo);

    size_t i = mFrameBuffers.size();
    mFrameBuffers.resize(i + 1);

    mFrameBuffers[i].fbo = fbo;
    mFrameBuffers[i].clrbuf = clrbuf;
    mFrameBuffers[i].clrbufMsaa = clrbufMsaa;
    mFrameBuffers[i].rbo = rbo;

    return i;
}

void GfxRenderingAPIOGL::UpdateFramebufferParameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                                     bool opengl_invertY, bool render_target, bool has_depth_buffer,
                                                     bool can_extract_depth) {
    FramebufferOGL& fb = mFrameBuffers[fb_id];

    width = std::max(width, 1U);
    height = std::max(height, 1U);
    msaa_level = std::min(msaa_level, (uint32_t)mMaxMsaaLevel);

    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

    if (fb_id != 0) {
        if (fb.width != width || fb.height != height || fb.msaa_level != msaa_level ||
            fb.color_format != GfxFramebufferColorFormat::Rgba8) {
            if (msaa_level <= 1) {
                glBindTexture(GL_TEXTURE_2D, fb.clrbuf);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
                glBindTexture(GL_TEXTURE_2D, 0);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.clrbuf, 0);
            } else {
                glBindRenderbuffer(GL_RENDERBUFFER, fb.clrbufMsaa);
                glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_RGB8, width, height);
                glBindRenderbuffer(GL_RENDERBUFFER, 0);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb.clrbufMsaa);
            }
        }

        if (has_depth_buffer &&
            (fb.width != width || fb.height != height || fb.msaa_level != msaa_level || !fb.has_depth_buffer)) {
            glBindRenderbuffer(GL_RENDERBUFFER, fb.rbo);
            if (msaa_level <= 1) {
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
            } else {
                glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_DEPTH24_STENCIL8, width, height);
            }
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }

        if (!fb.has_depth_buffer && has_depth_buffer) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.rbo);
        } else if (fb.has_depth_buffer && !has_depth_buffer) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        }
    }

    fb.width = width;
    fb.height = height;
    fb.has_depth_buffer = has_depth_buffer;
    fb.msaa_level = msaa_level;
    fb.invertY = opengl_invertY;
    fb.color_format = GfxFramebufferColorFormat::Rgba8;
}

bool GfxRenderingAPIOGL::UpdateFramebufferParametersWithColorFormat(
    int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level, bool opengl_invertY,
    bool render_target, bool has_depth_buffer, bool can_extract_depth,
    GfxFramebufferColorFormat color_format) {
    if (color_format == GfxFramebufferColorFormat::Rgba8) {
        UpdateFramebufferParameters(fb_id, width, height, msaa_level, opengl_invertY, render_target,
                                    has_depth_buffer, can_extract_depth);
        return true;
    }

    if (color_format != GfxFramebufferColorFormat::R32ui || fb_id == 0 || !render_target ||
        msaa_level > 1) {
        return false;
    }

    FramebufferOGL& fb = mFrameBuffers[fb_id];

    width = std::max(width, 1U);
    height = std::max(height, 1U);

    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

    if (fb.width != width || fb.height != height || fb.msaa_level != 1 ||
        fb.color_format != GfxFramebufferColorFormat::R32ui) {
        glBindTexture(GL_TEXTURE_2D, fb.clrbuf);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.clrbuf, 0);
    }

    if (has_depth_buffer && (fb.width != width || fb.height != height || fb.msaa_level != 1 ||
                             !fb.has_depth_buffer)) {
        glBindRenderbuffer(GL_RENDERBUFFER, fb.rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    if (!fb.has_depth_buffer && has_depth_buffer) {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.rbo);
    } else if (fb.has_depth_buffer && !has_depth_buffer) {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
    }

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    fb.width = width;
    fb.height = height;
    fb.has_depth_buffer = has_depth_buffer;
    fb.msaa_level = 1;
    fb.invertY = opengl_invertY;
    fb.color_format = GfxFramebufferColorFormat::R32ui;
    return complete;
}

bool GfxRenderingAPIOGL::SupportsOot3dShadow2dR32uiPipeline() const {
#if defined(USE_OPENGLES)
    return false;
#else
    return true;
#endif
}

bool GfxRenderingAPIOGL::BindOot3dShadow2dTexture(int fb_id, uint32_t texture_unit) {
    if (fb_id <= 0 || fb_id >= static_cast<int>(mFrameBuffers.size()) ||
        texture_unit != kOot3dShadow2dTextureUnit) {
        return false;
    }
    const auto& fb = mFrameBuffers[fb_id];
    if (fb.color_format != GfxFramebufferColorFormat::R32ui || fb.clrbuf == 0) {
        return false;
    }

    if (mLastActiveTexture != static_cast<int8_t>(texture_unit)) {
        mLastActiveTexture = static_cast<int8_t>(texture_unit);
        glActiveTexture(GL_TEXTURE0 + texture_unit);
    }
    glBindTexture(GL_TEXTURE_2D, fb.clrbuf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

bool GfxRenderingAPIOGL::SetOot3dShadow2dShaderParameters(uint32_t texture_bias, bool orthographic, bool invert) {
    if (mCurrentShaderProgram == nullptr ||
        mCurrentShaderProgram->oot3d_shadow2d_texture_bias_location < 0) {
        return false;
    }
    glUniform1i(mCurrentShaderProgram->oot3d_shadow2d_texture_bias_location,
                static_cast<GLint>(texture_bias));
    glUniform1i(mCurrentShaderProgram->oot3d_shadow2d_orthographic_location,
                orthographic ? 1 : 0);
    glUniform1i(mCurrentShaderProgram->oot3d_shadow2d_invert_location,
                invert ? 1 : 0);
    return true;
}

bool GfxRenderingAPIOGL::SupportsOot3dPicaFogLut() const {
    return true;
}

bool GfxRenderingAPIOGL::SetOot3dPicaFogShaderParameters(const uint32_t* lut_words,
                                                         size_t lut_word_count, bool flip,
                                                         uint64_t state_key) {
    if (mCurrentShaderProgram == nullptr || lut_words == nullptr ||
        lut_word_count != kOot3dPicaFogLutEntryCount ||
        mCurrentShaderProgram->oot3d_pica_fog_lut_location < 0 ||
        mCurrentShaderProgram->oot3d_pica_fog_flip_location < 0) {
        return false;
    }
    if (!mCurrentShaderProgram->oot3d_pica_fog_state_key_valid ||
        mCurrentShaderProgram->oot3d_pica_fog_state_key != state_key) {
        glUniform1uiv(mCurrentShaderProgram->oot3d_pica_fog_lut_location,
                     static_cast<GLsizei>(lut_word_count),
                     reinterpret_cast<const GLuint*>(lut_words));
        glUniform1i(mCurrentShaderProgram->oot3d_pica_fog_flip_location, flip ? 1 : 0);
        mCurrentShaderProgram->oot3d_pica_fog_state_key = state_key;
        mCurrentShaderProgram->oot3d_pica_fog_state_key_valid = true;
    }
    return true;
}

bool GfxRenderingAPIOGL::SetOot3dPicaAlphaTestShaderParameters(bool enabled,
                                                               uint32_t compare_func,
                                                               uint8_t reference) {
    if (mCurrentShaderProgram == nullptr ||
        mCurrentShaderProgram->oot3d_pica_alpha_test_enabled_location < 0 ||
        mCurrentShaderProgram->oot3d_pica_alpha_test_func_location < 0 ||
        mCurrentShaderProgram->oot3d_pica_alpha_test_ref_location < 0 ||
        compare_func > 7) {
        return false;
    }
    glUniform1i(mCurrentShaderProgram->oot3d_pica_alpha_test_enabled_location,
                enabled ? 1 : 0);
    glUniform1i(mCurrentShaderProgram->oot3d_pica_alpha_test_func_location,
                static_cast<GLint>(compare_func));
    glUniform1i(mCurrentShaderProgram->oot3d_pica_alpha_test_ref_location,
                static_cast<GLint>(reference));
    return true;
}

bool GfxRenderingAPIOGL::SupportsOot3dPicaTexture2() const {
    return true;
}

bool GfxRenderingAPIOGL::EnsureOot3dShadow2dDepthEncodeProgram() {
#if defined(USE_OPENGLES)
    return false;
#else
    if (mOot3dShadow2dDepthEncodeProgram != 0) {
        return true;
    }

    static constexpr const char* kVertexShaderSource = R"(
#version 130
attribute vec4 aVtxPos;
void main() {
    gl_Position = aVtxPos;
}
)";
    static constexpr const char* kFragmentShaderSource = R"(
#version 130
out uint vOutShadow;
void main() {
    uint depth24 = uint(clamp(gl_FragCoord.z, 0.0, 1.0) * 16777215.0);
    vOutShadow = (depth24 << 8) | 255u;
}
)";

    const auto compileShader = [](GLenum shaderType, const char* source) {
        GLuint shader = glCreateShader(shaderType);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint success = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (success != GL_TRUE) {
            GLint maxLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
            char errorLog[1024];
            glGetShaderInfoLog(shader, std::min<GLint>(maxLength, static_cast<GLint>(sizeof(errorLog))),
                               &maxLength, errorLog);
            fprintf(stderr, "OOT3D Shadow2D shader compilation failed\n%s\n", errorLog);
            glDeleteShader(shader);
            return static_cast<GLuint>(0);
        }
        return shader;
    };

    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    if (vertexShader == 0) {
        return false;
    }
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        return false;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success != GL_TRUE) {
        GLint maxLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
        char errorLog[1024];
        glGetProgramInfoLog(program, std::min<GLint>(maxLength, static_cast<GLint>(sizeof(errorLog))),
                            &maxLength, errorLog);
        fprintf(stderr, "OOT3D Shadow2D depth encode program link failed\n%s\n", errorLog);
        glDeleteProgram(program);
        return false;
    }

    mOot3dShadow2dDepthEncodeProgram = program;
    mOot3dShadow2dDepthEncodePositionLocation = glGetAttribLocation(program, "aVtxPos");
    return mOot3dShadow2dDepthEncodePositionLocation >= 0;
#endif
}

bool GfxRenderingAPIOGL::StartOot3dShadow2dDepthEncodePass(int fb_id, uint32_t clear_value) {
    if (!SupportsOot3dShadow2dR32uiPipeline() || fb_id <= 0 ||
        fb_id >= static_cast<int>(mFrameBuffers.size()) ||
        !EnsureOot3dShadow2dDepthEncodeProgram()) {
        return false;
    }
    const auto& fb = mFrameBuffers[fb_id];
    if (fb.color_format != GfxFramebufferColorFormat::R32ui || fb.width == 0 || fb.height == 0) {
        return false;
    }

    if (mLastLoadedShader != nullptr) {
        UnloadShader(mLastLoadedShader);
    }
    glUseProgram(mOot3dShadow2dDepthEncodeProgram);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
    mCurrentFrameBuffer = fb_id;
    glViewport(0, 0, fb.width, fb.height);
    if (mLastScissorEnabled != 0) {
        mLastScissorEnabled = 0;
        glDisable(GL_SCISSOR_TEST);
    }
    glDisable(GL_BLEND);
    mLastBlendEnabled = 0;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    mLastDepthTest = 1;
    mLastDepthMask = 1;
    mLastZmodeDecal = 0;
    glDisable(GL_POLYGON_OFFSET_FILL);

    const GLuint clearColor[4] = { clear_value, 0, 0, 0 };
    glClearBufferuiv(GL_COLOR, 0, clearColor);
    glClear(GL_DEPTH_BUFFER_BIT);
    return true;
}

void GfxRenderingAPIOGL::EndOot3dShadow2dDepthEncodePass() {
    glBindFramebuffer(GL_FRAMEBUFFER, mFrameBuffers[mCurrentFrameBuffer].fbo);
}

bool GfxRenderingAPIOGL::DrawOot3dShadow2dDepthEncodedTriangles(float buf_vbo[], size_t buf_vbo_len,
                                                                size_t buf_vbo_num_tris) {
    if (mOot3dShadow2dDepthEncodeProgram == 0 ||
        mOot3dShadow2dDepthEncodePositionLocation < 0 ||
        buf_vbo_len != buf_vbo_num_tris * 3 * 4) {
        return false;
    }
    if (mCurrentArrayBuffer != mOpenglVbo) {
        glBindBuffer(GL_ARRAY_BUFFER, mOpenglVbo);
        mCurrentArrayBuffer = mOpenglVbo;
    }
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
    glEnableVertexAttribArray(mOot3dShadow2dDepthEncodePositionLocation);
    glVertexAttribPointer(mOot3dShadow2dDepthEncodePositionLocation, 4, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), reinterpret_cast<void*>(0));
    glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);
    glDisableVertexAttribArray(mOot3dShadow2dDepthEncodePositionLocation);
    return true;
}

void GfxRenderingAPIOGL::StartDrawToFramebuffer(int fb_id, float noise_scale) {
    FramebufferOGL& fb = mFrameBuffers[fb_id];

    if (noise_scale != 0.0f) {
        mCurrentNoiseScale = 1.0f / noise_scale;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
    mCurrentFrameBuffer = fb_id;
}

void GfxRenderingAPIOGL::ClearFramebuffer(bool color, bool depth) {
    if (mLastScissorEnabled != 0) {
        mLastScissorEnabled = 0;
        glDisable(GL_SCISSOR_TEST);
    }
    glDepthMask(GL_TRUE);
    glClearColor(mClearColor[0], mClearColor[1], mClearColor[2], mClearColor[3]);
    glClear((color ? GL_COLOR_BUFFER_BIT : 0) | (depth ? GL_DEPTH_BUFFER_BIT : 0));
    glDepthMask(mCurrentDepthMask ? GL_TRUE : GL_FALSE);
    if (mLastScissorEnabled != 1) {
        mLastScissorEnabled = 1;
        glEnable(GL_SCISSOR_TEST);
    }
}

void GfxRenderingAPIOGL::ClearDepthRegion(int x, int y, int w, int h) {
    // Save current scissor state so callers don't need to manually invalidate.
    GLint prevScissor[4];
    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, prevScissor);

    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDepthMask(mCurrentDepthMask ? GL_TRUE : GL_FALSE);

    // Restore previous scissor state.
    glScissor(prevScissor[0], prevScissor[1], prevScissor[2], prevScissor[3]);
    if (!scissorWasEnabled) {
        glDisable(GL_SCISSOR_TEST);
    }
}

void GfxRenderingAPIOGL::ResolveMSAAColorBuffer(int fb_id_target, int fb_id_source) {
    FramebufferOGL& fb_dst = mFrameBuffers[fb_id_target];
    FramebufferOGL& fb_src = mFrameBuffers[fb_id_source];
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_dst.fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_src.fbo);

    // Disabled for blit
    if (mLastScissorEnabled != 0) {
        mLastScissorEnabled = 0;
        glDisable(GL_SCISSOR_TEST);
    }

    glBlitFramebuffer(0, 0, fb_src.width, fb_src.height, 0, 0, fb_dst.width, fb_dst.height, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, mCurrentFrameBuffer);

    if (mLastScissorEnabled != 1) {
        mLastScissorEnabled = 1;
        glEnable(GL_SCISSOR_TEST);
    }
}

void* GfxRenderingAPIOGL::GetFramebufferTextureId(int fb_id) {
    return (void*)(uintptr_t)mFrameBuffers[fb_id].clrbuf;
}

void GfxRenderingAPIOGL::SelectTextureFb(int fb_id) {
    // glDisable(GL_DEPTH_TEST);
    int tile = 0;
    GLuint texId = mFrameBuffers[fb_id].clrbuf;
    // Ensure the textures metadata vector can hold this FB texture handle.
    // FB color buffers are created outside NewTexture(), so the vector may
    // not have been resized for them yet.
    if (texId >= textures.size()) {
        textures.resize((size_t)texId + 1);
    }
    SelectTexture(tile, texId);
}

void GfxRenderingAPIOGL::CopyFramebuffer(int fb_dst_id, int fb_src_id, int srcX0, int srcY0, int srcX1, int srcY1,
                                         int dstX0, int dstY0, int dstX1, int dstY1) {
    if (fb_dst_id >= (int)mFrameBuffers.size() || fb_src_id >= (int)mFrameBuffers.size()) {
        return;
    }

    FramebufferOGL src = mFrameBuffers[fb_src_id];
    const FramebufferOGL& dst = mFrameBuffers[fb_dst_id];

    // Adjust y values for non-inverted source frame buffers because opengl uses bottom left for origin
    if (!src.invertY) {
        int temp = srcY1 - srcY0;
        srcY1 = src.height - srcY0;
        srcY0 = srcY1 - temp;
    }

    // Flip the y values
    if (src.invertY != dst.invertY) {
        std::swap(srcY0, srcY1);
    }

    // Disabled for blit
    if (mLastScissorEnabled != 0) {
        mLastScissorEnabled = 0;
        glDisable(GL_SCISSOR_TEST);
    }

    // For msaa enabled buffers we can't perform a scaled blit to a simple sample buffer
    // First do an unscaled blit to a msaa resolved buffer
    if (src.height != dst.height && src.width != dst.width && src.msaa_level > 1) {
        // Start with the main buffer (0) as the msaa resolved buffer
        int fb_resolve_id = 0;
        FramebufferOGL fb_resolve = mFrameBuffers[fb_resolve_id];

        // If the size doesn't match our source, then we need to use our separate color msaa resolved buffer (2)
        if (fb_resolve.height != src.height || fb_resolve.width != src.width) {
            fb_resolve_id = 2;
            fb_resolve = mFrameBuffers[fb_resolve_id];
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, src.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_resolve.fbo);

        glBlitFramebuffer(0, 0, src.width, src.height, 0, 0, src.width, src.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // Switch source buffer to the resolved sample
        fb_src_id = fb_resolve_id;
        src = fb_resolve;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, src.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo);

    // The 0 buffer is a double buffer so we need to choose the back to avoid imgui elements
    if (fb_src_id == 0) {
        glReadBuffer(GL_BACK);
    } else {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, mFrameBuffers[mCurrentFrameBuffer].fbo);

    glReadBuffer(GL_BACK);

    if (mLastScissorEnabled != 1) {
        mLastScissorEnabled = 1;
        glEnable(GL_SCISSOR_TEST);
    }
}

void GfxRenderingAPIOGL::ReadFramebufferToCPU(int fb_id, uint32_t width, uint32_t height, uint16_t* rgba16_buf) {
    if (fb_id >= (int)mFrameBuffers.size()) {
        return;
    }

    // Read as RGBA8 (GL_UNSIGNED_BYTE) then convert to RGBA16 (5551).
    // GL_RGBA + GL_UNSIGNED_SHORT_5_5_5_1 writes 4 separate u16 components per pixel
    // (8 bytes) on some drivers (NVIDIA), not the packed 2 bytes the spec implies.
    // Reading as RGBA8 and converting matches the DX11 path's approach.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, mFrameBuffers[fb_id].fbo);
    glReadBuffer(fb_id == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);

    std::vector<uint8_t> rgba8(width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba8.data());

    for (uint32_t y = 0; y < height; y++) {
        const uint32_t srcY = height - 1u - y;
        for (uint32_t x = 0; x < width; x++) {
            const size_t srcIndex = (static_cast<size_t>(srcY) * width + x) * 4u;
            const size_t dstIndex = static_cast<size_t>(y) * width + x;
            uint8_t r = (rgba8[srcIndex + 0] >> 3) & 0x1F;
            uint8_t g = (rgba8[srcIndex + 1] >> 3) & 0x1F;
            uint8_t b = (rgba8[srcIndex + 2] >> 3) & 0x1F;
            uint8_t a = rgba8[srcIndex + 3] ? 1 : 0;
            rgba16_buf[dstIndex] = (r << 11) | (g << 6) | (b << 1) | a;
        }
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, mFrameBuffers[mCurrentFrameBuffer].fbo);
    glReadBuffer(mCurrentFrameBuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
}

std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
GfxRenderingAPIOGL::GetPixelDepth(int fb_id, const std::set<std::pair<float, float>>& coordinates) {
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> res;

    FramebufferOGL& fb = mFrameBuffers[fb_id];

    // When looking up one value and the framebuffer is single-sampled, we can read pixels directly
    // Otherwise we need to blit first to a new buffer then read it
    if (coordinates.size() == 1 && fb.msaa_level <= 1) {
        uint32_t depth_stencil_value;
        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
        int x = coordinates.begin()->first;
        int y = coordinates.begin()->second;
#ifndef USE_OPENGLES // not supported on gles. Runs fine without it, but this may cause issues
        glReadPixels(x, fb.invertY ? fb.height - y : y, 1, 1, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8,
                     &depth_stencil_value);
#endif
        res.emplace(*coordinates.begin(), (depth_stencil_value >> 18) << 2);
    } else {
        if (mPixelDepthRbSize < coordinates.size()) {
            // Resizing a renderbuffer seems broken with Intel's driver, so recreate one instead.
            glBindFramebuffer(GL_FRAMEBUFFER, mPixelDepthFb);
            glDeleteRenderbuffers(1, &mPixelDepthRb);
            glGenRenderbuffers(1, &mPixelDepthRb);
            glBindRenderbuffer(GL_RENDERBUFFER, mPixelDepthRb);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, coordinates.size(), 1);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mPixelDepthRb);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);

            mPixelDepthRbSize = coordinates.size();
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mPixelDepthFb);

        glDisable(GL_SCISSOR_TEST); // needed for the blit operation

        {
            size_t i = 0;
            for (const auto& coord : coordinates) {
                int x = coord.first;
                int y = coord.second;
                if (fb.invertY) {
                    y = fb.height - y;
                }
                glBlitFramebuffer(x, y, x + 1, y + 1, i, 0, i + 1, 1, GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
                                  GL_NEAREST);
                ++i;
            }
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, mPixelDepthFb);
        std::vector<uint32_t> depth_stencil_values(coordinates.size());
#ifndef USE_OPENGLES // not supported on gles. Runs fine without it, but this may cause issues
        glReadPixels(0, 0, coordinates.size(), 1, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, depth_stencil_values.data());
#endif
        {
            size_t i = 0;
            for (const auto& coord : coordinates) {
                res.emplace(coord, (depth_stencil_values[i++] >> 18) << 2);
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, mCurrentFrameBuffer);

    return res;
}

void GfxRenderingAPIOGL::SetTextureFilter(FilteringMode mode) {
    gfx_texture_cache_clear();
    mCurrentFilterMode = mode;
}

FilteringMode GfxRenderingAPIOGL::GetTextureFilter() {
    return mCurrentFilterMode;
}

void GfxRenderingAPIOGL::SetSrgbMode() {
    mSrgbMode = true;
}

ImTextureID GfxRenderingAPIOGL::GetTextureById(int id) {
    return reinterpret_cast<ImTextureID>(id);
}
} // namespace Fast
#endif

#pragma clang diagnostic pop
