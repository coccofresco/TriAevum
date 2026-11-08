// Ported from mm3d-dekui src/mm3d/player_hud.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hud_adjustments.h"
#include "native_inventory.h"
namespace Mm3dDekui::PlayerHud {
using namespace NativeInventory;
using namespace HudAdjustments;
// Developer baseline, independent of player-facing HUD preferences.
inline AtomicButtonPose rupee_keys,carrots;
inline std::atomic<float> dpad_x{0},dpad_y{0};
inline void ApplyDpadPosition(float x,float y){dpad_x=x;dpad_y=y;}
inline std::atomic<float> magic_height{0}; // Zero leaves the installed layout unchanged.
inline void ApplyMagicHeight(float height){magic_height=height;}
inline void ApplyCarrots(const ButtonPose& p){carrots.x=p.x;carrots.y=p.y;carrots.scale=p.scale;}
inline void ApplyRupeeKeys(const ButtonPose& p){rupee_keys.x=p.x;rupee_keys.y=p.y;rupee_keys.scale=p.scale;}
struct Bounds {
    float left=1e9f,top=1e9f,right=-1e9f,bottom=-1e9f;
    bool Valid() const {return left<=right&&top<=bottom;}
    void Add(float x,float y){left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
    void Add(const Bounds& b){if(b.Valid()){Add(b.left,b.top);Add(b.right,b.bottom);}}
};
enum class Anchor {TopLeft,TopRight,BottomLeft,Center};
inline NativeInventory::Hook::Affine Mapping(const Bounds& b,Anchor anchor,float scale,float padding){
    const float x=anchor==Anchor::TopRight?b.right:anchor==Anchor::Center?(b.left+b.right)*.5f:b.left;
    const float y=anchor==Anchor::BottomLeft?b.bottom:anchor==Anchor::Center?(b.top+b.bottom)*.5f:b.top;
    return {scale,0,0,scale,x*(1-scale),y*(1-scale)+padding};
}
struct Hook {
    NativeInventory::Hook helper;
    using Write=NativeInventory::Hook::Write;
    std::vector<std::vector<Write>> frames;
    // A/B and X/Y/I/II live in different layouts. Cache their unadjusted
    // rendered bounds so all six scale about the same top-right corner.
    std::array<Bounds,2> button_bounds;
    // Tatl (Hud.chat_alart) belongs to the D-pad in Hud2. Keep the native
    // upper-left bounds so both layouts use the same anchor and vertical push.
    // Cache bounds, not a transform: slider changes apply in either draw order.
    std::array<Bounds,3> upper_bounds; // Dpad, Magic, Hearts
    template<class M> bool Measure(M& m,U32 node,Bounds& bounds) {
        std::vector<Write> unused;std::vector<U32> nodes;
        if(!helper.Transform(m,node,unused,nodes,{1,0,0,1,0,0}))return false;
        const auto f=[&](U32 at)->std::optional<float>{auto b=m.Read(at);if(!b)return {};
            float v=std::bit_cast<float>(*b);return std::isfinite(v)?std::optional<float>(v):std::nullopt;};
        for(auto n:nodes){
            auto geo=m.Read(n+0x28);if(!geo)return false;if(!*geo)continue;
            auto type=m.Read(*geo+4);if(!type)return false;if((*type&65535)!=6)continue;
            auto x=f(*geo+0x14),y=f(*geo+0x18),w=f(*geo+0x20),h=f(*geo+0x24),
                 xx=f(n+0xe0),xy=f(n+0xe4),yx=f(n+0xf0),yy=f(n+0xf4),tx=f(n+0xec),ty=f(n+0xfc);
            if(!x||!y||!w||!h||!xx||!xy||!yx||!yy||!tx||!ty)return false;
            for(float px:{*x,*x+*w})for(float py:{*y,*y+*h})bounds.Add(*tx+*xx*px+*xy*py,*ty+*yx*px+*yy*py);
        }
        return bounds.Valid();
    }
    template<class M> void SpaceHearts(M& m,const std::vector<U32>& nodes) {
        const int steps=std::clamp(heart_spacing.load(),-HeartSpacingLimit,HeartSpacingLimit);
        if(!steps)return;
        std::array<U32,20> hearts{};
        for(unsigned i=0;i<hearts.size();++i) {
            char name[]="heart00";name[5]='0'+i/10;name[6]='0'+i%10;
            for(auto node:nodes)if(Named(m,node,name)){hearts[i]=node;break;}
        }
        std::vector<Write> writes;std::vector<U32> seen;
        // Translate whole heart instances (including empty/fill/pulse layers).
        // Keep the first heart in each row fixed; apply after player scaling so
        // spacing follows both the editor baseline and player size adjustments.
        for(unsigned i=0;i<hearts.size();++i) {
            const auto first=hearts[i/10*10],node=hearts[i];
            if(!first||!node||i%10==0)continue;
            const auto a=m.Read(first+0xec),b=m.Read(node+0xec);
            if(!a||!b)return;
            const float origin=std::bit_cast<float>(*a),x=std::bit_cast<float>(*b);
            if(!std::isfinite(origin)||!std::isfinite(x))return;
            const float dx=(x-origin)*(.005f*steps);
            if(!helper.Transform(m,node,writes,seen,{1,0,0,1,dx,0}))return;
        }
        for(auto w:writes)m.Write(w.address,w.after);
        frames.back().insert(frames.back().end(),writes.begin(),writes.end());
    }
    template<class M> void MoveButtons(M& m,const std::vector<U32>& nodes,bool actions) {
        constexpr std::array<const char*,6> icons{"icon_item_x_g","icon_item_y_g","icon_item_1_g","icon_item_2_g","a_action_button","b_action_button"};
        constexpr std::array<const char*,4> bases{"btn_x_g","btn_y_g","btn_corner01_g","btn_corner03_g"};
        std::vector<Write> writes;std::vector<U32> seen;
        for(unsigned i=0;i<6;++i) {
            if(actions!=(i>=4))continue;
            const auto slot=i; // StickControls::ItemLayoutIndex(i,2) arrives in S5 (identity while it is off)
            const int dx=button_x[slot].load(),dy=button_y[slot].load();if(!dx&&!dy)continue;
            for(auto n:nodes)if(Named(m,n,icons[i])||(i<4&&Named(m,n,bases[i])))
                if(!helper.Transform(m,n,writes,seen,{1,0,0,1,float(dx),float(dy)}))return;
        }
        for(auto w:writes)m.Write(w.address,w.after);
        frames.back().insert(frames.back().end(),writes.begin(),writes.end());
    }
    template<class M> void Begin(M& m,U32 layout) {
        const bool nested=std::any_of(frames.begin(),frames.end(),[](const auto& f){return !f.empty();});
        frames.emplace_back();if(nested)return;
        const bool hud=LayoutNamed(m,layout,"Hud"),hud2=LayoutNamed(m,layout,"Hud2"),icons=LayoutNamed(m,layout,"HudItemIconSet");
        if(!hud&&!hud2&&!icons)return;
        std::vector<Write> unused;std::vector<U32> nodes;
        if(!helper.Transform(m,layout+8,unused,nodes,{1,0,0,1,0,0}))return;
        const auto find=[&](const char* name){for(auto n:nodes)if(Named(m,n,name))return n;return U32{0};};
        if(hud2)if(auto meter=find("magic_gauge_g")) {
            const float target=magic_height.load();
            const auto sy=m.Read(meter+0x4c);
            Bounds bar;
            if(auto base=find("gauge_base"))Measure(m,base,bar);
            if(!bar.Valid())Measure(m,meter,bar);
            if(std::isfinite(target)&&target>=.5f&&target<=2.f&&sy&&bar.Valid()) {
                const float installed=std::bit_cast<float>(*sy);
                if(std::isfinite(installed)&&installed>0) {
                    // Local Y scale includes the height already baked into the
                    // archive. Replace it rather than multiplying it twice.
                    const float ratio=target*Factor(magic_height_steps.load())/installed;
                    if(ratio!=1.f) {
                        std::vector<Write> height_writes;std::vector<U32> seen;
                        // The layout origin is not the visible top edge; panes
                        // can extend above it. Grow downward from the frame top.
                        if(!helper.Transform(m,meter,height_writes,seen,{1,0,0,ratio,0,bar.top*(1-ratio)}))return;
                        for(auto w:height_writes)m.Write(w.address,w.after);
                        frames.back()=std::move(height_writes);
                    }
                }
            }
            const float width=MagicWidth(magic_width.load());
            if(width!=1.f&&bar.Valid()) {
                // Anchor to the visible frame, not the changing green fill.
                // Transform the entire meter before MagicBorder clones its
                // frame/shadow so every layer follows the same right edge.
                std::vector<Write> width_writes;std::vector<U32> seen;
                if(!helper.Transform(m,meter,width_writes,seen,{width,0,0,1,bar.left*(1-width),0}))return;
                for(auto w:width_writes)m.Write(w.address,w.after);
                frames.back().insert(frames.back().end(),width_writes.begin(),width_writes.end());
            }
        }
        struct Group {std::vector<U32> roots;Part part;Anchor anchor;bool bottom=false;U32 anchor_node=0;};
        std::vector<Group> groups;
        const auto add=[&](const char* name,Part part,Anchor anchor,bool bottom=false,const char* reference=nullptr){
            if(auto n=find(name))groups.push_back({{n},part,anchor,bottom,reference?find(reference):0});};
        if(hud2){
            add("heart_status",Hearts,Anchor::TopLeft);
            add("magic_gauge_g",Magic,Anchor::TopLeft,false,"gauge_base");
            add("hud_dpad",Dpad,Anchor::TopLeft);
            // The native digit columns already align. Scale both rows around
            // the rupee row's fixed anchor: their differently shaped icons must
            // not give each row a separate pivot (or counts drift at larger sizes).
            std::vector<U32> counters;
            const auto rupees=find("rupee_g");
            if(rupees)counters.push_back(rupees);
            if(auto keys=find("key_g"))counters.push_back(keys);
            if(!counters.empty())groups.push_back({counters,Rupees,Anchor::BottomLeft,true,rupees});
            add("carrot_status",Carrots,Anchor::Center,true);
            std::vector<U32> roots;for(const char* name:{"item_icon_set","icon_item_oca_g"})if(auto n=find(name))roots.push_back(n);
            if(!roots.empty())groups.push_back({roots,Buttons,Anchor::TopRight});
        }
        if(icons)groups.push_back({{layout+8},Buttons,Anchor::TopRight});
        if(hud){
            add("action_button_g",Buttons,Anchor::TopRight);
            add("minigame_g",Minigame,Anchor::TopRight);
            // Both clock displays share the main clock's anchor. Including the
            // countdown in the bounds would move that anchor when it appears.
            std::vector<U32> clock_roots;
            for(const char* name:{"game_timer_g","last_timer_g"})
                if(auto n=find(name))clock_roots.push_back(n);
            auto clock_anchor=find("timer_decoration_g");
            if(!clock_anchor)clock_anchor=find("game_timer_g");
            if(!clock_roots.empty())groups.push_back({clock_roots,Clock,Anchor::Center,true,clock_anchor});
            add("eventTimer",Timer,Anchor::Center);
            add("chat_alart",Dpad,Anchor::TopLeft);
        }
        // Measure every group before applying any player transformations.
        std::vector<Bounds> measured(groups.size());
        for(unsigned i=0;i<groups.size();++i){
            const auto& g=groups[i];auto& b=measured[i];
            if(g.anchor_node)Measure(m,g.anchor_node,b);
            if(!b.Valid())for(auto n:g.roots)Measure(m,n,b);
            if(g.part==Carrots) {
                // Use the six carrot faces, excluding the animated flash layers.
                // Include spent carrots so using a boost never shifts the pivot.
                Bounds faces;
                for(auto n:g.roots) {
                    std::vector<Write> unused;std::vector<U32> children;
                    if(helper.Transform(m,n,unused,children,{1,0,0,1,0,0}))
                        for(auto child:children)if(Named(m,child,"carrot_pane"))Measure(m,child,faces);
                }
                if(faces.Valid())b=faces;
            }
            if(g.part==Buttons) {
                // Item artwork, action labels and inactive ocarina panes can extend
                // far beyond the button circles. They must not define the pivot.
                Bounds bases;
                for(auto n:nodes)for(const char* name:{"a_button_base","btn_corner01_g","btn_x_g","btn_y_g","btn_corner03_g"})
                    if(Named(m,n,name))Measure(m,n,bases);
                if(bases.Valid())b=bases;
                if(b.Valid())button_bounds[hud?1:0]=b;
            }
        }
        if(hud2) {
            upper_bounds={};
            for(unsigned i=0;i<groups.size();++i)
                if(groups[i].part==Hearts||groups[i].part==Magic||groups[i].part==Dpad)
                    upper_bounds[groups[i].part]=measured[i];
        }
        Bounds upper;
        for(const auto& b:upper_bounds)upper.Add(b);
        const float global=OverallScale();
        std::vector<Write> writes;
        for(unsigned i=0;i<groups.size();++i){
            const auto& g=groups[i];auto b=measured[i];
            ButtonPose baseline;
            if(g.part==Rupees||g.part==Carrots) {
                const auto& pose=g.part==Rupees?rupee_keys:carrots;
                baseline={pose.x.load(),pose.y.load(),pose.scale.load()};
                if(!std::isfinite(baseline.x)||!std::isfinite(baseline.y)||!std::isfinite(baseline.scale)||baseline.scale<.25f||baseline.scale>3)baseline={};
            }
            const float scale=Scale(g.part)*baseline.scale,padding=g.bottom?-Bottom():Top();

            if(g.part==Buttons){b={};for(const auto& part:button_bounds)b.Add(part);}
            // Do not scale Tatl around its own corner. Until Hud2 has been
            // observed, leave it native rather than inventing a D-pad anchor.
            if(hud&&g.part==Dpad)b=upper_bounds[Dpad];
            if(!b.Valid())continue;
            auto mapping=Mapping(b,g.anchor,scale,padding);
            // EventTimer contains alternate language panes and animated digits.
            // Their combined bounds are not the visible timer's center. Anchor
            // horizontal scaling to the native top screen's center instead.
            if(g.part==Timer)mapping.tx=200.f*(1-scale);
            // Compose editor size/position before user scaling about the moved
            // anchor. User offsets/padding remain screen pixels.
            mapping.tx+=baseline.x;mapping.ty+=baseline.y;
            if(upper.Valid()&&(g.part==Hearts||g.part==Magic||g.part==Dpad)) {
                // Scale the upper-left arrangement together, including the gaps.
                // Individual growth also pushes lower elements down; editor
                // positions and any intentional baseline overlap remain intact.
                float push=0;
                for(const auto part:{Hearts,Magic}) {
                    const auto& above=upper_bounds[part];
                    if(part!=g.part&&above.Valid()&&above.bottom<=b.top)
                        push+=(above.bottom-above.top)*(Factor(elements[part].load())-1);
                }
                const float local=Factor(elements[g.part].load());
                mapping.tx=upper.left*(1-global)+global*b.left*(1-local);
                mapping.ty=upper.top*(1-global)+global*(b.top*(1-local)+push)+padding;
            }
            if(g.part==Dpad){mapping.tx+=dpad_x.load();mapping.ty+=dpad_y.load();}
            // User offsets are screen-space pixels, after scale and padding.
            mapping.tx+=HorizontalPadding(g.part)+X(g.part);mapping.ty+=Y(g.part);
            if(mapping.xx==1&&mapping.yy==1&&mapping.tx==0&&mapping.ty==0)continue;
            std::vector<U32> seen;
            for(auto n:g.roots)if(!helper.Transform(m,n,writes,seen,mapping))return;
        }
        for(auto w:writes)m.Write(w.address,w.after);
        frames.back().insert(frames.back().end(),writes.begin(),writes.end());
        MoveButtons(m,nodes,hud);
        if(hud2)SpaceHearts(m,nodes);
        // Multiply the resolved native alpha, retaining cutscene/transition
        // fades. Restrict this to gameplay groups, not screen covers or menus.
        const float alpha=Opacity();
        if(alpha!=1.f) {
            std::vector<Write> fades;std::vector<U32> faded;
            for(const auto& group:groups)for(U32 root:group.roots) {
                std::vector<Write> unused;std::vector<U32> children;
                if(!helper.Transform(m,root,unused,children,{1,0,0,1,0,0}))return;
                for(U32 node:children) {
                    if(std::find(faded.begin(),faded.end(),node)!=faded.end())continue;
                    faded.push_back(node);
                    const auto bits=m.Read(node+0x134);if(!bits)return;
                    const float native=std::bit_cast<float>(*bits);if(!std::isfinite(native))return;
                    fades.push_back({node+0x134,*bits,std::bit_cast<U32>(native*alpha)});
                }
            }
            for(auto w:fades)m.Write(w.address,w.after);
            frames.back().insert(frames.back().end(),fades.begin(),fades.end());
        }
    }
    template<class M> void End(M& m){if(frames.empty())return;
        for(auto i=frames.back().rbegin();i!=frames.back().rend();++i)m.Write(i->address,i->before);
        frames.pop_back();
    }
};
}
