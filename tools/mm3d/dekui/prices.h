// Ported from mm3d-dekui src/mm3d/prices.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <bit>
#include <optional>
#include <vector>
#include "price_hooks_data.h"
#include "price_text_data.h"
#include "single_screen_text.h"
namespace Mm3dDekui::Prices {
using U32=std::uint32_t;
inline std::atomic_bool enabled{false};
inline bool initialized=false;
inline void Initialize(bool value) {
    // UI-thread startup only. A pending purchase must not change price halfway through.
    if(!initialized){enabled.store(value);initialized=true;}
}
constexpr U32 FirstSVC=0xfffd00,Wallet=0x775318,ChangeRupees=0x002c157c; // ref 0x2c1634
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<Sites.size();++i)
        if(pc==Sites[i].pc&&word==Sites[i].word)return (word&0xf0000000)|0x0f000000|FirstSVC+i;
    return word;
}
template<class M> bool CodeMatches(M& m) {
    if(!SingleScreenText::CodeMatches(m))return false;
    for(const auto& site:Sites)
        for(unsigned j=0;j<site.guards.size();++j)
            if(m.Read(site.pc-8+j*4)!=site.guards[j])return false;
    return m.Read(ChangeRupees)==0xe59f100cu&&m.Read(ChangeRupees+4)==0xe1d122b4u;
}
inline U32 DoubleCharge(U32 amount,bool active) {
    const auto value=std::bit_cast<std::int32_t>(amount);
    return active&&value<0&&value>=-16383?U32(value*2):amount;
}
template<class M,class R> void Execute(M& m,R& r,unsigned index,bool active) {
    if(index>=Sites.size())return;
    const auto& site=Sites[index];
    if(site.kind==Kind::Funds) {
        // Replay the original signed-halfword load. Only the temporary value used
        // by this purchase check is halved; never change the wallet or its HUD.
        const U32 base=(site.word>>16)&15,dest=(site.word>>12)&15;
        const U32 offset=((site.word>>4)&0xf0)|(site.word&15),at=r[base]+offset;
        auto word=m.Read(at&~3u);
        const auto value=word?std::int32_t(std::int16_t(*word>>((at&2)*8))):0;
        r[dest]=U32(active&&at==Wallet&&value>=0?value/2:value);
        return;
    }
    if(site.kind==Kind::Fallback) {r[7]=active?20:10;return;}
    if(site.kind==Kind::Charge)r[0]=DoubleCharge(r[0],active);
    else if(site.kind==Kind::NetReward&&active&&r[0]==90)r[0]=80; // 100 prize minus doubled 10 fee
    r[14]=site.pc+4;r[15]=ChangeRupees; // original BL, flags unchanged
}
template<class M> void Message(M& m,U32 self,U32 id,bool active) {
    const MessageEdit* edit=nullptr;
    for(const auto& e:Messages)if(e.id==id){edit=&e;break;}
    if(!edit||!SingleScreenText::Pointer(self,20))return;
    auto base=m.Read(self+4),entries=m.Read(self+8),size=m.Read(self+12),stride=m.Read(self+16);
    if(!base||!entries||!size||stride!=20||!SingleScreenText::Pointer(*base,16)||
       *size<16||*size>0x1000000||!SingleScreenText::Pointer(*base,*size)||*entries!=*base+16||
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
        auto metadata=m.Read(entry+4),offset=m.Read(entry+12),length=m.Read(entry+16);
        if(!metadata||!offset||!length||*length!=edit->original.size()||edit->text.size()>*length||
           (*offset&1)||*offset<16+*count*20||*offset>*size||*length>*size-*offset)return;
        const auto text=active?edit->text:edit->original;
        const U32 wanted=active?edit->updated:edit->metadata,start=*base+*offset,end=start+*length;
        std::uint64_t hash=0xcbf29ce484222325ULL;
        struct Write{U32 at,value;};std::vector<Write> writes;
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
        if(*metadata==wanted&&hash==(active?edit->edited_hash:edit->hash))return;
        if(*metadata!=(active?edit->metadata:edit->updated)||hash!=(active?edit->hash:edit->edited_hash))return;
        // Validate metadata and the entire original text before changing either.
        for(const auto& w:writes)m.Write(w.at,w.value);
        m.Write(entry+4,wanted);return;
    }
}
}
