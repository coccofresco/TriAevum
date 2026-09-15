#include "y2r_service.h"
#include <iostream>
#include <stdexcept>

struct Memory {
    std::array<uint8_t, 8192> Bytes{};
    bool IsWritable(uint32_t a, size_t n) const { return a <= Bytes.size() && n <= Bytes.size() - a; }
    bool ReadBytes(uint32_t a, std::span<uint8_t> b) const {
        if (!IsWritable(a, b.size())) return false;
        std::copy_n(Bytes.begin() + a, b.size(), b.begin()); return true;
    }
    bool WriteBytes(uint32_t a, std::span<const uint8_t> b) {
        if (!IsWritable(a, b.size())) return false;
        std::copy(b.begin(), b.end(), Bytes.begin() + a); return true;
    }
};
void Require(bool value) { if (!value) throw std::runtime_error("Y2R regression"); }
int main() {
    try {
        Memory memory;
        CtrServices::Y2rState s;
        s.Width = 16; s.Height = 16; s.Alpha = 255;
        s.Coefficients = {256,0,0,0,0,0,0,0}; // Exact grayscale ramp.
        s.Buffers = {{{0,256,16,0}, {512,128,8,0}, {768,128,8,0}, {}, {2048,1024,64,0}}};
        for (size_t i = 0; i < 256; ++i) memory.Bytes[i] = static_cast<uint8_t>(i);
        Require(CtrServices::ConvertY2r(s, memory));
        for (size_t i = 0; i < 256; ++i) {
            Require(memory.Bytes[2048+i*4] == 255 && memory.Bytes[2048+i*4+3] == i);
        }
        for (unsigned rotation = 0; rotation < 4; ++rotation) {
            for (unsigned tiled = 0; tiled < 2; ++tiled) {
                s.Rotation = rotation; s.Tiled = tiled;
                Require(CtrServices::ConvertY2r(s, memory));
                std::array<unsigned, 256> histogram{};
                for (size_t i = 0; i < 256; ++i) ++histogram[memory.Bytes[2048+i*4+3]];
                for (const auto count : histogram) Require(count == 1);
                const unsigned first = rotation == 0 ? 0 : rotation == 1 ? 112 : rotation == 2 ? 127 : 15;
                Require(memory.Bytes[2051] == first);
            }
        }
        s.Rotation=0; s.Tiled=0;
        for (unsigned format = 0; format < 4; ++format) {
            s.Input = format;
            const unsigned step = format >= 2 ? 2 : 1;
            const unsigned chroma = format % 2 ? 64 : 128;
            s.Buffers[0] = {0,256*step,16*step,0};
            s.Buffers[1] = {512,chroma*step,8*step,0};
            s.Buffers[2] = {1024,chroma*step,8*step,0};
            for (size_t i=0;i<256;++i) memory.Bytes[i*step]=static_cast<uint8_t>(i);
            Require(CtrServices::ConvertY2r(s,memory));
            Require(memory.Bytes[2048+255*4+3]==255);
        }
        s.Input=4; s.Buffers[3]={0,512,32,0};
        for(size_t i=0;i<256;++i) memory.Bytes[i*2]=static_cast<uint8_t>(i);
        for (unsigned output=0; output<4; ++output) {
            s.Output=output;
            const unsigned bpp=output==0?4:output==1?3:2;
            s.Buffers[4]={2048,256*bpp,16*bpp,16};
            memory.Bytes.fill(0xCD);
            for(size_t i=0;i<256;++i) memory.Bytes[i*2]=static_cast<uint8_t>(i);
            Require(CtrServices::ConvertY2r(s,memory));
            Require(memory.Bytes[2048+16*bpp]==0xCD); // Output DMA gap untouched.
        }
        const auto before=memory.Bytes;
        s.Buffers[4][0]=8190;
        Require(!CtrServices::ConvertY2r(s,memory) && before==memory.Bytes);
        const auto reply=CtrServices::DispatchY2r(s,0x26,{},memory);
        Require(reply.Handled && !reply.Signal && reply.Words[0]!=0);
        const std::array<uint32_t,1> width{7};
        Require(CtrServices::DispatchY2r(s,0x1A,width,memory).Words[0]!=0);
        std::cout << "y2r_service_tests: ok\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
