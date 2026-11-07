// SPDX-License-Identifier: GPL-2.0-or-later
// Tests for the guest core ported from mm3d-dekui.
#include "core.h"
#include "core_test_sites.h"
#include "inventory_menu.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

using namespace Mm3dDekui;

namespace {
void Check(bool value, const std::string& what) {
    if (!value) throw std::runtime_error(what);
}

struct TestCpu final : Cpu {
    std::array<U32,16> r{};
    std::array<U32,64> s{};
    U32 flags=0;
    std::array<U32,16>& Regs() override { return r; }
    U32 Cpsr() override { return flags; }
    void SetCpsr(U32 value) override { flags=value; }
    std::array<U32,64>& ExtRegs() override { return s; }
};

struct TestMemory final : Memory {
    std::vector<unsigned char> image;
    std::map<U32,U32> data;
    std::set<U32> reads;
    std::optional<U32> corrupt;
    bool missing=false,record=false;
    unsigned writes=0;
    std::optional<U32> Read(U32 at) override {
        if (record) reads.insert(at);
        if (corrupt==at && missing) return std::nullopt;
        std::optional<U32> value;
        const auto found=data.find(at);
        if (found!=data.end()) value=found->second;
        else if (at>=0x100000U && std::uint64_t(at)-0x100000U+4<=image.size()) {
            const auto p=at-0x100000U;
            value=U32(image[p])|(U32(image[p+1])<<8)|(U32(image[p+2])<<16)|(U32(image[p+3])<<24);
        }
        if (value && corrupt==at) *value^=1;
        return value;
    }
    void Write(U32 at,U32 value) override { data[at]=value; ++writes; }
};

void PutName(TestMemory& m,U32 at,const char* name) {
    const std::string text=name;
    for (unsigned off=0;off<=text.size();off+=4) {
        U32 word=0;
        for (unsigned i=0;i<4 && off+i<text.size();++i) word|=U32(static_cast<unsigned char>(text[off+i]))<<(8*i);
        m.data[at+off]=word;
    }
}

void MessageFixture(TestMemory& m,U32 base) {
    const U32 root=base+8,group=base+0x200,plate=base+0x400,indicator=base+0x600;
    m.data[base+4]=base+0x1000; m.data[base+0x1018]=base+0x1100;
    PutName(m,base+0x1100,"MessageWindow");
    m.data[root+0x18]=1; m.data[root+0x14]=base+0x1200; m.data[base+0x1200]=group;
    m.data[group+8]=root; m.data[group+0x10]=base+0x1300; PutName(m,base+0x1300,"all");
    m.data[group+0x18]=3; m.data[group+0x14]=base+0x1400;
    m.data[base+0x1400]=plate; m.data[base+0x1404]=indicator; m.data[base+0x1408]=0;
    m.data[group+0x8c]=std::bit_cast<U32>(10.f); m.data[group+0x90]=std::bit_cast<U32>(20.f);
    m.data[group+0x98]=std::bit_cast<U32>(1.f); m.data[group+0x9c]=std::bit_cast<U32>(1.5f);
    m.data[group+0xd4]=1; m.data[group+0xd8]=0x1800;
    m.data[plate+0x10]=base+0x1500; PutName(m,base+0x1500,"plate");
    m.data[plate+0x98]=std::bit_cast<U32>(1.25f); m.data[plate+0xd4]=1; m.data[plate+0xd8]=0;
    m.data[indicator+0x10]=base+0x1600; PutName(m,base+0x1600,"waitIcon");
    m.data[indicator+0x8c]=std::bit_cast<U32>(5.f); m.data[indicator+0x90]=std::bit_cast<U32>(6.f);
    m.data[indicator+0xd4]=1; m.data[indicator+0xd8]=0;
}

void SharedHookFrames() {
    Core core;
    TestMemory memory;
    TestCpu cpu;
    constexpr U32 first=0x10010000,second=0x10014000;
    MessageFixture(memory,first); MessageFixture(memory,second);
    const auto original=memory.data;
    DialogueScale::scale=.75f; DialogueScale::y_offset=12.f;
    DialogueScale::background_width=1.5f; DialogueScale::indicator_x=4; DialogueScale::indicator_y=-3;
    cpu.r[13]=0x10020000;
    for (U32 layout:{first,second}) {
        cpu.r[4]=layout;
        core.CallSvc(DialogueScale::BeforeSVC,cpu,memory);
        Check(cpu.r[0]==layout+8,"dialogue displaced ADD r0,r4,#8");
        Check(memory.Read(layout+0x298)==std::bit_cast<U32>(.75f),"shared dialogue scales named all group");
        Check(memory.Read(layout+0x290)==std::bit_cast<U32>(32.f),"shared dialogue offsets animated pose");
        Check(memory.Read(layout+0x498)==std::bit_cast<U32>(1.875f),"dialogue widens only plate");
        Check(memory.Read(layout+0x68c)==std::bit_cast<U32>(9.f),"dialogue indicator offset");
    }
    for (U32 layout:{second,first}) {
        cpu.r[4]=layout;
        const U32 sp=cpu.r[13];
        core.CallSvc(DialogueScale::AfterSVC,cpu,memory);
        Check(cpu.r[13]==sp+8,"dialogue displaced ADD sp,sp,#8");
        for (U32 off:{0x28cU,0x290U,0x298U,0x29cU,0x2d8U,0x498U,0x4d4U,0x4d8U,0x68cU,0x690U,0x6d4U,0x6d8U})
            Check(memory.Read(layout+off)==original.at(layout+off),"nested dialogue restores its frame");
        // The donor deliberately retains the group's dirty flags for recalculation.
        Check(memory.Read(layout+0x2d4)==(original.at(layout+0x2d4)|0x1b00),"dialogue retains native dirty flags");
    }
    DialogueScale::scale=1; DialogueScale::y_offset=0; DialogueScale::background_width=1;
    DialogueScale::indicator_x=0; DialogueScale::indicator_y=0;

    // Shared Begin/End must remain balanced even for unsupported layouts and
    // nested draws; every module pushes a frame before deciding to decline.
    cpu.r[13]=0x10030000; cpu.r[6]=0x10031000;
    for (U32 layout:{first+0x2000,second+0x2000}) {
        cpu.r[4]=layout;
        core.CallSvc(NativeInventory::FirstSVC+2,cpu,memory);
        Check(cpu.r[2]==layout+8 && cpu.r[13]==0x10030000,"unsupported root falls through without scratch allocation");
    }
    for (U32 layout:{second+0x2000,first+0x2000}) {
        cpu.r[4]=layout; cpu.r[0]=layout;
        core.CallSvc(NativeInventory::FirstSVC+3,cpu,memory);
        Check(cpu.r[13]==0x10030000 && memory.Read(cpu.r[6])==layout,"shared End balances frames and replays STR");
    }
}

void HelpersAndReplay() {
    Check(Difficulty::ApplyDamage(48,U32(-8),4)==16,"damage multiplier");
    Check(Difficulty::ApplyDamage(24,U32(-8),4)==0,"damage clamp");
    Check(Difficulty::ApplyDamage(16,8,16)==24,"healing unchanged");
    Check(Difficulty::SuppressDrop(0xe,3,true),"heart drop");
    Check(!Difficulty::SuppressDrop(0xe,0x19,false,false,false,false,true),"empty bomb placeholder");
    Check(PauseCapture::Physical(0x14000020)==0x20000020,"pause linear address");
    Check(PauseCapture::Physical(0x1f000010)==0x18000010,"pause VRAM address");
    Check(PauseCapture::Physical(0x00400000)==0,"pause invalid address");
    Check(CameraLive::ProjectionValue(std::bit_cast<U32>(45.f),45.f)==std::bit_cast<U32>(45.f),"FOV off");
    Check(std::abs(std::bit_cast<float>(CameraLive::ProjectionValue(std::bit_cast<U32>(45.f),60.f))-60.f)<.001f,"FOV widening");
    const auto fitted=Widescreen::FitTop({0,0,400,240},{0,240,320,480},480,true);
    Check(fitted.GetHeight()==240 && fitted.right-fitted.left==427,"widescreen slot fit");

    Core core;
    TestMemory memory;
    TestCpu cpu;
    for (unsigned i=0;i<16;++i) cpu.r[i]=0x12340000U+i;
    for (unsigned i=0;i<64;++i) cpu.s[i]=0xabcd0000U+i;
    cpu.flags=0xa0000030;
    cpu.r[2]=48; cpu.r[4]=U32(-8);
    const auto before=cpu.r;
    const auto ext=cpu.s;
    Difficulty::damage_multiplier=4;
    core.CallSvc(Difficulty::SVC,cpu,memory);
    auto expected=before; expected[2]=16;
    Check(cpu.r==expected && cpu.s==ext && cpu.flags==0xa0000030 && memory.writes==0,"ADD replay affects only r2");
    Difficulty::damage_multiplier=1;

    cpu.r[13]=0x10002000; cpu.r[15]=Difficulty::SpawnPC+4;
    cpu.r[2]=0x123; memory.data[cpu.r[13]+8]=0;
    const auto spawn=cpu.r;
    core.CallSvc(Difficulty::SpawnSVC,cpu,memory);
    Check(cpu.r[13]==spawn[13]-16 && cpu.r[15]==spawn[15],"spawn prologue continuation");
    Check(memory.Read(spawn[13]-16)==spawn[4] && memory.Read(spawn[13]-4)==spawn[14],"spawn displaced stack stores");
    cpu.r=spawn; cpu.r[2]=0xe; memory.data[cpu.r[13]+8]=3;
    Difficulty::disable_heart_drops=true;
    const auto writes=memory.writes;
    core.CallSvc(Difficulty::SpawnSVC,cpu,memory);
    Check(cpu.r[0]==0 && cpu.r[15]==cpu.r[14] && cpu.r[13]==spawn[13] && memory.writes==writes,"suppressed spawn returns without prologue");
    Difficulty::disable_heart_drops=false;
    Difficulty::Settings legacy;
    cpu.r=spawn;
    Check(Difficulty::Spawn(memory,cpu.r,legacy)==Difficulty::kSpawnPc+4 && cpu.r[15]==spawn[15],"existing difficulty API");

    cpu.r.fill(0); cpu.r[4]=0x10004000; memory.data[cpu.r[4]+0x18]=std::bit_cast<U32>(45.f);
    CameraLive::desired_fov=60.f;
    core.CallSvc(CameraLive::ProjectionSVC,cpu,memory);
    Check(std::abs(std::bit_cast<float>(cpu.s[0])-60.f)<.001f,"projection SVC replays VLDR to S0");
    for (unsigned i=1;i<64;++i) Check(cpu.s[i]==ext[i],"projection preserves other VFP words");
    CameraLive::desired_fov=45.f;
    cpu.r[1]=0; cpu.r[2]=1; cpu.flags=0x0fffffffu;
    core.CallSvc(SongList::NavSVC,cpu,memory);
    Check(cpu.flags==0x8fffffffu,"song navigation CMP flags and control bits");
    const auto unchanged=cpu.r;
    const auto unchanged_s=cpu.s;
    core.CallSvc(1,cpu,memory);
    Check(cpu.r==unchanged && cpu.s==unchanged_s,"unknown SVC is inert");
    // A completed bottom-manager pass must not classify a later standalone
    // widget as bottom-screen. There is no active LayoutBase context here.
    cpu.r[0]=0;cpu.r[1]=5;
    core.CallSvc(NativeInventory::FirstSVC+1,cpu,memory);
    cpu.r=unchanged;
    unsigned emissions=0;
    memory.data[cpu.r[4]+4]=0x10008000;memory.data[cpu.r[4]+0x28]=0x10009000;
    core.host.layout_draw=[&](Memory&,U32 owner,U32 widget,U32 pass){
        Check(owner==0x10008000&&widget==cpu.r[4]&&pass==~0u,"attached widget without LayoutBase context");++emissions;return false;
    };
    core.CallSvc(ButtonEmission::Svc,cpu,memory);
    auto replay=unchanged;replay[0]=0x10009000;
    Check(emissions==1&&cpu.r==replay&&cpu.s==unchanged_s&&cpu.flags==0x8fffffffu,"emission displaced load only; SP LR flags VFP intact");
    core.host.layout_draw=[](Memory&,U32,U32,U32){return true;};
    core.CallSvc(ButtonEmission::Svc,cpu,memory);replay[0]=0;
    Check(cpu.r==replay&&cpu.s==unchanged_s&&cpu.flags==0x8fffffffu,"replacement uses native null geometry branch; children and ABI intact");
    core.ResetButtonEmission();
    Check(!InventoryMenu::RestoreLegacy().empty(),"Qt legacy archive operation explicitly unavailable");
}

void ImageContracts(const char* path) {
    if (!path || !*path) {
        std::cout << "SKIP code.bin contracts: pass a path or set MM3D_CODE_BIN\n";
        return;
    }
    std::ifstream file(path,std::ios::binary);
    if (!file) {
        std::cout << "SKIP code.bin contracts: file absent: " << path << '\n';
        return;
    }
    TestMemory memory;
    memory.image.assign(std::istreambuf_iterator<char>(file),{});
    Check(memory.image.size()==5967872,"expected vanilla EUR v1.1 code.bin size");
    Core core;
    const auto pcs=Core::CandidatePcs();
    Check(pcs.size()==TestData::Sites.size(),"candidate manifest size");
    Check(std::is_sorted(pcs.begin(),pcs.end()) && std::adjacent_find(pcs.begin(),pcs.end())==pcs.end(),"candidate PCs sorted and unique");
    unsigned positives=0,blocked=0,mutations=0;
    for (std::size_t i=0;i<pcs.size();++i) {
        const auto& site=TestData::Sites[i];
        const auto label=std::string(site.module)+" at "+std::to_string(site.pc);
        Check(pcs[i]==site.pc,"candidate order "+label);
        Check(memory.Read(site.pc)==site.word,"intercepted original word "+label);
        memory.reads.clear(); memory.record=true;
        const auto got=core.Instruction(memory,site.pc,site.word);
        memory.record=false;
        if (site.restoration_dependency) {
            Check(got==site.word,"Restoration dependency must remain guarded "+label);
            ++blocked;
            for (auto [at,word]:TestData::RestorationGuards) memory.data[at]=word;
            memory.reads.clear(); memory.record=true;
            Check(core.Instruction(memory,site.pc,site.word)==site.replacement,"synthetic Restoration-guard contract "+label);
            memory.record=false;
        } else {
            if (got!=site.replacement) {
                std::cerr << std::hex << "FAIL " << site.module << " PC=" << site.pc << " got=" << got << " expected=" << site.replacement << std::dec << '\n';
            }
            Check(got==site.replacement,"translated replacement "+label);
            ++positives;
        }
        Check(!memory.reads.empty(),"positive site must have guards "+label);
        const auto guards=memory.reads;
        for (U32 guard:guards) {
            memory.corrupt=guard;
            for (bool missing:{false,true}) {
                memory.missing=missing;
                const auto input=memory.Read(site.pc).value_or(site.word);
                Check(core.Instruction(memory,site.pc,input)==input,"guard mutation/unmapped rejection "+label+" guard="+std::to_string(guard));
                ++mutations;
            }
            memory.missing=false; memory.corrupt.reset();
        }
        Check(core.Instruction(memory,site.pc,site.word^1)==(site.word^1),"wrong intercepted word rejected "+label);
        memory.data.clear();
    }
    for (U32 pc:TestData::Excluded) {
        Check(!std::binary_search(pcs.begin(),pcs.end(),pc),"appended site excluded from registry");
        Check(core.Instruction(memory,pc,0xe1110005U)==0xe1110005U,"appended substitution excluded");
    }
    Check(core.Instruction(memory,0x100000,0xe1a00000)==0xe1a00000,"unrelated PC passes through");
    Check(memory.writes==0,"instruction selection never mutates guest memory");
    std::cout << "PASS code.bin: " << positives << " replacements, " << blocked
              << " Restoration-guarded native sites, " << mutations
              << " corrupt/missing guard checks; 17 appended entries excluded\n";
}
} // namespace

int main(int argc,char** argv) {
    try {
        HelpersAndReplay();
        SharedHookFrames();
        std::cout << "PASS helpers and SVC register/stack/VFP replay\n";
        ImageContracts(argc>1 ? argv[1] : std::getenv("MM3D_CODE_BIN"));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
