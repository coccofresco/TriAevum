// Y2R conversion and IPC semantics adapted from Citra/Azahar:
// core/hw/y2r.cpp and core/hle/service/cam/y2r_u.cpp.
// Copyright 2014-2015 Citra Emulator Project. GPL-2.0-or-later.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace CtrServices {

// Portable service state. No title addresses, rendering backend, or GPU handles.
struct Y2rState {
    uint32_t Input = 0, Output = 0, Rotation = 0, Tiled = 0;
    uint32_t Width = 1024, Height = 1024, Alpha = 0;
    uint32_t SpatialDither = 0, TemporalDither = 0, Interrupt = 0;
    std::array<int16_t, 8> Coefficients{};
    std::array<uint16_t, 16> DitherWeights{};
    // Y, U, V, YUYV, output: address, byte count, DMA unit, inter-unit gap.
    std::array<std::array<uint32_t, 4>, 5> Buffers{};
};

inline constexpr std::array<std::array<int16_t, 8>, 4> Y2rCoefficients{{
    {0x100,0x166,0xB6,0x58,0x1C5,-0x166F,0x10EE,-0x1C5B},
    {0x100,0x193,0x77,0x2F,0x1DB,-0x1933,0xA7C,-0x1D51},
    {0x12A,0x198,0xD0,0x64,0x204,-0x1BDE,0x10F2,-0x229B},
    {0x12A,0x1CA,0x88,0x36,0x21C,-0x1F04,0x99C,-0x2421}
}};

