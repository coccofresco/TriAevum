#ifdef ENABLE_OPENGL

#include "fast/backends/gfx_opengl.h"

#include "fast/oot3d/pica_geometry_registry.h"
#include "fast/oot3d/pica_shadow2d.h"
#include "fast/renderer3ds/pica_composition_schedule.h"
#include "fast/renderer3ds/pica_gl_shader_dialect.h"
#include "oot3d/renderer/pica_texture_decode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fast {
namespace {

void SetPicaGlError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

#if !defined(USE_OPENGLES) && !defined(__APPLE__)

uint64_t HashBytes(std::span<const uint8_t> bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (const uint8_t value : bytes) {
        hash = (hash ^ value) * 1099511628211ULL;
    }
    return hash == 0U ? 1U : hash;
}

struct PicaGlTargetKey {
    uint64_t Namespace = 0U;
    uint32_t ColorAddress = 0U;
    uint32_t DepthAddress = 0U;
    uint16_t Width = 0U;
    uint16_t Height = 0U;
    uint8_t ColorFormat = 0U;
    uint8_t DepthFormat = 0U;

    bool operator<(const PicaGlTargetKey& other) const noexcept {
        return std::tie(Namespace, ColorAddress, DepthAddress, Width, Height,
                        ColorFormat, DepthFormat) <
               std::tie(other.Namespace, other.ColorAddress,
                        other.DepthAddress, other.Width, other.Height,
                        other.ColorFormat, other.DepthFormat);
    }
};

struct PicaGlShaderKey {
    Renderer3ds::PicaShaderSourceIdentity Vertex;
    Renderer3ds::PicaShaderSourceIdentity Fragment;

    bool operator<(const PicaGlShaderKey& other) const noexcept {
        return std::tie(Vertex.Id, Vertex.SecondaryHash, Vertex.Size,
                        Fragment.Id, Fragment.SecondaryHash, Fragment.Size) <
               std::tie(other.Vertex.Id, other.Vertex.SecondaryHash,
                        other.Vertex.Size, other.Fragment.Id,
                        other.Fragment.SecondaryHash, other.Fragment.Size);
    }
};

struct PicaGlTextureKey {
    uint64_t ContentHash = 0U;
    uint32_t PhysicalAddress = 0U;
    uint16_t Width = 0U;
    uint16_t Height = 0U;
    uint8_t Format = 0U;
    uint8_t Type = 0U;
    uint8_t WrapS = 0U;
    uint8_t WrapT = 0U;
    bool MinLinear = false;
    bool MagLinear = false;
    bool MipLinear = false;
    int16_t LodBiasRaw = 0;
    uint8_t MinMipLevel = 0U;
    uint8_t MaxMipLevel = 0U;

    bool operator<(const PicaGlTextureKey& other) const noexcept {
        return std::tie(ContentHash, PhysicalAddress, Width, Height, Format,
                        Type, WrapS, WrapT, MinLinear, MagLinear, MipLinear,
                        LodBiasRaw, MinMipLevel, MaxMipLevel) <
               std::tie(other.ContentHash, other.PhysicalAddress,
                        other.Width, other.Height, other.Format, other.Type,
                        other.WrapS, other.WrapT, other.MinLinear,
                        other.MagLinear, other.MipLinear, other.LodBiasRaw,
                        other.MinMipLevel, other.MaxMipLevel);
    }
};

struct PicaGlProgram {
    GLuint Handle = 0U;
};

struct PicaGlTarget {
    GLuint Framebuffer = 0U;
    GLuint Color = 0U;
    GLuint DepthStencil = 0U;
    GLuint ShadowFramebuffer = 0U;
    GLuint Shadow = 0U;
};

struct PicaGlTexture {
    GLuint Handle = 0U;
    GLenum Format = GL_RGBA8;
};

struct PicaGlDisplay {
    GLuint Framebuffer = 0U;
    GLuint Color = 0U;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    bool Initialized = false;
};

struct PicaGlAttributeState {
    GLuint Buffer = 0U;
    GLsizei ByteStride = 0;
    uintptr_t ByteOffset = 0U;
    GLenum Type = GL_FLOAT;
    uint8_t ComponentCount = 0U;
    bool Configured = false;
};

struct PicaGlGeometryBuffer {
    GLuint Handle = 0U;
    uint64_t ContentVersion = 0U;
    uint64_t StructuralSignature = 0U;
};

GLuint CompilePicaGlShader(GLenum stage, std::string_view source) {
    const GLuint shader = glCreateShader(stage);
    const GLchar* text = source.data();
    const GLint length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }
    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<size_t>(std::max(logLength, 1)), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error(
        std::string(stage == GL_VERTEX_SHADER ? "vertex" : "fragment") +
        " PICA OpenGL shader failed: " + log);
}

PicaGlProgram BuildPicaGlProgram(const GfxNativePicaDrawView& draw) {
    std::string vertexSource;
    std::string fragmentSource;
    std::string translateError;
    if (!Renderer3ds::TranslatePicaShaderToOpenGl43(
            draw.VertexShaderSource, vertexSource, &translateError)) {
        throw std::runtime_error(
            "PICA vertex shader translation failed: " + translateError);
    }
    if (!Renderer3ds::TranslatePicaShaderToOpenGl43(
            draw.FragmentShaderSource, fragmentSource, &translateError)) {
        throw std::runtime_error(
            "PICA fragment shader translation failed: " + translateError);
    }

    const GLuint vertex = CompilePicaGlShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = 0U;
    GLuint program = 0U;
    try {
        fragment = CompilePicaGlShader(GL_FRAGMENT_SHADER, fragmentSource);
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            std::string log(
                static_cast<size_t>(std::max(logLength, 1)), '\0');
            glGetProgramInfoLog(program, logLength, nullptr, log.data());
            throw std::runtime_error(
                "PICA OpenGL program link failed: " + log);
        }
        for (GLint slot = 0; slot < 3; ++slot) {
            const std::string name = "pica_texture" + std::to_string(slot);
            const GLint location = glGetUniformLocation(program, name.c_str());
            if (location >= 0) {
                glUseProgram(program);
                glUniform1i(location, slot + 1);
            }
        }
        const GLint lighting =
            glGetUniformLocation(program, "pica_lighting_lut");
        if (lighting >= 0) {
            glUseProgram(program);
            glUniform1i(lighting, 13);
        }
    } catch (...) {
        if (program != 0U) {
            glDeleteProgram(program);
        }
        glDeleteShader(fragment);
        glDeleteShader(vertex);
        throw;
    }
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    return {program};
}

GLenum ToPicaGlCompare(GfxNativePicaCompareFunction compare) {
    switch (compare) {
        case GfxNativePicaCompareFunction::Never: return GL_NEVER;
        case GfxNativePicaCompareFunction::Always: return GL_ALWAYS;
        case GfxNativePicaCompareFunction::Equal: return GL_EQUAL;
        case GfxNativePicaCompareFunction::NotEqual: return GL_NOTEQUAL;
        case GfxNativePicaCompareFunction::Less: return GL_LESS;
        case GfxNativePicaCompareFunction::LessOrEqual: return GL_LEQUAL;
        case GfxNativePicaCompareFunction::Greater: return GL_GREATER;
        case GfxNativePicaCompareFunction::GreaterOrEqual: return GL_GEQUAL;
    }
    throw std::runtime_error("invalid PICA compare function");
}

GLenum ToPicaGlStencil(GfxNativePicaStencilAction action) {
    switch (action) {
        case GfxNativePicaStencilAction::Keep: return GL_KEEP;
        case GfxNativePicaStencilAction::Zero: return GL_ZERO;
        case GfxNativePicaStencilAction::Replace: return GL_REPLACE;
        case GfxNativePicaStencilAction::Increment: return GL_INCR;
        case GfxNativePicaStencilAction::Decrement: return GL_DECR;
        case GfxNativePicaStencilAction::Invert: return GL_INVERT;
        case GfxNativePicaStencilAction::IncrementWrap: return GL_INCR_WRAP;
        case GfxNativePicaStencilAction::DecrementWrap: return GL_DECR_WRAP;
    }
    throw std::runtime_error("invalid PICA stencil action");
}

GLenum ToPicaGlLogic(GfxNativePicaLogicOperation operation) {
    switch (operation) {
        case GfxNativePicaLogicOperation::Clear: return GL_CLEAR;
        case GfxNativePicaLogicOperation::And: return GL_AND;
        case GfxNativePicaLogicOperation::AndReverse: return GL_AND_REVERSE;
        case GfxNativePicaLogicOperation::Copy: return GL_COPY;
        case GfxNativePicaLogicOperation::Set: return GL_SET;
        case GfxNativePicaLogicOperation::CopyInverted: return GL_COPY_INVERTED;
        case GfxNativePicaLogicOperation::NoOp: return GL_NOOP;
        case GfxNativePicaLogicOperation::Invert: return GL_INVERT;
        case GfxNativePicaLogicOperation::Nand: return GL_NAND;
        case GfxNativePicaLogicOperation::Or: return GL_OR;
        case GfxNativePicaLogicOperation::Nor: return GL_NOR;
        case GfxNativePicaLogicOperation::Xor: return GL_XOR;
        case GfxNativePicaLogicOperation::Equivalent: return GL_EQUIV;
        case GfxNativePicaLogicOperation::AndInverted: return GL_AND_INVERTED;
        case GfxNativePicaLogicOperation::OrReverse: return GL_OR_REVERSE;
        case GfxNativePicaLogicOperation::OrInverted: return GL_OR_INVERTED;
    }
    throw std::runtime_error("invalid PICA logic operation");
}

