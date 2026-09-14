#pragma once
#include <cstdint>
#include <string>

namespace Oot3dNativeGame::TevExpressions {
inline constexpr const char* ByteRoundHelpers =
    "vec3 byteround3(vec3 value) { precise vec3 scaled=value*255.0; precise vec3 biased=scaled+0.5; return floor(biased)/255.0; }\n"
    "float byteround1(float value) { precise float scaled=value*255.0; precise float biased=scaled+0.5; return floor(biased)/255.0; }\n";
inline std::string ColorModifier(const std::string& source, uint8_t modifier,
                          bool& supported) {
    switch (modifier) {
    case 0x0:
        return source + ".rgb";
    case 0x1:
        return "(vec3(1.0) - " + source + ".rgb)";
    case 0x2:
        return source + ".aaa";
    case 0x3:
        return "(vec3(1.0) - " + source + ".aaa)";
    case 0x4:
        return source + ".rrr";
    case 0x5:
        return "(vec3(1.0) - " + source + ".rrr)";
    case 0x8:
        return source + ".ggg";
    case 0x9:
        return "(vec3(1.0) - " + source + ".ggg)";
    case 0xC:
        return source + ".bbb";
    case 0xD:
        return "(vec3(1.0) - " + source + ".bbb)";
    default:
        supported = false;
        return "vec3(0.0)";
    }
}

inline std::string AlphaModifier(const std::string& source, uint8_t modifier,
                          bool& supported) {
    switch (modifier) {
    case 0:
        return source + ".a";
    case 1:
        return "(1.0 - " + source + ".a)";
    case 2:
        return source + ".r";
    case 3:
        return "(1.0 - " + source + ".r)";
    case 4:
        return source + ".g";
    case 5:
        return "(1.0 - " + source + ".g)";
    case 6:
        return source + ".b";
    case 7:
        return "(1.0 - " + source + ".b)";
    default:
        supported = false;
        return "0.0";
    }
}

inline std::string ColorOperation(uint8_t operation, const std::string& a,
                           const std::string& b, const std::string& c,
                           bool& supported) {
    switch (operation) {
    case 0:
        return a;
    case 1:
        return a + " * " + b;
    case 2:
        return a + " + " + b;
    case 3:
        return a + " + " + b + " - vec3(0.5)";
    case 4:
        return "mix(" + b + ", " + a + ", " + c + ")";
    case 5:
        return a + " - " + b;
    case 6:
    case 7:
        return "vec3(dot(" + a + " - vec3(0.5), " + b +
               " - vec3(0.5)) * 4.0)";
    case 8:
        return "fma(" + a + ", " + b + ", " + c + ")";
    case 9:
        return "min(" + a + " + " + b + ", vec3(1.0)) * " + c;
    default:
        supported = false;
        return "vec3(0.0)";
    }
}

inline std::string AlphaOperation(uint8_t operation, const std::string& a,
                           const std::string& b, const std::string& c,
                           bool& supported) {
    switch (operation) {
    case 0:
        return a;
    case 1:
        return a + " * " + b;
    case 2:
        return a + " + " + b;
    case 3:
        return a + " + " + b + " - 0.5";
    case 4:
        return "mix(" + b + ", " + a + ", " + c + ")";
    case 5:
        return a + " - " + b;
    case 8:
        return "fma(" + a + ", " + b + ", " + c + ")";
    case 9:
        return "min(" + a + " + " + b + ", 1.0) * " + c;
    default:
        supported = false;
        return "0.0";
    }
}
} // namespace Oot3dNativeGame::TevExpressions