template<class Memory>
bool ConvertY2r(const Y2rState& s, Memory& memory) {
    if (!s.Width || s.Width > 1024 || s.Width % 8 || !s.Height || s.Height > 1024 ||
        s.Input > 4 || s.Output > 3 || s.Rotation > 3 || s.Tiled > 1 ||
        ((s.Tiled || s.Rotation) && s.Height % 8) ||
        ((s.Input == 1 || s.Input == 3) && s.Height % 2)) return false;
    const auto read = [&](size_t plane, size_t bytes, std::vector<uint8_t>& out) {
        const auto& b = s.Buffers[plane];
        if (!b[2] || bytes > b[1] || bytes % b[2]) return false;
        out.resize(bytes);
        for (size_t offset = 0; offset < bytes; offset += b[2]) {
            const uint64_t address = uint64_t(b[0]) + (offset / b[2]) * (uint64_t(b[2]) + b[3]);
            if (address + b[2] > uint64_t(UINT32_MAX) + 1 ||
                !memory.ReadBytes(static_cast<uint32_t>(address),
                    std::span<uint8_t>(out).subspan(offset, b[2]))) return false;
        }
        return true;
    };
    const size_t pixels = s.Width * s.Height;
    const size_t step = s.Input == 2 || s.Input == 3 ? 2 : 1;
    const bool halfHeight = s.Input == 1 || s.Input == 3;
    std::vector<uint8_t> y, u, v;
    if (s.Input == 4) {
        if (!read(3, pixels * 2, y)) return false;
    } else if (!read(0, pixels * step, y) ||
               !read(1, pixels * step / (halfHeight ? 4 : 2), u) ||
               !read(2, pixels * step / (halfHeight ? 4 : 2), v)) return false;
    const size_t bpp = s.Output == 0 ? 4 : (s.Output == 1 ? 3 : 2);
    std::vector<uint8_t> output(pixels * bpp);
    const auto morton = [](unsigned x, unsigned y) {
        unsigned result = 0;
        for (unsigned bit = 0; bit < 3; ++bit)
            result |= ((x >> bit) & 1) << (bit * 2) | ((y >> bit) & 1) << (bit * 2 + 1);
        return result;
    };
    for (unsigned row = 0; row < s.Height; ++row) {
        for (unsigned x = 0; x < s.Width; ++x) {
            const size_t index = row * s.Width + x;
            int Y, U, V;
            if (s.Input == 4) {
                Y = y[index * 2]; U = y[(index & ~size_t(1)) * 2 + 1];
                V = y[(index & ~size_t(1)) * 2 + 3];
            } else {
                const size_t uv = ((row / (halfHeight ? 2 : 1)) * s.Width + x) / 2;
                Y = y[index * step]; U = u[uv * step]; V = v[uv * step];
            }
            const auto& c = s.Coefficients;
            const int cy = c[0] * Y;
            const auto channel = [](int value, int offset) {
                return static_cast<uint8_t>(std::clamp(((value >> 3) + offset + 0x18) >> 5, 0, 255));
            };
            const uint8_t r = channel(cy + c[1] * V, c[5]);
            const uint8_t g = channel(cy - c[2] * V - c[3] * U, c[6]);
            const uint8_t b = channel(cy + c[4] * U, c[7]);
            // Hardware rotates each eight-line strip, not the whole image.
            const unsigned h = std::min(8U, s.Height - (row & ~7U));
            unsigned dx = x, dy = row & 7U, width = s.Width;
            switch (s.Rotation) {
            case 1: dx = h - 1 - (row & 7U); dy = x; width = 8; break;
            case 2: dx = s.Width - 1 - x; dy = h - 1 - (row & 7U); break;
            case 3: dx = row & 7U; dy = s.Width - 1 - x; width = 8; break;
            }
            const size_t local = s.Tiled ? ((dy / 8) * (width / 8) + dx / 8) * 64 +
                morton(dx & 7, dy & 7) : dy * width + dx;
            const size_t at = ((row / 8) * s.Width * 8 + local) * bpp;
            if (s.Output == 0) {
                output[at] = static_cast<uint8_t>(s.Alpha); output[at+1] = b;
                output[at+2] = g; output[at+3] = r;
            } else if (s.Output == 1) {
                output[at] = b; output[at+1] = g; output[at+2] = r;
            } else {
                const uint16_t color = s.Output == 2
                    ? (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | ((s.Alpha & 255) >> 7)
                    : (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
                output[at] = static_cast<uint8_t>(color); output[at+1] = color >> 8;
            }
        }
    }
    const auto& dst = s.Buffers[4];
    if (!dst[2] || output.size() > dst[1] || output.size() % dst[2]) return false;
    // Validate the complete DMA destination before modifying guest memory.
    for (size_t offset = 0; offset < output.size(); offset += dst[2]) {
        const uint64_t addr = uint64_t(dst[0]) + offset / dst[2] * (uint64_t(dst[2]) + dst[3]);
        if (addr + dst[2] > uint64_t(UINT32_MAX) + 1 ||
            !memory.IsWritable(static_cast<uint32_t>(addr), dst[2])) return false;
    }
    for (size_t offset = 0; offset < output.size(); offset += dst[2]) {
        const uint32_t addr = static_cast<uint32_t>(uint64_t(dst[0]) +
            offset / dst[2] * (uint64_t(dst[2]) + dst[3]));
        if (!memory.WriteBytes(addr, std::span<const uint8_t>(output).subspan(offset, dst[2]))) return false;
    }
    return true;
}

struct Y2rReply {
    bool Handled = true, Signal = false, Clear = false, EventHandle = false;
    std::vector<uint32_t> Words{0}; // Result followed by IPC normal parameters.
};

template<class Memory>
Y2rReply DispatchY2r(Y2rState& s, uint16_t command,
                    std::span<const uint32_t> args, Memory& memory) {
    Y2rReply reply;
    constexpr uint32_t invalid = 0xE0E053FDU;
    const auto fail = [&] { reply.Words = {invalid}; return reply; };
    const auto set = [&](uint32_t& field, uint32_t max) {
        if (args.empty() || args[0] > max) reply.Words[0] = invalid;
        else field = args[0];
    };
    switch (command) {
    case 1: set(s.Input, 4); break;
    case 2: reply.Words.push_back(s.Input); break;
    case 3: set(s.Output, 3); break;
    case 4: reply.Words.push_back(s.Output); break;
    case 5: set(s.Rotation, 3); break;
    case 6: reply.Words.push_back(s.Rotation); break;
    case 7: set(s.Tiled, 1); break;
    case 8: reply.Words.push_back(s.Tiled); break;
    case 9: set(s.SpatialDither, 1); break;
    case 10: reply.Words.push_back(s.SpatialDither); break;
    case 11: set(s.TemporalDither, 1); break;
    case 12: reply.Words.push_back(s.TemporalDither); break;
    case 13: set(s.Interrupt, 1); break;
    case 14: reply.Words.push_back(s.Interrupt); break;
    case 15: reply.EventHandle = true; break;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x18: {
        if (args.size() < 4) return fail();
        auto& buffer = s.Buffers[command == 0x18 ? 4 : command - 0x10];
        std::copy_n(args.begin(), 4, buffer.begin());
        buffer[2] &= 0xFFFF; buffer[3] &= 0xFFFF;
        break;
    }
    case 0x14: case 0x15: case 0x16: case 0x17: case 0x19:
        reply.Words.push_back(1); break;
    case 0x1A:
        if (args.empty() || !args[0] || args[0] > 1024 || args[0] % 8) return fail();
        s.Width = args[0]; break;
    case 0x1B: reply.Words.push_back(s.Width); break;
    case 0x1C:
        if (args.empty() || !args[0] || args[0] > 1024) return fail();
        if (args[0] != 1024) s.Height = args[0];
        break;
    case 0x1D: reply.Words.push_back(s.Height); break;
    case 0x1E:
        if (args.size() < 4) return fail();
        for (size_t i = 0; i < 8; ++i) s.Coefficients[i] = static_cast<int16_t>(args[i/2] >> (16*(i%2)));
        break;
    case 0x1F: case 0x21: {
        if (command == 0x21 && (args.empty() || args[0] >= 4)) return fail();
        const auto& c = command == 0x21 ? Y2rCoefficients[args[0]] : s.Coefficients;
        for (size_t i = 0; i < 8; i += 2)
            reply.Words.push_back(uint16_t(c[i]) | uint32_t(uint16_t(c[i+1])) << 16);
        break;
    }
    case 0x20:
        if (args.empty() || args[0] >= 4) return fail();
        s.Coefficients = Y2rCoefficients[args[0]]; break;
    case 0x22: set(s.Alpha, 65535); break;
    case 0x23: reply.Words.push_back(s.Alpha); break;
    case 0x24:
        if (args.size() < 8) return fail();
        for (size_t i = 0; i < 16; ++i) s.DitherWeights[i] = static_cast<uint16_t>(args[i/2] >> (16*(i%2)));
        break;
    case 0x25:
        for (size_t i = 0; i < 16; i += 2)
            reply.Words.push_back(s.DitherWeights[i] | uint32_t(s.DitherWeights[i+1]) << 16);
        break;
    case 0x26:
        if (!ConvertY2r(s, memory)) return fail();
        reply.Signal = true; break;
    case 0x27: break; // Conversion completes synchronously before replying.
    case 0x28: reply.Words.push_back(0); break;
    case 0x29:
        if (args.size() < 3) return fail();
        if ((args[0]&255)>4 || ((args[0]>>8)&255)>3 || ((args[0]>>16)&255)>3 ||
            (args[0]>>24)>1 || !(args[1]&65535) || (args[1]&65535)>1024 ||
            (args[1]&7) || !(args[1]>>16) || (args[1]>>16)>1024 || (args[2]&255)>3) return fail();
        s.Input=args[0]&255; s.Output=(args[0]>>8)&255;
        s.Rotation=(args[0]>>16)&255; s.Tiled=args[0]>>24;
        s.Width=args[1]&65535; if ((args[1]>>16)!=1024) s.Height=args[1]>>16;
        s.Coefficients=Y2rCoefficients[args[2]&255]; s.Alpha=args[2]>>16;
        break;
    case 0x2A: reply.Words.push_back(0); break;
    case 0x2B: {
        // The native driver leaves the interrupt/dithering settings intact.
        s.Input=0; s.Output=0; s.Rotation=0; s.Tiled=0; s.Width=1024;
        s.Coefficients.fill(0); s.Alpha=0; s.Buffers={}; reply.Clear=true;
        break;
    }
    case 0x2C: break;
    default: reply.Handled=false; break;
    }
    return reply;
}
} // namespace CtrServices