GLenum ToPicaGlBlendEquation(GfxNativeBlendEquation equation) {
    switch (equation) {
        case GfxNativeBlendEquation::Add: return GL_FUNC_ADD;
        case GfxNativeBlendEquation::Subtract: return GL_FUNC_SUBTRACT;
        case GfxNativeBlendEquation::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case GfxNativeBlendEquation::Min: return GL_MIN;
        case GfxNativeBlendEquation::Max: return GL_MAX;
    }
    throw std::runtime_error("invalid PICA blend equation");
}

GLenum ToPicaGlBlendFactor(GfxNativeBlendFactor factor) {
    switch (factor) {
        case GfxNativeBlendFactor::Zero: return GL_ZERO;
        case GfxNativeBlendFactor::One: return GL_ONE;
        case GfxNativeBlendFactor::SourceColor: return GL_SRC_COLOR;
        case GfxNativeBlendFactor::OneMinusSourceColor: return GL_ONE_MINUS_SRC_COLOR;
        case GfxNativeBlendFactor::DestColor: return GL_DST_COLOR;
        case GfxNativeBlendFactor::OneMinusDestColor: return GL_ONE_MINUS_DST_COLOR;
        case GfxNativeBlendFactor::SourceAlpha: return GL_SRC_ALPHA;
        case GfxNativeBlendFactor::OneMinusSourceAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case GfxNativeBlendFactor::DestAlpha: return GL_DST_ALPHA;
        case GfxNativeBlendFactor::OneMinusDestAlpha: return GL_ONE_MINUS_DST_ALPHA;
        case GfxNativeBlendFactor::ConstantColor: return GL_CONSTANT_COLOR;
        case GfxNativeBlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
        case GfxNativeBlendFactor::ConstantAlpha: return GL_CONSTANT_ALPHA;
        case GfxNativeBlendFactor::OneMinusConstantAlpha: return GL_ONE_MINUS_CONSTANT_ALPHA;
        case GfxNativeBlendFactor::SourceAlphaSaturate: return GL_SRC_ALPHA_SATURATE;
    }
    throw std::runtime_error("invalid PICA blend factor");
}

GLenum ToPicaGlTopology(GfxNativePicaTopology topology) {
    switch (topology) {
        case GfxNativePicaTopology::TriangleList:
        case GfxNativePicaTopology::GeometryShader:
            return GL_TRIANGLES;
        case GfxNativePicaTopology::TriangleStrip:
            return GL_TRIANGLE_STRIP;
        case GfxNativePicaTopology::TriangleFan:
            return GL_TRIANGLE_FAN;
    }
    throw std::runtime_error("invalid PICA topology");
}

GLenum ToPicaGlVertexType(GfxNativePicaVertexFormat format) {
    switch (format) {
        case GfxNativePicaVertexFormat::SignedByte: return GL_BYTE;
        case GfxNativePicaVertexFormat::UnsignedByte: return GL_UNSIGNED_BYTE;
        case GfxNativePicaVertexFormat::SignedShort: return GL_SHORT;
        case GfxNativePicaVertexFormat::Float: return GL_FLOAT;
    }
    throw std::runtime_error("invalid PICA vertex format");
}

GLenum ToPicaGlWrap(uint8_t wrap) {
    switch (wrap) {
        case 0U: return GL_CLAMP_TO_EDGE;
        case 2U:
        case 6U:
        case 7U: return GL_REPEAT;
        case 3U: return GL_MIRRORED_REPEAT;
        case 1U:
            throw std::runtime_error(
                "PICA clamp-to-border requires border color state");
        case 4U:
        case 5U:
            throw std::runtime_error("PICA asymmetric wrap is unsupported");
        default:
            throw std::runtime_error("invalid PICA texture wrap mode");
    }
}

uint32_t PicaMipCount(const GfxNativePicaTextureView& texture) {
    if (texture.Width == 0U || texture.Height == 0U) {
        return 0U;
    }
    uint32_t width = texture.Width;
    uint32_t height = texture.Height;
    uint32_t levels = 1U;
    while (width > 8U && height > 8U) {
        ++levels;
        width >>= 1U;
        height >>= 1U;
    }
    return std::min(
        levels, static_cast<uint32_t>(texture.MaxMipLevel) + 1U);
}

std::optional<size_t> PicaEncodedMipSize(
    uint8_t format, uint32_t width, uint32_t height) {
    static constexpr std::array<uint8_t, 14> kNibblesPerPixel{
        8U, 6U, 4U, 4U, 4U, 4U, 4U,
        2U, 2U, 2U, 1U, 1U, 1U, 2U};
    if (format >= kNibblesPerPixel.size() || width == 0U || height == 0U) {
        return std::nullopt;
    }
    const uint64_t tiledWidth = (std::max(8U, width) + 7U) & ~7ULL;
    const uint64_t tiledHeight = (std::max(8U, height) + 7U) & ~7ULL;
    const uint64_t bytes =
        (tiledWidth * tiledHeight * kNibblesPerPixel[format] + 1U) / 2U;
    if (bytes > std::numeric_limits<size_t>::max()) {
        return std::nullopt;
    }
    return static_cast<size_t>(bytes);
}

GLint PicaGlMinFilter(const GfxNativePicaTextureView& texture,
                      uint32_t mipLevels) {
    if (mipLevels <= 1U) {
        return texture.MinLinear ? GL_LINEAR : GL_NEAREST;
    }
    if (texture.MipLinear) {
        return texture.MinLinear ? GL_LINEAR_MIPMAP_LINEAR
                                 : GL_NEAREST_MIPMAP_LINEAR;
    }
    return texture.MinLinear ? GL_LINEAR_MIPMAP_NEAREST
                             : GL_NEAREST_MIPMAP_NEAREST;
}

uint32_t PicaColorBytesPerPixel(uint8_t format) {
    return format == 0U ? 4U : (format == 1U ? 3U : 2U);
}

uint32_t PicaDepthBytesPerPixel(uint8_t format) {
    return format == 0U ? 2U : (format == 2U ? 3U : 4U);
}

uint32_t PicaFillElementSize(uint16_t control) {
    return (control & (1U << 9U)) != 0U
        ? 4U
        : ((control & (1U << 8U)) != 0U ? 3U : 2U);
}

uint8_t PicaFillByte(const GfxNativePicaMemoryFillView& fill,
                     uint32_t index) {
    const uint32_t shift =
        (index % PicaFillElementSize(fill.Control)) * 8U;
    return static_cast<uint8_t>(fill.Value >> shift);
}

float PicaUnorm(uint32_t value, uint32_t maximum) {
    return static_cast<float>(value) / static_cast<float>(maximum);
}

std::array<float, 4> DecodePicaClearColor(
    const GfxNativePicaMemoryFillView& fill, uint8_t format) {
    const uint8_t b0 = PicaFillByte(fill, 0U);
    const uint8_t b1 = PicaFillByte(fill, 1U);
    const uint8_t b2 = PicaFillByte(fill, 2U);
    const uint8_t b3 = PicaFillByte(fill, 3U);
    if (format == 0U) {
        return {PicaUnorm(b3, 0xFFU), PicaUnorm(b2, 0xFFU),
                PicaUnorm(b1, 0xFFU), PicaUnorm(b0, 0xFFU)};
    }
    if (format == 1U) {
        return {PicaUnorm(b2, 0xFFU), PicaUnorm(b1, 0xFFU),
                PicaUnorm(b0, 0xFFU), 1.0F};
    }
    const uint16_t packed = static_cast<uint16_t>(b0) |
                            static_cast<uint16_t>(b1) << 8U;
    if (format == 2U) {
        return {PicaUnorm((packed >> 11U) & 0x1FU, 0x1FU),
                PicaUnorm((packed >> 5U) & 0x3FU, 0x3FU),
                PicaUnorm(packed & 0x1FU, 0x1FU), 1.0F};
    }
    if (format == 3U) {
        return {PicaUnorm((packed >> 11U) & 0x1FU, 0x1FU),
                PicaUnorm((packed >> 6U) & 0x1FU, 0x1FU),
                PicaUnorm((packed >> 1U) & 0x1FU, 0x1FU),
                static_cast<float>(packed & 1U)};
    }
    return {PicaUnorm((packed >> 12U) & 0xFU, 0xFU),
            PicaUnorm((packed >> 8U) & 0xFU, 0xFU),
            PicaUnorm((packed >> 4U) & 0xFU, 0xFU),
            PicaUnorm(packed & 0xFU, 0xFU)};
}

