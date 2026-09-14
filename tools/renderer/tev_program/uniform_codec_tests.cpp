#include "oot3d_native_pica_fragment_uniform_codec.h"
#include "fast/oot3d/pica_uniform_layout.h"
#include <bit>
#include <iostream>
#include <stdexcept>
#include <vector>

struct Writer {
    std::vector<uint8_t> Bytes;
    void U32(uint32_t v) { for(unsigned i=0;i<4;++i) Bytes.push_back(uint8_t(v>>(8*i))); }
    void I32(int32_t v) { U32(std::bit_cast<uint32_t>(v)); }
    void Float(float v) { U32(std::bit_cast<uint32_t>(v)); }
};
struct Reader {
    std::span<const uint8_t> Bytes;
    size_t Offset=0;
    bool U32(uint32_t& v) {
        if(Bytes.size()-Offset<4) return false;
        v=0; for(unsigned i=0;i<4;++i) v|=uint32_t(Bytes[Offset++])<<(8*i);
        return true;
    }
    bool I32(int32_t& v) { uint32_t bits; if(!U32(bits))return false; v=std::bit_cast<int32_t>(bits); return true; }
    bool Float(float& v) { uint32_t bits; if(!U32(bits))return false; v=std::bit_cast<float>(bits); return true; }
};
void Check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
int main() {
    using namespace Oot3dNativeGame;
    using namespace FragmentUniformCodec;
    try {
        Oot3dPicaFragmentUniformState input;
        input.TevConstants[3]={0.1F,0.2F,0.3F,0.4F};
        input.Lighting.Position[4]={1,2,3,4};
        input.ShadowBiasLinear=0.125F;
        for(size_t s=0;s<6;++s) input.TevProgram.Stages[s]={uint32_t(s),0x1001,0x40004,0x20002};
        input.TevProgram.Control[0]=0xAB00;
        Writer encoded; WriteFragmentUniforms(encoded,input);
        Reader reader{encoded.Bytes}; Oot3dPicaFragmentUniformState decoded;
        Check(ReadFragmentUniforms(reader,decoded,true,true,true,true),"V10 decode failed");
        Check(reader.Offset==encoded.Bytes.size(),"unconsumed payload");
        Writer roundtrip; WriteFragmentUniforms(roundtrip,decoded);
        Check(roundtrip.Bytes==encoded.Bytes,"non-identical roundtrip");
        auto legacy=encoded.Bytes; legacy.resize(legacy.size()-112);
        Reader old{legacy}; Oot3dPicaFragmentUniformState oldDecoded;
        Check(ReadFragmentUniforms(old,oldDecoded,true,true,true,false),"V9 decode failed");
        Check(old.Offset==legacy.size() && oldDecoded.ShadowBiasLinear==input.ShadowBiasLinear &&
            oldDecoded.TevProgram.Control[0]==0,"V9 prefix corrupted");
        for(size_t length=0;length<encoded.Bytes.size();++length) {
            Reader truncated{std::span(encoded.Bytes).first(length)};
            Oot3dPicaFragmentUniformState partial;
            Check(!ReadFragmentUniforms(truncated,partial,true,true,true,true),"truncated payload accepted");
        }
        static_assert(Fast::Oot3d::kPicaPackedFragmentTevProgramOffset==2112);
        static_assert(Fast::Oot3d::kPicaPackedFragmentUniformSize==2224);
        std::cout<<"V10 uniform codec roundtrip, V9 compatibility, "<<encoded.Bytes.size()<<" truncations passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
