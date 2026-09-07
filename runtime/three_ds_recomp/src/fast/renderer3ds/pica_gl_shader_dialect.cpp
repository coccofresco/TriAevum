#include "fast/renderer3ds/pica_gl_shader_dialect.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace Fast::Renderer3ds {
namespace {

void SetError(std::string* error, std::string_view message) {
    if (error != nullptr) {
        error->assign(message);
    }
}

std::string_view Trim(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1U);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1U);
    }
    return value;
}

std::string CompactLower(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        if (std::isspace(static_cast<unsigned char>(character)) == 0) {
            result.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(character))));
        }
    }
    return result;
}

bool IsIdentifierCharacter(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
           character == '_';
}

bool RewriteLayoutQualifiers(std::string_view source, std::string& output,
                             std::string* error) {
    size_t cursor = 0U;
    while (cursor < source.size()) {
        const size_t layout = source.find("layout", cursor);
        if (layout == std::string_view::npos) {
            output.append(source.substr(cursor));
            return true;
        }
        const bool beginsToken =
            layout == 0U || !IsIdentifierCharacter(source[layout - 1U]);
        const size_t afterKeyword = layout + 6U;
        const bool endsToken =
            afterKeyword == source.size() ||
            !IsIdentifierCharacter(source[afterKeyword]);
        if (!beginsToken || !endsToken) {
            output.append(source.substr(cursor, afterKeyword - cursor));
            cursor = afterKeyword;
            continue;
        }

        size_t open = afterKeyword;
        while (open < source.size() &&
               std::isspace(static_cast<unsigned char>(source[open])) != 0) {
            ++open;
        }
        if (open == source.size() || source[open] != '(') {
            SetError(error, "GLSL layout keyword has no qualifier list");
            return false;
        }
        const size_t close = source.find(')', open + 1U);
        if (close == std::string_view::npos) {
            SetError(error, "GLSL layout qualifier list is unterminated");
            return false;
        }

        std::vector<std::string_view> retained;
        size_t qualifierCursor = open + 1U;
        while (qualifierCursor <= close) {
            const size_t comma = source.find(',', qualifierCursor);
            const size_t qualifierEnd =
                comma == std::string_view::npos || comma > close
                    ? close
                    : comma;
            const std::string_view qualifier =
                Trim(source.substr(qualifierCursor,
                                   qualifierEnd - qualifierCursor));
            const std::string compact = CompactLower(qualifier);
            if (compact == "push_constant") {
                SetError(error,
                         "Vulkan push constants are not part of the canonical OpenGL path");
                return false;
            }
            if (compact.starts_with("set=")) {
                if (compact != "set=0") {
                    SetError(error,
                             "OpenGL PICA shaders require descriptor set zero");
                    return false;
                }
            } else if (!qualifier.empty()) {
                retained.push_back(qualifier);
            }
            if (qualifierEnd == close) {
                break;
            }
            qualifierCursor = qualifierEnd + 1U;
        }

        output.append(source.substr(cursor, layout - cursor));
        if (!retained.empty()) {
            output.append("layout(");
            for (size_t index = 0U; index < retained.size(); ++index) {
                if (index != 0U) {
                    output.append(", ");
                }
                output.append(retained[index]);
            }
            output.push_back(')');
        }
        cursor = close + 1U;
    }
    return true;
}

} // namespace

bool TranslatePicaShaderToOpenGl43(
    std::string_view canonicalSource, std::string& translatedSource,
    std::string* error) {
    translatedSource.clear();
    if (error != nullptr) {
        error->clear();
    }

    const size_t firstLineEnd = canonicalSource.find('\n');
    const std::string_view firstLine = Trim(canonicalSource.substr(
        0U, firstLineEnd == std::string_view::npos
                ? canonicalSource.size()
                : firstLineEnd));
    if (firstLine != "#version 450") {
        SetError(error, "canonical PICA shader is not GLSL 4.5");
        return false;
    }
    if (canonicalSource.find("uniform texture") != std::string_view::npos ||
        canonicalSource.find("uniform sampler ") != std::string_view::npos) {
        SetError(error,
                 "separate Vulkan texture/sampler declarations are unsupported");
        return false;
    }

    std::string versionAdjusted;
    versionAdjusted.reserve(canonicalSource.size() + 32U);
    versionAdjusted.append("#version 430");
    if (firstLineEnd != std::string_view::npos) {
        versionAdjusted.append(canonicalSource.substr(firstLineEnd));
    }

    if (!RewriteLayoutQualifiers(versionAdjusted, translatedSource, error)) {
        translatedSource.clear();
        return false;
    }

    // Vulkan and OpenGL use the same [0, 1] window-depth range but different
    // clip-space Z ranges. Apply z_gl = 2*z_vk-w in the canonical vertex
    // epilogue so gl_FragCoord.z and the existing PICA depth expression remain
    // identical without requiring GL_ARB_clip_control.
    constexpr std::string_view canonicalPosition =
        "gl_Position = vec4(pica_position.x, pica_position.y, "
        "-pica_position.z, pica_position.w);";
    constexpr std::string_view openGlPosition =
        "gl_Position = vec4(pica_position.x, pica_position.y, "
        "-2.0 * pica_position.z - pica_position.w, pica_position.w);";
    const size_t position = translatedSource.find(canonicalPosition);
    if (position != std::string::npos) {
        translatedSource.replace(position, canonicalPosition.size(),
                                 openGlPosition);
    }
    return true;
}

} // namespace Fast::Renderer3ds