std::pair<float, uint8_t> DecodePicaClearDepthStencil(
    const GfxNativePicaMemoryFillView& fill, uint8_t format) {
    const uint32_t b0 = PicaFillByte(fill, 0U);
    const uint32_t b1 = PicaFillByte(fill, 1U);
    const uint32_t b2 = PicaFillByte(fill, 2U);
    const uint32_t depth = format == 0U
        ? b0 | (b1 << 8U)
        : b0 | (b1 << 8U) | (b2 << 16U);
    return {PicaUnorm(depth, format == 0U ? 0xFFFFU : 0xFFFFFFU),
            format == 3U ? PicaFillByte(fill, 3U) : 0U};
}

void ClearPicaShadow(const PicaGlTarget& target, uint32_t value) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.ShadowFramebuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glClearBufferuiv(GL_COLOR, 0, &value);
}

void DeletePicaTarget(PicaGlTarget& target) {
    glDeleteFramebuffers(1, &target.Framebuffer);
    glDeleteFramebuffers(1, &target.ShadowFramebuffer);
    glDeleteTextures(1, &target.Color);
    glDeleteTextures(1, &target.Shadow);
    glDeleteRenderbuffers(1, &target.DepthStencil);
    target = {};
}

void DeletePicaDisplay(PicaGlDisplay& display) {
    glDeleteFramebuffers(1, &display.Framebuffer);
    glDeleteTextures(1, &display.Color);
    display = {};
}

#endif

} // namespace

struct GfxRenderingAPIOGL::NativePicaState {
    std::vector<uint64_t> Completions;
#if !defined(USE_OPENGLES) && !defined(__APPLE__)
    Renderer3ds::PicaCompositionSchedule Composition;
    std::map<PicaGlShaderKey, PicaGlProgram> Programs;
    std::map<PicaGlTargetKey, PicaGlTarget> Targets;
    std::map<PicaGlTextureKey, PicaGlTexture> Textures;
    std::map<uint64_t, PicaGlTexture> LightingLuts;
    std::map<std::pair<uint64_t, uint32_t>, PicaGlDisplay> Displays;
    std::vector<GfxNativePicaMemoryFillView> PendingFills;
    std::optional<GfxNativePicaDisplayTransferView> LastPresented;
    Oot3d::PicaGeometryRegistry GeometryRegistry;
    std::unordered_map<uint64_t, PicaGlGeometryBuffer> GeometryBuffers;
    std::vector<Oot3d::PicaNriSourceVertexStream> GeometrySourceStreams;
    std::vector<Oot3d::PicaNriSourceVertexAttribute> GeometrySourceAttributes;
    std::array<GLuint, 16> VertexBuffers{};
    GLuint VertexArray = 0U;
    GLuint IndexBuffer = 0U;
    GLuint VertexUniformBuffer = 0U;
    GLuint FragmentUniformBuffer = 0U;
    GLuint FallbackColor = 0U;
    GLuint FallbackInteger = 0U;
    GLuint ScanoutProgram = 0U;
    GLint ScanoutFlipLocation = -1;
    uint16_t EnabledAttributeMask = 0U;
    std::array<uint8_t, 16> AttributeDivisors{};
    std::array<PicaGlAttributeState, 16> AttributeStates{};
    bool GeometryCacheEnabled = true;
    uint64_t GeometryPersistentDraws = 0U;
    uint64_t GeometryStreamingDraws = 0U;
    uint64_t GeometryPersistentUploads = 0U;
    uint64_t GeometryPersistentUploadBytes = 0U;
    uint64_t GeometryStreamingUploads = 0U;
    uint64_t GeometryStreamingUploadBytes = 0U;
    uint64_t UniformUploads = 0U;
    uint64_t UniformUploadBytes = 0U;
#endif
};

GfxRenderingAPIOGL::GfxRenderingAPIOGL() = default;

GfxRenderingAPIOGL::~GfxRenderingAPIOGL() {
    ShutdownNativePica();
}

void GfxRenderingAPIOGL::InitNativePica() {
    ShutdownNativePica();
    mNativePica = std::make_unique<NativePicaState>();
#if !defined(USE_OPENGLES) && !defined(__APPLE__)
    auto& state = *mNativePica;
    state.GeometryCacheEnabled = mNativePicaGeometryCacheEnabled;
    glGenVertexArrays(1, &state.VertexArray);
    glGenBuffers(static_cast<GLsizei>(state.VertexBuffers.size()),
                 state.VertexBuffers.data());
    glGenBuffers(1, &state.IndexBuffer);
    glGenBuffers(1, &state.VertexUniformBuffer);
    glGenBuffers(1, &state.FragmentUniformBuffer);

    const std::array<uint8_t, 4> white{255U, 255U, 255U, 255U};
    glGenTextures(1, &state.FallbackColor);
    glBindTexture(GL_TEXTURE_2D, state.FallbackColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, white.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    const uint32_t emptyShadow = 0xFFFFFFFFU;
    glGenTextures(1, &state.FallbackInteger);
    glBindTexture(GL_TEXTURE_2D, state.FallbackInteger);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 1, 1, 0, GL_RED_INTEGER,
                 GL_UNSIGNED_INT, &emptyShadow);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    static constexpr std::string_view kScanoutVertex = R"glsl(#version 430
out vec2 pica_uv;
void main() {
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 position = positions[gl_VertexID];
    gl_Position = vec4(position, 0.0, 1.0);
    pica_uv = position * 0.5 + 0.5;
}
)glsl";
    static constexpr std::string_view kScanoutFragment = R"glsl(#version 430
layout(binding=0) uniform sampler2D pica_scanout;
uniform int pica_flip_y;
in vec2 pica_uv;
layout(location=0) out vec4 pica_color;
void main() {
    // CTR display-transfer images use the physical portrait layout. OpenGL's
    // framebuffer origin is the opposite of Vulkan's, so compensate that
    // origin before applying the same portrait-to-landscape transform.
    vec2 uv = vec2(pica_uv.y, pica_uv.x);
    if (pica_flip_y != 0) uv.y = 1.0 - uv.y;
    pica_color = texture(pica_scanout, uv);
}
)glsl";
    const GLuint vertex = CompilePicaGlShader(GL_VERTEX_SHADER, kScanoutVertex);
    GLuint fragment = 0U;
    try {
        fragment = CompilePicaGlShader(GL_FRAGMENT_SHADER, kScanoutFragment);
        state.ScanoutProgram = glCreateProgram();
        glAttachShader(state.ScanoutProgram, vertex);
        glAttachShader(state.ScanoutProgram, fragment);
        glLinkProgram(state.ScanoutProgram);
        GLint linked = GL_FALSE;
        glGetProgramiv(state.ScanoutProgram, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            throw std::runtime_error("PICA OpenGL scanout program failed to link");
        }
        glUseProgram(state.ScanoutProgram);
        const GLint sampler =
            glGetUniformLocation(state.ScanoutProgram, "pica_scanout");
        if (sampler >= 0) {
            glUniform1i(sampler, 0);
        }
        state.ScanoutFlipLocation =
            glGetUniformLocation(state.ScanoutProgram, "pica_flip_y");
    } catch (...) {
        glDeleteShader(fragment);
        glDeleteShader(vertex);
        ShutdownNativePica();
        throw;
    }
    glDeleteShader(fragment);
    glDeleteShader(vertex);
#endif
}

void GfxRenderingAPIOGL::ShutdownNativePica() noexcept {
    if (!mNativePica) {
        return;
    }
#if !defined(USE_OPENGLES) && !defined(__APPLE__)
    auto& state = *mNativePica;
    for (auto& [key, program] : state.Programs) {
        glDeleteProgram(program.Handle);
    }
    for (auto& [key, target] : state.Targets) {
        DeletePicaTarget(target);
    }
    for (auto& [key, texture] : state.Textures) {
        glDeleteTextures(1, &texture.Handle);
    }
    for (auto& [key, texture] : state.LightingLuts) {
        glDeleteTextures(1, &texture.Handle);
    }
    for (auto& [key, display] : state.Displays) {
        DeletePicaDisplay(display);
    }
    for (auto& [identity, geometry] : state.GeometryBuffers) {
        glDeleteBuffers(1, &geometry.Handle);
    }
    glDeleteProgram(state.ScanoutProgram);
    glDeleteTextures(1, &state.FallbackColor);
    glDeleteTextures(1, &state.FallbackInteger);
    glDeleteBuffers(static_cast<GLsizei>(state.VertexBuffers.size()),
                    state.VertexBuffers.data());
    glDeleteBuffers(1, &state.IndexBuffer);
    glDeleteBuffers(1, &state.VertexUniformBuffer);
    glDeleteBuffers(1, &state.FragmentUniformBuffer);
    glDeleteVertexArrays(1, &state.VertexArray);
#endif
    mNativePica.reset();
}

