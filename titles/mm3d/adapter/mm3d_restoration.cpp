// SPDX-License-Identifier: GPL-2.0-or-later
// Runtime adapter for the attributed Project Restoration source port.
#include "mm3d_restoration.h"
#include "engine_impl.h"
#include "sites.h"
#include "substitute.h"
#include <nlohmann/json.hpp>
#include <set>

namespace Oot3dNativeGame {
namespace a32=oot3d::recomp::a32;
using namespace Mm3dRestoration;

Mm3dRestorationPort::Mm3dRestorationPort(Mm3dGuestCalls& calls):mCalls(calls),mSession(std::make_unique<Session>()) {}
Mm3dRestorationPort::~Mm3dRestorationPort()=default;
void Mm3dRestorationPort::SetDekuiBridge(DekuiBridge bridge){mSession->dekui=std::move(bridge);}
u32 Mm3dRestorationPort::HudHeartWidget() const noexcept{return mSession->hud_widgets[3];}

std::optional<std::vector<MessageEdit>> Mm3dRestorationPort::ReadMessageEdits(std::string_view text,
                                                                              std::string* error) {
    const auto bytes = [](const std::string& hex, std::vector<u8>* out) {
        if (hex.size() % 2U != 0U) return false;
        out->clear();
        for (size_t at = 0; at < hex.size(); at += 2U) {
            unsigned value = 0;
            for (const char c : hex.substr(at, 2U)) {
                value <<= 4U;
                if (c >= '0' && c <= '9') value |= unsigned(c - '0');
                else if (c >= 'a' && c <= 'f') value |= unsigned(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') value |= unsigned(c - 'A' + 10);
                else return false;
            }
            out->push_back(static_cast<u8>(value));
        }
        return true;
    };
    try {
        const auto document = nlohmann::json::parse(text);
        if (document.at("format").get<std::string>() != "triaevum_mm3d_message_edits_v1") {
            throw std::runtime_error("unsupported message edits format");
        }
        const auto number = [](const nlohmann::json& value, u64 maximum) -> u64 {
            if (!value.is_number_integer() ||
                (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() < 0) ||
                value.get<u64>() > maximum)
                throw std::runtime_error("message integer outside range");
            return value.get<u64>();
        };
        const auto hash = [](const nlohmann::json& value) -> u64 {
            const auto text = value.get<std::string>();
            if (text.size() != 16 || text.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                throw std::runtime_error("message hash must contain exactly 16 hexadecimal digits");
            return std::stoull(text, nullptr, 16);
        };
        const auto& rows = document.at("edits");
        if (!rows.is_array() || rows.size() > 20000)
            throw std::runtime_error("invalid message edit table");
        std::vector<MessageEdit> edits;
        size_t totalBytes = 0;
        for (const auto& entry : rows) {
            MessageEdit edit;
            edit.id = static_cast<u16>(number(entry.at("id"), 65535));
            edit.language = static_cast<u8>(number(entry.at("language"), 13));
            edit.price = entry.value("price", false);
            edit.original_hash = hash(entry.at("original_hash"));
            edit.edited_hash = hash(entry.at("edited_hash"));
            if (entry.contains("original_metadata") != entry.contains("edited_metadata"))
                throw std::runtime_error("message metadata requires both original and edited values");
            if (entry.contains("original_metadata")) {
                edit.original_metadata = static_cast<u32>(number(entry.at("original_metadata"), 0xffffffffULL));
                edit.edited_metadata = static_cast<u32>(number(entry.at("edited_metadata"), 0xffffffffULL));
            }
            const auto& original = entry.at("original").get_ref<const std::string&>();
            const auto& edited = entry.at("edited").get_ref<const std::string&>();
            if (original.empty() || original.size() != edited.size() || original.size() > 0x2000000 ||
                (totalBytes += original.size()) > 0x2000000)
                throw std::runtime_error("message byte extent exceeds preparation limit");
            if (!bytes(entry.at("original").get<std::string>(), &edit.original) ||
                !bytes(entry.at("edited").get<std::string>(), &edit.edited)) {
                throw std::runtime_error("message bytes are not hexadecimal");
            }
            edits.push_back(std::move(edit));
        }
        return edits;
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = exception.what();
        return std::nullopt;
    }
}

bool Mm3dRestorationPort::SetMessageEdits(std::vector<MessageEdit> edits) {
    const auto hash=[](const auto& bytes){u64 h=0xcbf29ce484222325ULL;for(u8 b:bytes)h=(h^b)*0x100000001b3ULL;return h;};
    std::set<std::pair<u16,u8>> seen;
    for(const auto& e:edits)if(e.language>=14||e.original.empty()||e.original.size()!=e.edited.size()||e.original.size()>0x1000000||hash(e.original)!=e.original_hash||hash(e.edited)!=e.edited_hash||e.original_metadata.has_value()!=e.edited_metadata.has_value()||!seen.emplace(e.id,e.language).second)return false;
    std::stable_sort(edits.begin(),edits.end(),[](const auto& a,const auto& b){return a.price<b.price;});
    mSession->message_edits=std::move(edits);return true;
}
PortCapabilities Mm3dRestorationPort::Capabilities(a32::MemoryBus& memory) const {
    if(!mPatches)return {};
    const auto enabled=mPatches->EnabledPcs();
    const auto matches=[&](std::string_view action){for(const auto& site:SourceSites)if(site.action==action){if(!std::binary_search(enabled.begin(),enabled.end(),site.pc))return false;for(unsigned i=0;i<site.guard_count;++i){u32 value=0;if(!memory.Read32(site.guards[i].first,&value)||value!=site.guards[i].second)return false;}return true;}return false;};
    return {matches("MessageDataGet"),bool(mSession->dekui)&&matches("rst_CameraChangeModeExHook")};
}
std::vector<u32> Mm3dRestorationPort::RegistryBoundaries() {
    std::set<u32> boundaries(SourceTargets.begin(),SourceTargets.end());for(const auto& site:SourceSites){boundaries.insert(site.pc);boundaries.insert(site.pc+4);}return {boundaries.begin(),boundaries.end()};
}
void Mm3dRestorationPort::Register(Mm3dGuestPatches& patches) {
    patches.AddTargets({SourceTargets.begin(),SourceTargets.end()});
    for(const auto& descriptor:SourceSites) {
        Mm3dGuestPatchSite site;site.Pc=descriptor.pc;site.Name="rst."+std::string(descriptor.hook);site.TraceName="rst."+std::string(descriptor.action);
        site.Guard.assign(descriptor.guards.begin(),descriptor.guards.begin()+descriptor.guard_count);
        site.Handler=[this,descriptor](a32::GuestState& state,a32::MemoryBus& memory)->std::optional<u32> {
            if(descriptor.kind==SiteKind::Patch)return Substitute(descriptor.replacement,descriptor.pc,state,memory);
            if(descriptor.action.starts_with("MainScreen_")&&mSession->hud_depth)return std::nullopt;
            Guest guest(memory);
            if(descriptor.action=="UpdatePadState"){mSession->input.UpdatePadState(guest,mSession->is_swimming);return std::nullopt;}
            if(descriptor.action=="UpdatePadStateForOcarina"){Input::UpdatePadStateForOcarina(guest);return std::nullopt;}
            u32 resume=descriptor.kind==SiteKind::Soft?descriptor.pc:descriptor.pc+4;
            if(descriptor.kind==SiteKind::Return)resume=state.r[14];
            if(descriptor.kind==SiteKind::Call)state.r[14]=descriptor.pc+4;
            if(descriptor.kind==SiteKind::Pre) {
                const auto displaced=Substitute(descriptor.word,descriptor.pc,state,memory);
                if(!displaced)throw std::logic_error("Restoration pre-hook replay failed");resume=*displaced;
            }
            return mCalls.Start(descriptor.pc,resume,[this,descriptor](Mm3dGuestCallContext& context){return Run(context,descriptor.action);},state,memory);
        };
        patches.Add(std::move(site));
    }
    for(auto [pc,action]:std::array<std::pair<u32,std::string_view>,5>{{{Session::StoneDelay1,"StoneDelay1"},{Session::StoneDelay2,"StoneDelay2"},{Session::SotInit,"SotInit"},{Session::SotCalc,"SotCalc"},{Session::SotDestroy,"SotDestroy"}}}) {
        Mm3dGuestPatchSite site;site.Pc=pc;site.Name="rst."+std::string(action);site.InGuestCode=false;
        site.Handler=[this,pc,action](a32::GuestState& state,a32::MemoryBus& memory){return mCalls.Start(pc,state.r[14],[this,action](Mm3dGuestCallContext& ctx){return Run(ctx,action);},state,memory);};patches.Add(std::move(site));
    }
    mPatches=&patches;
}
Mm3dGuestTask Mm3dRestorationPort::Run(Mm3dGuestCallContext& context,std::string_view action) {
    CallMachine machine;Engine engine(*mSession,machine,context);auto work=engine.Run(action);machine.leaf=work.handle;
    while(!work.Done()) {
        machine.pending.reset();machine.leaf.resume();
        if(machine.pending){const auto request=std::move(*machine.pending);machine.result=co_await context.Call(request.entry,request.args);}
        else if(!work.Done())throw std::logic_error("Restoration routine suspended without guest call");
    }
    (void)work.Result();
}
std::size_t Mm3dRestorationPort::ApplyDataPatches(a32::MemoryBus& memory) {
    std::size_t applied=0;
    for(const auto& patch:DataPatches) {
        bool matches=true;for(unsigned i=0;i<patch.size;++i){u8 value=0;matches&=memory.Read8(patch.pc+i,&value)&&value==patch.original[i];}
        if(!matches)continue;
        bool written=true;for(unsigned i=0;i<patch.size;++i)written&=memory.Write8(patch.pc+i,patch.replacement[i]);if(written)++applied;
    }return applied;
}
} // namespace Oot3dNativeGame
