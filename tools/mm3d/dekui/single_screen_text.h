// Ported from mm3d-dekui src/mm3d/single_screen_text.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <optional>
#include <atomic>
#include <vector>
#include "single_screen_text_data.h"

namespace Mm3dDekui::SingleScreenText {
inline std::atomic_bool enabled{true};
using U32=std::uint32_t;
constexpr U32 PC=0x007d1498,Word=0xe5903004,SVC=0xfffe90; // ref 0x7d1498 (Restoration source port)
inline U32 Instruction(U32 pc,U32 word){return pc==PC&&word==Word?0xef000000|SVC:word;}
template<class M> bool CodeMatches(M& m) {
    // Restoration's MessageData::Get implementation; retain the outer Odolwa
    // message remap and the original lookup, attributes and language handling.
    constexpr std::array<U32,8> words{Word,0xe92d47f0,0xe593e008,0xe25ee001,
        0x4a000051,0xe1a06000,0xe3a04000,0xe5905008};
    for(unsigned i=0;i<words.size();++i)if(m.Read(PC+i*4)!=words[i])return false;
    return m.Read(0x007d07a4)==0xea00033bu&&m.Read(0x007d079c)==0xe1510003u; // ref 0x7d07a4 (Restoration source port), 0x7d079c (Restoration source port)
}
inline bool Pointer(U32 p,U32 n=4){return p>=0x100000&&n<=0x1000000&&p<=0x40000000-n;}
template<class M> void Apply(M& m,U32 self,U32 id) {
    const bool active=enabled.load();
    const Replacement* replacement=nullptr;
    for(const auto& r:Replacements)if(r.id==id){replacement=&r;break;}
    if(!replacement||!Pointer(self,20))return;
    auto base=m.Read(self+4),entries=m.Read(self+8),size=m.Read(self+12),stride=m.Read(self+16);
    if(!base||!entries||!size||stride!=20||!Pointer(*base,16)||
       *size<16||*size>0x1000000||!Pointer(*base,*size)||*entries!=*base+16||
       m.Read(*base)!=0x47534d47u||m.Read(*base+4)!=1||m.Read(*base+12)!=2)return;
    auto count=m.Read(*base+8);
    if(!count||!*count||*count>20000||16+*count*20>*size)return;
    U32 lo=0,hi=*count;
    while(lo<hi) {
        const U32 mid=lo+(hi-lo)/2,entry=*entries+mid*20;
        auto key=m.Read(entry);if(!key)return;
        const U32 candidate=*key&65535;
        if(candidate<id){lo=mid+1;continue;}
        if(candidate>id){hi=mid;continue;}
        auto offset=m.Read(entry+12),length=m.Read(entry+16);
        if(!offset||!length||*length!=replacement->size||replacement->text.size()>*length||
           (*offset&1)||*offset<16+*count*20||*offset>*size||*length>*size-*offset)return;
        const auto text=active?replacement->text:replacement->original;
        const U32 start=*base+*offset,end=start+*length;
        std::uint64_t hash=0xcbf29ce484222325ULL;
        struct Write {U32 at,value;};std::vector<Write> writes;
        // Read and validate every destination word before writing anything.
        // Preserve neighboring entries when this text is only halfword-aligned.
        for(U32 at=start&~3u;at<end;at+=4) {
            auto before=m.Read(at);if(!before)return;
            U32 value=*before;
            for(unsigned byte=0;byte<4;++byte) {
                const U32 pos=at+byte;if(pos<start||pos>=end)continue;
                hash=(hash^((*before>>(byte*8))&255))*0x100000001b3ULL;
                const auto i=pos-start;
                const U32 next=i<text.size()?static_cast<unsigned char>(text[i]):0;
                value=(value&~(255u<<(byte*8)))|(next<<(byte*8));
            }
            writes.push_back({at,value});
        }
        // Already edited text and user-provided translations remain untouched.
        if(hash!=(active?replacement->hash:replacement->edited_hash))return;
        for(const auto& w:writes)m.Write(w.at,w.value);
        return;
    }
}
}