void GfxRenderingAPIOGL::InvalidateLegacyStateAfterPica() noexcept {
    mLastLoadedShader = nullptr;
    mCurrentArrayBuffer = 0U;
    mLastActiveTexture = -1;
    mLastBlendEnabled = -1;
    mLastScissorEnabled = -1;
    mLastDepthTest = -1;
    mLastDepthMask = -1;
    mLastZmodeDecal = -1;
    std::fill(std::begin(mLastBoundTextures),
              std::end(mLastBoundTextures), 0U);
}

bool GfxRenderingAPIOGL::PublishPicaCompositionSequence(
    const Renderer3ds::PicaCompositionSequenceView& sequence,
    std::string* error) {
#if defined(USE_OPENGLES) || defined(__APPLE__)
    SetPicaGlError(error, "native PICA requires OpenGL 4.3");
    return false;
#else
    if (!mNativePica) {
        SetPicaGlError(error, "PICA OpenGL backend is not initialized");
        return false;
    }
    if (mNativePica->Composition.Valid()) {
        const auto previous = mNativePica->Composition.Stats();
        if (previous.ConsumedDrawCount != previous.DrawCount) {
            SetPicaGlError(
                error, "previous PICA composition sequence was not consumed");
            return false;
        }
        if (mNativePica->Composition.SequenceId() == sequence.SequenceId) {
            SetPicaGlError(error, "PICA composition identity was reused");
            return false;
        }
    }
    Renderer3ds::PicaCompositionSchedule compiled;
    if (!compiled.Compile(sequence, error)) {
        return false;
    }
    mNativePica->Composition = std::move(compiled);
    return true;
#endif
}

#if !defined(USE_OPENGLES) && !defined(__APPLE__)
namespace {

void DeletePicaGlGeometry(
    GfxRenderingAPIOGL::NativePicaState& state, uint64_t identity) {
    const auto found = state.GeometryBuffers.find(identity);
    if (found == state.GeometryBuffers.end()) {
        return;
    }
    const GLuint handle = found->second.Handle;
    for (auto& attribute : state.AttributeStates) {
        if (attribute.Buffer == handle) {
            attribute = {};
        }
    }
    glDeleteBuffers(1, &found->second.Handle);
    state.GeometryBuffers.erase(found);
}

PicaGlTarget& GetPicaGlTarget(
    GfxRenderingAPIOGL::NativePicaState& state,
    const GfxNativePicaDrawView& draw) {
    const PicaGlTargetKey key{
        draw.RenderTargetNamespace,
        draw.FramebufferColorPhysicalAddress,
        draw.FramebufferDepthPhysicalAddress,
        draw.FramebufferWidth,
        draw.FramebufferHeight,
        draw.FramebufferColorFormat,
        draw.FramebufferDepthFormat,
    };
    auto [found, inserted] = state.Targets.try_emplace(key);
    if (!inserted) {
        return found->second;
    }
    auto& target = found->second;
    try {
        glGenTextures(1, &target.Color);
        glBindTexture(GL_TEXTURE_2D, target.Color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, key.Width, key.Height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glGenRenderbuffers(1, &target.DepthStencil);
        glBindRenderbuffer(GL_RENDERBUFFER, target.DepthStencil);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                              key.Width, key.Height);

        glGenFramebuffers(1, &target.Framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, target.Framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, target.Color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, target.DepthStencil);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("PICA OpenGL render target is incomplete");
        }

        glGenTextures(1, &target.Shadow);
        glBindTexture(GL_TEXTURE_2D, target.Shadow);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, key.Width, key.Height, 0,
                     GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &target.ShadowFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, target.ShadowFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, target.Shadow, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("PICA OpenGL shadow target is incomplete");
        }
        ClearPicaShadow(target, 0xFFFFFFFFU);

        glBindFramebuffer(GL_FRAMEBUFFER, target.Framebuffer);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFU);
        glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
        glClearDepth(1.0);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
    } catch (...) {
        DeletePicaTarget(target);
        state.Targets.erase(found);
        throw;
    }
    return target;
}

PicaGlProgram& GetPicaGlProgram(
    GfxRenderingAPIOGL::NativePicaState& state,
    const GfxNativePicaDrawView& draw) {
    auto vertexIdentity = draw.VertexShaderSourceIdentity;
    auto fragmentIdentity = draw.FragmentShaderSourceIdentity;
    if (!vertexIdentity.Available()) {
        vertexIdentity =
            Renderer3ds::IdentifyPicaShaderSource(draw.VertexShaderSource);
    }
    if (!fragmentIdentity.Available()) {
        fragmentIdentity =
            Renderer3ds::IdentifyPicaShaderSource(draw.FragmentShaderSource);
    }
    const PicaGlShaderKey key{vertexIdentity, fragmentIdentity};
    auto found = state.Programs.find(key);
    if (found != state.Programs.end()) {
        return found->second;
    }

    // The frontend publishes stable source identities with every draw. Hash
    // the complete GLSL text only while creating a new program; repeating
    // that work on every cache hit made shader lookup scale with source size
    // instead of draw count. The cache-miss check retains the rejecting
    // identity contract before any program is compiled.
    if (draw.VertexShaderSourceIdentity.Available() &&
        Renderer3ds::IdentifyPicaShaderSource(draw.VertexShaderSource) !=
            vertexIdentity) {
        throw std::runtime_error("PICA vertex source identity mismatch");
    }
    if (draw.FragmentShaderSourceIdentity.Available() &&
        Renderer3ds::IdentifyPicaShaderSource(draw.FragmentShaderSource) !=
            fragmentIdentity) {
        throw std::runtime_error("PICA fragment source identity mismatch");
    }
    return state.Programs.emplace(key, BuildPicaGlProgram(draw)).first->second;
}

GLuint FindLivePicaShadow(
    const GfxRenderingAPIOGL::NativePicaState& state,
    const GfxNativePicaTextureView& texture) {
    for (const auto& [key, target] : state.Targets) {
        if (key.ColorAddress == texture.PhysicalAddress &&
            key.Width == texture.Width && key.Height == texture.Height) {
            return target.Shadow;
        }
    }
    return 0U;
}

PicaGlTexture& GetPicaGlTexture(
    GfxRenderingAPIOGL::NativePicaState& state,
    const GfxNativePicaTextureView& texture) {
    const uint64_t contentHash = texture.NativeContentHashAvailable
        ? texture.NativeContentHash
        : HashBytes(texture.NativeBytes);
    const PicaGlTextureKey key{
        contentHash, texture.PhysicalAddress, texture.Width, texture.Height,
        texture.NativeFormat, texture.NativeType, texture.NativeWrapS,
        texture.NativeWrapT, texture.MinLinear, texture.MagLinear,
        texture.MipLinear, texture.LodBiasRaw, texture.MinMipLevel,
        texture.MaxMipLevel,
    };
    auto [found, inserted] = state.Textures.try_emplace(key);
    if (!inserted) {
        return found->second;
    }
    auto& result = found->second;
    try {
        glGenTextures(1, &result.Handle);
        glBindTexture(GL_TEXTURE_2D, result.Handle);
        if (texture.NativeType == 2U) {
            std::vector<uint32_t> pixels;
            std::string decodeError;
            if (!Oot3d::DetilePicaShadow2d(
                    texture.NativeBytes, texture.Width, texture.Height,
                    pixels, &decodeError)) {
                throw std::runtime_error(
                    "PICA Shadow2D decode failed: " + decodeError);
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, texture.Width,
                         texture.Height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT,
                         pixels.data());
            result.Format = GL_R32UI;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        } else {
            const uint32_t mipLevels = PicaMipCount(texture);
            if (mipLevels == 0U) {
                throw std::runtime_error("PICA texture has no mip levels");
            }
            size_t nativeOffset = 0U;
            for (uint32_t level = 0U; level < mipLevels; ++level) {
                const uint32_t width =
                    std::max(1U, static_cast<uint32_t>(texture.Width) >> level);
                const uint32_t height =
                    std::max(1U, static_cast<uint32_t>(texture.Height) >> level);
                const auto encoded = PicaEncodedMipSize(
                    texture.NativeFormat, width, height);
                if (!encoded.has_value() ||
                    nativeOffset + *encoded > texture.NativeBytes.size()) {
                    throw std::runtime_error("PICA texture mip chain is truncated");
                }
                std::vector<uint8_t> rgba8;
                std::string decodeError;
                if (!::Oot3d::Renderer::DecodePicaTextureRgba8(
                        texture.NativeFormat, static_cast<uint16_t>(width),
                        static_cast<uint16_t>(height),
                        texture.NativeBytes.subspan(nativeOffset, *encoded),
                        rgba8, &decodeError)) {
                    throw std::runtime_error(
                        "PICA texture decode failed: " + decodeError);
                }
                glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                             GL_RGBA8, static_cast<GLsizei>(width),
                             static_cast<GLsizei>(height), 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, rgba8.data());
                nativeOffset += *encoded;
            }
            result.Format = GL_RGBA8;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            PicaGlMinFilter(texture, mipLevels));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            texture.MagLinear ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL,
                            texture.MinMipLevel);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                            std::min<uint32_t>(texture.MaxMipLevel,
                                               mipLevels - 1U));
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS,
                            static_cast<float>(texture.LodBiasRaw) / 256.0F);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        ToPicaGlWrap(texture.NativeWrapS));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        ToPicaGlWrap(texture.NativeWrapT));
    } catch (...) {
        glDeleteTextures(1, &result.Handle);
        state.Textures.erase(found);
        throw;
    }
    return result;
}

GLuint GetPicaLightingLut(
    GfxRenderingAPIOGL::NativePicaState& state,
    const Renderer3ds::PicaLightingLutView& lut) {
    if (!lut.Valid()) {
        throw std::runtime_error("PICA lighting LUT is invalid");
    }
    auto [found, inserted] = state.LightingLuts.try_emplace(lut.ContentHash);
    if (!inserted) {
        return found->second.Handle;
    }
    auto& texture = found->second;
    glGenTextures(1, &texture.Handle);
    glBindTexture(GL_TEXTURE_2D, texture.Handle);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 256, 24, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_INT, lut.PackedEntries.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    texture.Format = GL_R32UI;
    return texture.Handle;
}

void ApplyPicaFill(const GfxNativePicaMemoryFillView& fill,
                   const PicaGlTargetKey& key,
                   const PicaGlTarget& target) {
    const uint64_t pixels = static_cast<uint64_t>(key.Width) * key.Height;
    const uint64_t colorEnd = static_cast<uint64_t>(key.ColorAddress) +
                              pixels * PicaColorBytesPerPixel(key.ColorFormat);
    const uint64_t depthEnd = static_cast<uint64_t>(key.DepthAddress) +
                              pixels * PicaDepthBytesPerPixel(key.DepthFormat);
    const bool color = fill.StartPhysicalAddress <= key.ColorAddress &&
                       fill.EndPhysicalAddress >= colorEnd;
    const bool depth = key.DepthAddress != 0U &&
                       fill.StartPhysicalAddress <= key.DepthAddress &&
                       fill.EndPhysicalAddress >= depthEnd;
    if (!color && !depth) {
        return;
    }
    if (color) {
        const uint32_t shadow =
            static_cast<uint32_t>(PicaFillByte(fill, 0U)) |
            (static_cast<uint32_t>(PicaFillByte(fill, 1U)) << 8U) |
            (static_cast<uint32_t>(PicaFillByte(fill, 2U)) << 16U) |
            (static_cast<uint32_t>(PicaFillByte(fill, 3U)) << 24U);
        ClearPicaShadow(target, shadow);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, target.Framebuffer);
    glDisable(GL_SCISSOR_TEST);
    GLbitfield mask = 0U;
    if (color) {
        const auto decoded = DecodePicaClearColor(fill, key.ColorFormat);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(decoded[0], decoded[1], decoded[2], decoded[3]);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (depth) {
        const auto [depthValue, stencil] =
            DecodePicaClearDepthStencil(fill, key.DepthFormat);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFU);
        glClearDepth(depthValue);
        glClearStencil(stencil);
        mask |= GL_DEPTH_BUFFER_BIT;
        if (key.DepthFormat == 3U) {
            mask |= GL_STENCIL_BUFFER_BIT;
        }
    }
    glClear(mask);
}

void ApplyPendingPicaFills(GfxRenderingAPIOGL::NativePicaState& state,
                           const PicaGlTargetKey& key,
                           const PicaGlTarget& target) {
    auto fill = state.PendingFills.begin();
    while (fill != state.PendingFills.end()) {
        if (fill->RenderTargetNamespace != key.Namespace) {
            ++fill;
            continue;
        }
        const uint64_t pixels = static_cast<uint64_t>(key.Width) * key.Height;
        const uint64_t colorEnd = static_cast<uint64_t>(key.ColorAddress) +
                                  pixels * PicaColorBytesPerPixel(key.ColorFormat);
        const uint64_t depthEnd = static_cast<uint64_t>(key.DepthAddress) +
                                  pixels * PicaDepthBytesPerPixel(key.DepthFormat);
        const bool covers =
            (fill->StartPhysicalAddress <= key.ColorAddress &&
             fill->EndPhysicalAddress >= colorEnd) ||
            (key.DepthAddress != 0U &&
             fill->StartPhysicalAddress <= key.DepthAddress &&
             fill->EndPhysicalAddress >= depthEnd);
        if (!covers) {
            ++fill;
            continue;
        }
        ApplyPicaFill(*fill, key, target);
        fill = state.PendingFills.erase(fill);
    }
}

PicaGlDisplay& GetPicaGlDisplay(
    GfxRenderingAPIOGL::NativePicaState& state,
    uint64_t targetNamespace, uint32_t outputAddress,
    uint32_t width, uint32_t height) {
    const auto key = std::make_pair(targetNamespace, outputAddress);
    auto [found, inserted] = state.Displays.try_emplace(key);
    auto& display = found->second;
    if (!inserted && display.Width == width && display.Height == height) {
        return display;
    }
    if (!inserted) {
        DeletePicaDisplay(display);
    }
    glGenTextures(1, &display.Color);
    glBindTexture(GL_TEXTURE_2D, display.Color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &display.Framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, display.Framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, display.Color, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        DeletePicaDisplay(display);
        throw std::runtime_error("PICA OpenGL display image is incomplete");
    }
    display.Width = width;
    display.Height = height;
    return display;
}

} // namespace
#endif

GfxNativePicaBackendStats
GfxRenderingAPIOGL::GetNativePicaBackendStats() const noexcept {
    GfxNativePicaBackendStats result;
#if !defined(USE_OPENGLES) && !defined(__APPLE__)
    if (mNativePica != nullptr) {
        result.Available = true;
        const auto registry = mNativePica->GeometryRegistry.Stats();
        result.GeometryCacheEnabled = mNativePica->GeometryCacheEnabled;
        result.GeometryRegistryHits = registry.Hits;
        result.GeometryRegistryMisses = registry.Misses;
        result.GeometryRegistryUpdates = registry.Updates;
        result.GeometryRegistryEvictions = registry.Evictions;
        result.GeometryRegistryEntries = registry.Entries;
        result.GeometryPersistentDraws =
            mNativePica->GeometryPersistentDraws;
        result.GeometryStreamingDraws = mNativePica->GeometryStreamingDraws;
        result.GeometryPersistentUploads =
            mNativePica->GeometryPersistentUploads;
        result.GeometryPersistentUploadBytes =
            mNativePica->GeometryPersistentUploadBytes;
        result.GeometryStreamingUploads =
            mNativePica->GeometryStreamingUploads;
        result.GeometryStreamingUploadBytes =
            mNativePica->GeometryStreamingUploadBytes;
        result.UniformUploads = mNativePica->UniformUploads;
        result.UniformUploadBytes = mNativePica->UniformUploadBytes;
    }
#endif
    return result;
}

void GfxRenderingAPIOGL::SetNativePicaGeometryCacheEnabled(
    bool enabled) noexcept {
    mNativePicaGeometryCacheEnabled = enabled;
#if !defined(USE_OPENGLES) && !defined(__APPLE__)
    if (mNativePica != nullptr) {
        mNativePica->GeometryCacheEnabled = enabled;
    }
#else
    static_cast<void>(enabled);
#endif
}

bool GfxRenderingAPIOGL::SubmitPicaDraw(
    const GfxNativePicaDrawView& draw, std::string* error) {
#if defined(USE_OPENGLES) || defined(__APPLE__)
    SetPicaGlError(error, "native PICA requires OpenGL 4.3");
    return false;
#else
    try {
        if (!mNativePica || draw.VertexShaderSource.empty() ||
            draw.FragmentShaderSource.empty() || draw.VertexCount == 0U ||
            draw.VertexBindings.empty() || draw.VertexAttributes.empty()) {
            throw std::runtime_error("PICA OpenGL draw is incomplete");
        }
        if (draw.FragmentOperationMode != 0U &&
            draw.FragmentOperationMode != 3U) {
            throw std::runtime_error("PICA fragment operation is unsupported");
        }
        if (draw.ScissorMode == 1U) {
            throw std::runtime_error("PICA exclude scissor requires shader emulation");
        }
        if (draw.ScissorMode != 0U && draw.ScissorMode != 3U) {
            throw std::runtime_error("invalid PICA scissor mode");
        }
        if (draw.FramebufferWidth == 0U || draw.FramebufferHeight == 0U) {
            throw std::runtime_error("PICA framebuffer has zero extent");
        }
        if (mNativePica->Composition.Valid() &&
            !mNativePica->Composition.ConsumeDraw(
                Renderer3ds::BuildPicaCompositionDrawReference(draw), error)) {
            return false;
        }

        auto& program = GetPicaGlProgram(*mNativePica, draw);
        auto& target = GetPicaGlTarget(*mNativePica, draw);
        const PicaGlTargetKey targetKey{
            draw.RenderTargetNamespace,
            draw.FramebufferColorPhysicalAddress,
            draw.FramebufferDepthPhysicalAddress,
            draw.FramebufferWidth,
            draw.FramebufferHeight,
            draw.FramebufferColorFormat,
            draw.FramebufferDepthFormat,
        };
        ApplyPendingPicaFills(*mNativePica, targetKey, target);

        glBindFramebuffer(GL_FRAMEBUFFER, target.Framebuffer);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBindVertexArray(mNativePica->VertexArray);
        glUseProgram(program.Handle);

        glBindBuffer(GL_UNIFORM_BUFFER, mNativePica->VertexUniformBuffer);
        glBufferData(GL_UNIFORM_BUFFER,
                     static_cast<GLsizeiptr>(draw.VertexUniformBytes.size()),
                     draw.VertexUniformBytes.data(), GL_STREAM_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0,
                         mNativePica->VertexUniformBuffer);
        glBindBuffer(GL_UNIFORM_BUFFER, mNativePica->FragmentUniformBuffer);
        glBufferData(GL_UNIFORM_BUFFER,
                     static_cast<GLsizeiptr>(draw.FragmentUniformBytes.size()),
                     draw.FragmentUniformBytes.data(), GL_STREAM_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 4,
                         mNativePica->FragmentUniformBuffer);
        mNativePica->UniformUploads += 2U;
        mNativePica->UniformUploadBytes += draw.VertexUniformBytes.size() +
                                           draw.FragmentUniformBytes.size();

        uint16_t nextAttributeMask = 0U;
        for (const auto& binding : draw.VertexBindings) {
            if (binding.Binding >= mNativePica->VertexBuffers.size() ||
                binding.ByteStride == 0U || binding.Bytes.empty() ||
                binding.ByteStride > static_cast<uint32_t>(
                                         std::numeric_limits<GLsizei>::max()) ||
                binding.Bytes.size() > static_cast<size_t>(
                                           std::numeric_limits<GLsizeiptr>::max())) {
                throw std::runtime_error("PICA vertex binding is invalid");
            }
        }
        for (const auto& attribute : draw.VertexAttributes) {
            if (attribute.Location >= 16U || attribute.Binding >= 16U ||
                attribute.ComponentCount == 0U ||
                attribute.ComponentCount > 4U) {
                throw std::runtime_error("PICA vertex attribute is invalid");
            }
            nextAttributeMask |= static_cast<uint16_t>(
                1U << attribute.Location);
        }
        const uint16_t disabledAttributes =
            mNativePica->EnabledAttributeMask & ~nextAttributeMask;
        for (GLuint location = 0U; location < 16U; ++location) {
            if ((disabledAttributes & (1U << location)) != 0U) {
                glDisableVertexAttribArray(location);
            }
        }

        const bool persistentGeometry =
            mNativePica->GeometryCacheEnabled &&
            draw.GeometryIdentityAvailable && draw.GeometryIdentity != 0U &&
            draw.GeometryContentVersion != 0U;
        const Oot3d::PicaPreparedGeometry* preparedGeometry = nullptr;
        GLuint persistentGeometryBuffer = 0U;
        if (persistentGeometry) {
            auto& streams = mNativePica->GeometrySourceStreams;
            streams.clear();
            streams.reserve(draw.VertexBindings.size());
            for (const auto& binding : draw.VertexBindings) {
                streams.push_back({
                    {binding.Binding, binding.ByteStride,
                     binding.PerInstance},
                    binding.Bytes,
                });
            }
            auto& attributes = mNativePica->GeometrySourceAttributes;
            attributes.clear();
            attributes.reserve(draw.VertexAttributes.size());
            for (const auto& attribute : draw.VertexAttributes) {
                attributes.push_back({
                    attribute.Location,
                    attribute.Binding,
                    static_cast<Oot3d::PicaNriVertexScalar>(
                        attribute.Format),
                    attribute.ComponentCount,
                    attribute.ByteOffset,
                });
            }
            const auto resolution = mNativePica->GeometryRegistry.Resolve({
                draw.GeometryIdentity,
                draw.GeometryContentVersion,
                true,
                mFrameCount,
                streams,
                attributes,
                draw.IndexBytes,
                draw.Indexed,
                draw.IndicesAre16Bit,
                draw.VertexCount,
                false,
            });
            if (resolution.Geometry == nullptr) {
                throw std::runtime_error(
                    "PICA OpenGL geometry preparation failed");
            }
            for (const uint64_t evicted :
                 mNativePica->GeometryRegistry.TakeEvictedIdentities()) {
                DeletePicaGlGeometry(*mNativePica, evicted);
            }
            preparedGeometry = resolution.Geometry;
            auto& buffer = mNativePica
                               ->GeometryBuffers[preparedGeometry->Identity];
            if (buffer.Handle == 0U) {
                glGenBuffers(1, &buffer.Handle);
                if (buffer.Handle == 0U) {
                    throw std::runtime_error(
                        "PICA OpenGL geometry buffer allocation failed");
                }
            }
            if (buffer.ContentVersion !=
                    preparedGeometry->ContentVersion ||
                buffer.StructuralSignature !=
                    preparedGeometry->StructuralSignature) {
                if (preparedGeometry->Payload.size() >
                    static_cast<size_t>(
                        std::numeric_limits<GLsizeiptr>::max())) {
                    throw std::runtime_error(
                        "PICA OpenGL geometry payload is too large");
                }
                glBindBuffer(GL_ARRAY_BUFFER, buffer.Handle);
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(
                        preparedGeometry->Payload.size()),
                    preparedGeometry->Payload.data(), GL_STATIC_DRAW);
                buffer.ContentVersion = preparedGeometry->ContentVersion;
                buffer.StructuralSignature =
                    preparedGeometry->StructuralSignature;
                ++mNativePica->GeometryPersistentUploads;
                mNativePica->GeometryPersistentUploadBytes +=
                    preparedGeometry->Payload.size();
            }
            persistentGeometryBuffer = buffer.Handle;
            ++mNativePica->GeometryPersistentDraws;
        } else {
            for (const auto& binding : draw.VertexBindings) {
                const GLuint buffer =
                    mNativePica->VertexBuffers[binding.Binding];
                glBindBuffer(GL_ARRAY_BUFFER, buffer);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(binding.Bytes.size()),
                             binding.Bytes.data(), GL_STREAM_DRAW);
                ++mNativePica->GeometryStreamingUploads;
                mNativePica->GeometryStreamingUploadBytes +=
                    binding.Bytes.size();
            }
            ++mNativePica->GeometryStreamingDraws;
        }
        for (const auto& attribute : draw.VertexAttributes) {
            const auto binding = std::find_if(
                draw.VertexBindings.begin(), draw.VertexBindings.end(),
                [&](const auto& candidate) {
                    return candidate.Binding == attribute.Binding;
                });
            if (binding == draw.VertexBindings.end()) {
                throw std::runtime_error("PICA vertex attribute has no binding");
            }
            const GLuint buffer = persistentGeometry
                                      ? persistentGeometryBuffer
                                      : mNativePica->VertexBuffers[
                                            attribute.Binding];
            const GLenum type = ToPicaGlVertexType(attribute.Format);
            uint64_t byteOffset = attribute.ByteOffset;
            if (persistentGeometry) {
                const auto preparedBinding = std::find_if(
                    preparedGeometry->SourceBindings.begin(),
                    preparedGeometry->SourceBindings.end(),
                    [&](const auto& candidate) {
                        return candidate.Binding == attribute.Binding;
                    });
                if (preparedBinding ==
                    preparedGeometry->SourceBindings.end()) {
                    throw std::runtime_error(
                        "PICA prepared geometry has no vertex binding");
                }
                if (preparedBinding->Offset >
                    std::numeric_limits<uint64_t>::max() - byteOffset) {
                    throw std::runtime_error(
                        "PICA OpenGL vertex offset overflow");
                }
                byteOffset += preparedBinding->Offset;
            }
            if (byteOffset > std::numeric_limits<uintptr_t>::max()) {
                throw std::runtime_error(
                    "PICA OpenGL vertex offset is too large");
            }
            auto& state =
                mNativePica->AttributeStates[attribute.Location];
            if (!state.Configured || state.Buffer != buffer ||
                state.ByteStride != binding->ByteStride ||
                state.ByteOffset != static_cast<uintptr_t>(byteOffset) ||
                state.Type != type ||
                state.ComponentCount != attribute.ComponentCount) {
                glBindBuffer(GL_ARRAY_BUFFER, buffer);
                glVertexAttribPointer(
                    attribute.Location, attribute.ComponentCount, type,
                    GL_FALSE, binding->ByteStride,
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(
                        byteOffset)));
                state.Buffer = buffer;
                state.ByteStride = binding->ByteStride;
                state.ByteOffset = static_cast<uintptr_t>(byteOffset);
                state.Type = type;
                state.ComponentCount = attribute.ComponentCount;
                state.Configured = true;
            }
            const uint16_t attributeBit = static_cast<uint16_t>(
                1U << attribute.Location);
            if ((mNativePica->EnabledAttributeMask & attributeBit) == 0U) {
                glEnableVertexAttribArray(attribute.Location);
            }
            const uint8_t divisor = binding->PerInstance ? 1U : 0U;
            if (mNativePica->AttributeDivisors[attribute.Location] !=
                divisor) {
                glVertexAttribDivisor(attribute.Location, divisor);
                mNativePica->AttributeDivisors[attribute.Location] = divisor;
            }
        }
        mNativePica->EnabledAttributeMask = nextAttributeMask;

        std::array<GLuint, 3> textures{
            mNativePica->FallbackColor,
            mNativePica->FallbackColor,
            mNativePica->FallbackColor,
        };
        for (const auto& texture : draw.Textures) {
            if (texture.Slot >= textures.size()) {
                throw std::runtime_error("PICA texture slot is invalid");
            }
            if (texture.NativeType != 0U &&
                !(texture.Slot == 0U &&
                  (texture.NativeType == 2U || texture.NativeType == 3U))) {
                throw std::runtime_error("PICA texture type is unsupported");
            }
            GLuint handle = texture.NativeType == 2U
                ? FindLivePicaShadow(*mNativePica, texture)
                : 0U;
            if (handle == 0U) {
                handle = GetPicaGlTexture(*mNativePica, texture).Handle;
            }
            textures[texture.Slot] = handle;
        }
        for (size_t slot = 0U; slot < textures.size(); ++slot) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(slot + 1U));
            glBindTexture(GL_TEXTURE_2D, textures[slot]);
        }
        GLuint lighting = mNativePica->FallbackInteger;
        if (!draw.LightingLut.PackedEntries.empty()) {
            lighting = GetPicaLightingLut(*mNativePica, draw.LightingLut);
        }
        glActiveTexture(GL_TEXTURE13);
        glBindTexture(GL_TEXTURE_2D, lighting);

        const bool shadowProducer = draw.FragmentOperationMode == 3U;
        glBindImageTexture(5, target.Shadow, 0, GL_FALSE, 0,
                           GL_READ_WRITE, GL_R32UI);
        if (shadowProducer) {
            glDisable(GL_BLEND);
            glDisable(GL_COLOR_LOGIC_OP);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        } else {
            glColorMask((draw.ColorWriteMask & 1U) != 0U,
                        (draw.ColorWriteMask & 2U) != 0U,
                        (draw.ColorWriteMask & 4U) != 0U,
                        (draw.ColorWriteMask & 8U) != 0U);
            if (draw.Blend.Enabled) {
                glEnable(GL_BLEND);
                glDisable(GL_COLOR_LOGIC_OP);
                glBlendEquationSeparate(
                    ToPicaGlBlendEquation(draw.Blend.EquationRgb),
                    ToPicaGlBlendEquation(draw.Blend.EquationAlpha));
                glBlendFuncSeparate(
                    ToPicaGlBlendFactor(draw.Blend.SourceRgb),
                    ToPicaGlBlendFactor(draw.Blend.DestRgb),
                    ToPicaGlBlendFactor(draw.Blend.SourceAlpha),
                    ToPicaGlBlendFactor(draw.Blend.DestAlpha));
                glBlendColor(draw.Blend.ConstantColor[0],
                             draw.Blend.ConstantColor[1],
                             draw.Blend.ConstantColor[2],
                             draw.Blend.ConstantColor[3]);
            } else {
                glDisable(GL_BLEND);
                glEnable(GL_COLOR_LOGIC_OP);
                glLogicOp(ToPicaGlLogic(draw.LogicOperation));
            }
        }

        if (draw.DepthTestEnabled || draw.DepthWriteEnabled) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(draw.DepthTestEnabled
                ? ToPicaGlCompare(draw.DepthCompare)
                : GL_ALWAYS);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(draw.DepthWriteEnabled ? GL_TRUE : GL_FALSE);
        if (draw.Stencil.Enabled) {
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(ToPicaGlCompare(draw.Stencil.Compare),
                          draw.Stencil.Reference, draw.Stencil.CompareMask);
            glStencilMask(draw.Stencil.WriteMask);
            glStencilOp(ToPicaGlStencil(draw.Stencil.Fail),
                        ToPicaGlStencil(draw.Stencil.DepthFail),
                        ToPicaGlStencil(draw.Stencil.Pass));
        } else {
            glDisable(GL_STENCIL_TEST);
        }
        if (draw.CullMode == GfxNativeCullMode::KeepAll) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            // Vulkan's positive-height viewport reverses window-space winding
            // relative to OpenGL. Keep the PICA semantic winding here instead
            // of copying the Vulkan pipeline's compensating front-face value.
            glFrontFace(draw.CullMode ==
                            GfxNativeCullMode::KeepCounterClockwise
                        ? GL_CCW
                        : GL_CW);
            glCullFace(draw.FramebufferFlipped ? GL_FRONT : GL_BACK);
        }

        glViewport(static_cast<GLint>(draw.ViewportX),
                   static_cast<GLint>(draw.ViewportY),
                   static_cast<GLsizei>(draw.ViewportWidth),
                   static_cast<GLsizei>(draw.ViewportHeight));
        glEnable(GL_SCISSOR_TEST);
        if (draw.ScissorMode == 3U) {
            glScissor(draw.ScissorX1, draw.ScissorY1,
                      draw.ScissorX2 >= draw.ScissorX1
                          ? draw.ScissorX2 - draw.ScissorX1 + 1U
                          : 0U,
                      draw.ScissorY2 >= draw.ScissorY1
                          ? draw.ScissorY2 - draw.ScissorY1 + 1U
                          : 0U);
        } else {
            glScissor(0, 0, draw.FramebufferWidth, draw.FramebufferHeight);
        }

        const GLenum topology = ToPicaGlTopology(draw.Topology);
        if (draw.Indexed) {
            if (draw.IndexBytes.empty()) {
                throw std::runtime_error("PICA indexed draw has no indices");
            }
            const size_t indexSize = draw.IndicesAre16Bit
                                         ? sizeof(uint16_t)
                                         : sizeof(uint8_t);
            if (draw.IndexBytes.size() % indexSize != 0U) {
                throw std::runtime_error(
                    "PICA indexed draw has a partial index");
            }
            if (persistentGeometry) {
                if (!preparedGeometry->Indexed ||
                    preparedGeometry->VertexOrIndexCount >
                        static_cast<uint32_t>(
                            std::numeric_limits<GLsizei>::max()) ||
                    preparedGeometry->IndexOffset >
                        std::numeric_limits<uintptr_t>::max()) {
                    throw std::runtime_error(
                        "PICA OpenGL prepared index data is invalid");
                }
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                             persistentGeometryBuffer);
                glDrawElementsBaseVertex(
                    topology,
                    static_cast<GLsizei>(
                        preparedGeometry->VertexOrIndexCount),
                    GL_UNSIGNED_SHORT,
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(
                        preparedGeometry->IndexOffset)),
                    draw.BaseVertex);
            } else {
                const size_t indexCount = draw.IndexBytes.size() / indexSize;
                if (draw.IndexBytes.size() >
                        static_cast<size_t>(
                            std::numeric_limits<GLsizeiptr>::max()) ||
                    indexCount > static_cast<size_t>(
                                     std::numeric_limits<GLsizei>::max())) {
                    throw std::runtime_error(
                        "PICA OpenGL streaming index data is too large");
                }
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                             mNativePica->IndexBuffer);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(draw.IndexBytes.size()),
                             draw.IndexBytes.data(), GL_STREAM_DRAW);
                ++mNativePica->GeometryStreamingUploads;
                mNativePica->GeometryStreamingUploadBytes +=
                    draw.IndexBytes.size();
                const GLenum type = draw.IndicesAre16Bit
                                        ? GL_UNSIGNED_SHORT
                                        : GL_UNSIGNED_BYTE;
                glDrawElementsBaseVertex(
                    topology, static_cast<GLsizei>(indexCount), type, nullptr,
                    draw.BaseVertex);
            }
        } else {
            if (draw.VertexCount > static_cast<uint32_t>(
                                       std::numeric_limits<GLsizei>::max())) {
                throw std::runtime_error(
                    "PICA non-indexed draw has too many vertices");
            }
            glDrawArrays(topology, 0,
                         static_cast<GLsizei>(draw.VertexCount));
        }
        // Ordinary raster and framebuffer commands are ordered by GL. Only
        // the PICA shadow producer writes through an image binding and needs
        // an explicit visibility barrier before a later shadow consumer.
        if (shadowProducer) {
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                            GL_TEXTURE_FETCH_BARRIER_BIT);
        }
        InvalidateLegacyStateAfterPica();
        return true;
    } catch (const std::exception& exception) {
        InvalidateLegacyStateAfterPica();
        SetPicaGlError(error, exception.what());
        return false;
    }
#endif
}

bool GfxRenderingAPIOGL::SubmitPicaDisplayTransfer(
    const GfxNativePicaDisplayTransferView& transfer,
    std::string* error) {
#if defined(USE_OPENGLES) || defined(__APPLE__)
    SetPicaGlError(error, "native PICA requires OpenGL 4.3");
    return false;
#else
    try {
        if (!mNativePica || transfer.InputPhysicalAddress == 0U ||
            transfer.OutputPhysicalAddress == 0U ||
            transfer.InputWidth == 0U || transfer.InputHeight == 0U ||
            transfer.OutputWidth == 0U || transfer.OutputHeight == 0U) {
            throw std::runtime_error("PICA display transfer is incomplete");
        }
        const uint32_t scaling = (transfer.Flags >> 24U) & 3U;
        if ((transfer.Flags & 8U) != 0U ||
            (transfer.Flags & 0x10000U) != 0U || scaling > 2U) {
            throw std::runtime_error("PICA display transfer mode is unsupported");
        }
        const auto source = std::find_if(
            mNativePica->Targets.begin(), mNativePica->Targets.end(),
            [&](const auto& entry) {
                return entry.first.Namespace == transfer.RenderTargetNamespace &&
                       entry.first.ColorAddress ==
                           transfer.InputPhysicalAddress;
            });
        if (source == mNativePica->Targets.end()) {
            throw std::runtime_error("PICA display transfer source is missing");
        }
        if (transfer.InputWidth > source->first.Width ||
            transfer.InputHeight > source->first.Height) {
            throw std::runtime_error("PICA display transfer exceeds its source");
        }

        const uint32_t outputWidth = std::max<uint32_t>(1U,
            (static_cast<uint64_t>(transfer.OutputWidth) *
                 source->first.Width + transfer.InputWidth / 2U) /
                transfer.InputWidth);
        const uint32_t outputHeight = std::max<uint32_t>(1U,
            (static_cast<uint64_t>(transfer.OutputHeight) *
                 source->first.Height + transfer.InputHeight / 2U) /
                transfer.InputHeight);
        const uint32_t horizontalSamples = scaling != 0U ? 2U : 1U;
        const uint32_t verticalSamples = scaling == 2U ? 2U : 1U;
        if (static_cast<uint64_t>(outputWidth) * horizontalSamples >
                source->first.Width ||
            static_cast<uint64_t>(outputHeight) * verticalSamples >
                source->first.Height) {
            throw std::runtime_error("PICA display transfer samples out of bounds");
        }
        auto& display = GetPicaGlDisplay(
            *mNativePica, transfer.RenderTargetNamespace,
            transfer.OutputPhysicalAddress, outputWidth, outputHeight);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, source->second.Framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, display.Framebuffer);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(
            0, 0, static_cast<GLint>(outputWidth * horizontalSamples),
            static_cast<GLint>(outputHeight * verticalSamples),
            0, 0, static_cast<GLint>(outputWidth),
            static_cast<GLint>(outputHeight), GL_COLOR_BUFFER_BIT,
            scaling == 0U ? GL_NEAREST : GL_LINEAR);
        display.Initialized = true;

        if (transfer.Present) {
            SDL_Window* window = SDL_GL_GetCurrentWindow();
            if (window == nullptr) {
                throw std::runtime_error("PICA OpenGL scanout has no SDL window");
            }
            int windowWidth = 0;
            int windowHeight = 0;
#ifdef __SWITCH__
            SDL_GetWindowSize(window, &windowWidth, &windowHeight);
#else
            SDL_GL_GetDrawableSize(window, &windowWidth, &windowHeight);
#endif
            if (windowWidth <= 0 || windowHeight <= 0) {
                throw std::runtime_error("PICA OpenGL scanout extent is invalid");
            }
            // The transfer image is stored in the CTR's physical portrait
            // layout; presentation uses the transposed logical dimensions.
            const float scale = std::min(
                static_cast<float>(windowWidth) / outputHeight,
                static_cast<float>(windowHeight) / outputWidth);
            const int viewportWidth = std::max(
                1, static_cast<int>(std::lround(outputHeight * scale)));
            const int viewportHeight = std::max(
                1, static_cast<int>(std::lround(outputWidth * scale)));
            const int viewportX = (windowWidth - viewportWidth) / 2;
            const int viewportY = (windowHeight - viewportHeight) / 2;

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_COLOR_LOGIC_OP);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            if (transfer.PresentationMode ==
                GfxNativePicaPresentationMode::AlphaOverlay) {
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            } else {
                glDisable(GL_BLEND);
                glViewport(0, 0, windowWidth, windowHeight);
                glClearColor(mClearColor[0], mClearColor[1],
                             mClearColor[2], mClearColor[3]);
                glClear(GL_COLOR_BUFFER_BIT);
                glViewport(viewportX, viewportY,
                           viewportWidth, viewportHeight);
            }
            glBindVertexArray(mNativePica->VertexArray);
            glUseProgram(mNativePica->ScanoutProgram);
            const bool transferInputLinear =
                (transfer.Flags & (1U << 1U)) != 0U;
            const bool dontSwizzle =
                (transfer.Flags & (1U << 5U)) != 0U;
            const bool flip =
                ((!transferInputLinear !=
                  (transferInputLinear != dontSwizzle)) ^
                 ((transfer.Flags & 1U) != 0U));
            if (mNativePica->ScanoutFlipLocation >= 0) {
                glUniform1i(mNativePica->ScanoutFlipLocation, flip ? 1 : 0);
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, display.Color);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            mNativePica->LastPresented = transfer;
        }
        InvalidateLegacyStateAfterPica();
        return true;
    } catch (const std::exception& exception) {
        InvalidateLegacyStateAfterPica();
        SetPicaGlError(error, exception.what());
        return false;
    }
#endif
}

bool GfxRenderingAPIOGL::ClearPicaRenderTarget(
    uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress,
    std::string* error) {
#if defined(USE_OPENGLES) || defined(__APPLE__)
    SetPicaGlError(error, "native PICA requires OpenGL 4.3");
    return false;
#else
    if (!mNativePica || colorPhysicalAddress == 0U) {
        SetPicaGlError(error, "PICA render-target clear is incomplete");
        return false;
    }
    for (auto& [key, target] : mNativePica->Targets) {
        if (key.Namespace != renderTargetNamespace ||
            key.ColorAddress != colorPhysicalAddress) {
            continue;
        }
        ClearPicaShadow(target, 0xFFFFFFFFU);
        glBindFramebuffer(GL_FRAMEBUFFER, target.Framebuffer);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFU);
        glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
        glClearDepth(1.0);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
    }
    InvalidateLegacyStateAfterPica();
    return true;
#endif
}

bool GfxRenderingAPIOGL::SubmitPicaMemoryFill(
    const GfxNativePicaMemoryFillView& fill, std::string* error) {
#if defined(USE_OPENGLES) || defined(__APPLE__)
    SetPicaGlError(error, "native PICA requires OpenGL 4.3");
    return false;
#else
    if (!mNativePica || fill.StartPhysicalAddress == 0U ||
        fill.EndPhysicalAddress <= fill.StartPhysicalAddress ||
        (fill.Control & 1U) == 0U) {
        SetPicaGlError(error, "PICA memory fill is incomplete");
        return false;
    }
    const auto previous = std::find_if(
        mNativePica->PendingFills.begin(),
        mNativePica->PendingFills.end(),
        [&](const auto& pending) {
            return pending.RenderTargetNamespace == fill.RenderTargetNamespace &&
                   pending.StartPhysicalAddress == fill.StartPhysicalAddress &&
                   pending.EndPhysicalAddress == fill.EndPhysicalAddress;
        });
    if (previous != mNativePica->PendingFills.end()) {
        *previous = fill;
    } else {
        mNativePica->PendingFills.push_back(fill);
    }
    for (const auto& [key, target] : mNativePica->Targets) {
        ApplyPendingPicaFills(*mNativePica, key, target);
    }
    InvalidateLegacyStateAfterPica();
    return true;
#endif
}

bool GfxRenderingAPIOGL::QueuePicaCompletion(
    uint64_t completionId, std::string* error) {
    if (!mNativePica || completionId == 0U) {
        SetPicaGlError(error, "PICA completion identity is invalid");
        return false;
    }
#if !defined(USE_OPENGLES) && !defined(__APPLE__)
    // SubmitPicaDraw has already copied every guest source span into
    // backend-owned GL buffers/textures. A completion acknowledges that copy;
    // it does not need to idle the GPU. GL preserves command ordering through
    // the later display blit and SDL swap.
#endif
    mNativePica->Completions.push_back(completionId);
    return true;
}

std::vector<uint64_t> GfxRenderingAPIOGL::TakePicaCompletions() {
    if (!mNativePica) {
        return {};
    }
    auto result = std::move(mNativePica->Completions);
    mNativePica->Completions.clear();
    return result;
}

bool GfxRenderingAPIOGL::ResetPicaState(std::string* error) {
    try {
        if (!mNativePica) {
            throw std::runtime_error("PICA OpenGL backend is not initialized");
        }
#if !defined(USE_OPENGLES) && !defined(__APPLE__)
        glFinish();
#endif
        InitNativePica();
        InvalidateLegacyStateAfterPica();
        return true;
    } catch (const std::exception& exception) {
        SetPicaGlError(error, exception.what());
        return false;
    }
}

} // namespace Fast

#endif
