// SPDX-License-Identifier: GPL-2.0-or-later
// Native tree traversal follows mm3d-dekui's NativeInventory widget ABI.
#pragma once

#include "native_button_overlay.h"
#include "../dekui/ocarina_prompt.h"
#include "../dekui/button_emission.h"
#include <set>
#include <tuple>

namespace Oot3dNativeGame {
// EUR v1.1 widget emission ABI: 134DE4 supplies each actually rendered pane,
// including attached Pictograms and dekui's temporary prompt roots. Resource
// identity is the live registry entry, not pixels, a scene or a texture hash.
class Mm3dButtonOverlay {
public:
  struct Counters { uint64_t Layouts=0, Labels=0, Parts=0, Hud=0, Rings=0, Frames=0, Presented=0, Pictograms=0, Dpad=0, Text=0, SaturatedAlpha=0, StandaloneLabels=0, EmptyRecords=0, PassRejected=0, GuardRejected=0, Candidates=0, TintRejected=0, PoseRejected=0, UnboundCandidates=0, RingGeometryRejected=0, CapacityRejected=0, DuplicateCandidates=0; };
  const Counters& Stats() const { return mStats; }
  size_t PendingLabels() const { return mPending.size(); }
  size_t PendingPanes() const { return mSeen.size(); }
  void Configure(const NativeControlConfig& controls, NativePromptSet prompts) {
    const bool active = controls.NativeButtonLabels && prompts != NativePromptSet::Touch;
    mControls = controls; mPrompts = prompts; mActive = active;
  }
  void Reset() { mPending.clear(); mSeen.clear(); mFrames.Reset(); }
  void Record(uint64_t draw) {
    ++mStats.Frames;
    if(mPending.empty())++mStats.EmptyRecords;
    mFrames.Record(draw, std::move(mPending),mPrompts,mControls); mPending.clear(); mSeen.clear();
  }
  void Select(uint64_t draw) { mFrames.Select(draw, true); mStats.Presented += mFrames.Presented().size(); }
  std::vector<oot3d::ui::UiPrimitive> Compose(float pixelsPerUnit) const {
    std::vector<oot3d::ui::UiPrimitive> result;
    AppendNativeButtonOverlay(result, mFrames.Presented(), mFrames.Prompts(), mFrames.Controls(), pixelsPerUnit);
    return result;
  }
  // True suppresses only this native geometry, never the widget's children.
  template<class M> bool Capture(M& memory, uint32_t layout, uint32_t root, uint32_t pass) {
    if(!mActive)return false;
    if(pass>3 && pass!=~0u){++mStats.PassRejected;return false;}
    if(!Mm3dDekui::ButtonEmission::CodeMatches(memory) ||
        !Mm3dDekui::NativeInventory::Pointer(root,0x140)){++mStats.GuardRejected;return false;}
    const auto flags=memory.Read(root+0xd4);
    if (!flags || !(*flags&1)) return false;
    ++mStats.Layouts;
    Mm3dDekui::OcarinaPrompt::Hook resources;
    const auto before=mStats.Labels;
    const bool replaced=Pane(memory, layout, root, resources.Parts(memory));
    if(pass==~0u)mStats.StandaloneLabels+=mStats.Labels-before;
    return replaced;
  }
  static NativeControlAction PartsButton(float u, float v, float w, float h) {
    if (v != 0.f || w != 16.f/256 || h != 16.f/128) return NativeControlAction::Count;
    constexpr NativeControlAction actions[]{NativeControlAction::A, NativeControlAction::B,
        NativeControlAction::X, NativeControlAction::Y, NativeControlAction::L,
        NativeControlAction::R, NativeControlAction::Start};
    for (unsigned i=0; i<7; ++i) if (u == float(112+16*i)/256) return actions[i];
    return NativeControlAction::Count;
  }
private:
  template<class M> static std::optional<float> Float(M& m, uint32_t at) {
    auto value = m.Read(at); if (!value) return {};
    const float f = std::bit_cast<float>(*value);
    return std::isfinite(f) ? std::optional<float>(f) : std::nullopt;
  }
  template<class M> std::optional<std::array<float,5>> ZrText(M& m, uint32_t layout, uint32_t geo) {
    using namespace Mm3dDekui;
    using namespace NativeInventory;
    if (!Pointer(geo,0x30c) || !(LayoutNamed(m,layout,"MenuItem") || LayoutNamed(m,layout,"MenuMask") ||
        LayoutNamed(m,layout,"MenuCollect")) || m.Read(geo+0x308)!=std::optional<U32>(4)) return {};
    auto font=m.Read(geo+0x28),records=m.Read(geo+0x2fc);
    if (!font || !records || !Pointer(*records,240)) return {};
    float pen=0,height=0,width=0,plus=0;
    unsigned index=0;
    // Match the native font records semantically, not rendered pixels. Only
    // dekui's exact ZR+ legend qualifies; ordinary prose is never substituted.
    for (char c:std::string_view("ZR +")) {
      auto glyph=NotebookFont::Lookup(m,*font,c,pen); if (!glyph) return {};
      for (unsigned word=0;word<15;++word)
        if (m.Read(*records+index*60+word*4)!=std::optional<U32>(glyph->words[word])) return {};
      if(c=='+')plus=pen+glyph->advance*.5f;
      pen+=glyph->advance; height=std::bit_cast<float>(glyph->words[9]);
      if (++index==2) width=pen;
    }
    return std::array<float,5>{0,0,width,height,plus};
  }
  template<class M> bool Pane(M& m, uint32_t layout, uint32_t node, uint32_t parts) {
    using namespace Mm3dDekui::NativeInventory;
    auto geo=m.Read(node+0x28); if (!geo || !Pointer(*geo,0x7c)) return false;
    auto kind=m.Read(*geo+4),texture=m.Read(*geo+0x10);
    if (!kind || !texture) return false;
    std::optional<std::array<float,5>> text;
    if ((*kind&65535)==3) text=ZrText(m,layout,*geo);
    else if ((*kind&65535)!=6) return false;
    if ((*kind&65535)==3 && !text) return false;
    auto action=text ? NativeControlAction::Zr : NativeControlAction::Count, modifier=NativeControlAction::Count;
    const auto u=Float(m,*geo+0x28),v=Float(m,*geo+0x2c),uw=Float(m,*geo+0x30),vh=Float(m,*geo+0x34);
    if (!text && parts && *texture==parts && u&&v&&uw&&vh) action=PartsButton(*u,*v,*uw,*vh);
    const auto owner=m.Read(node+4);
    if (owner && (LayoutNamed(m,*owner,"HudItemIconSet") || LayoutNamed(m,*owner,"ItemIconSet2"))) {
      if (Named(m,node,"disp_x_pane")) action=NativeControlAction::X;
      if (Named(m,node,"disp_y_pane")) action=NativeControlAction::Y;
      // Restoration uses ZR+X/Y for I/II, NOT OoT's direct ZR/ZL mapping.
      if (Named(m,node,"disp_1_pane")) {action=NativeControlAction::X;modifier=NativeControlAction::Zr;}
      if (Named(m,node,"disp_2_pane")) {action=NativeControlAction::Y;modifier=NativeControlAction::Zr;}
    }
    const bool dpad=owner && LayoutNamed(m,*owner,"HudDpad") && Named(m,node,"pane_dpad");
    if(dpad) action=NativeControlAction::DpadUp;
    const bool hud = action != NativeControlAction::Count && !text && (!parts || *texture != parts);
    if (owner && LayoutNamed(m,*owner,"MessageWindow") && Named(m,node,"e_waitIcon"))
      action=NativeControlAction::A;
    bool ring=false;
    if (owner && LayoutNamed(m,*owner,"AActionIconBase") && Named(m,node,"bb00_ybtn")) {
      action=NativeControlAction::A;ring=true;
    }
    if (owner && LayoutNamed(m,*owner,"BActionIcon") && Named(m,node,"a_button_base_pane")) {
      action=NativeControlAction::B;ring=true;
    }
    if(action!=NativeControlAction::Count)++mStats.Candidates;
    if (action==NativeControlAction::Count) return false;
    if ((!dpad && !NativeButtonOverlayGlyph(action,mPrompts,mControls)) ||
        (modifier!=NativeControlAction::Count && !NativeButtonOverlayGlyph(modifier,mPrompts,mControls))) {
      ++mStats.UnboundCandidates; return false;
    }
    auto x=Float(m,*geo+0x14),y=Float(m,*geo+0x18),w=Float(m,*geo+0x20),h=Float(m,*geo+0x24);
    if (text) { x=(*text)[0]; y=(*text)[1]; w=(*text)[2]; h=(*text)[3]; }
    const auto xx=Float(m,node+0xe0),xy=Float(m,node+0xe4),yx=Float(m,node+0xf0),yy=Float(m,node+0xf4),
        tx=Float(m,node+0xec),ty=Float(m,node+0xfc),r=Float(m,node+0x128),g=Float(m,node+0x12c),
        b=Float(m,node+0x130),a=Float(m,node+0x134);
    if (!x||!y||!w||!h||!xx||!xy||!yx||!yy||!tx||!ty||!r||!g||!b||!a ||
        *a<=0 || *w<=0 || *h<=0) {++mStats.PoseRejected;return false;}
    if(std::max({*r,*g,*b})<=0){++mStats.TintRejected;return false;}
    // A/B letters occupy the top of the 38x38 native ring. The crop is a
    // title asset fact (HUD atlas A:24..32,0..10; B:64..72,0..10).
    if (ring) { if (*w!=38 || *h!=38) {++mStats.RingGeometryRejected;return false;} *x+=16; *w=8; *h=10; }
    const float cx=*x+*w*.5f, cy=*y+*h*.5f;
    NativeButtonOverlaySlot slot;
    slot.Action=action;slot.Modifier=modifier;
    // Same centered 400x240 -> 16:9 canvas as the MM3D adapter.
    slot.X=*tx+*xx*cx+*xy*cy+40.f/3.f; slot.Y=*ty+*yx*cx+*yy*cy;
    const float width=std::hypot(*xx,*yx)* *w, height=std::hypot(*xy,*yy)* *h;
    slot.Height=ring?std::hypot(width,height)+1.f:std::max(width,height);
    slot.Width=ring?0:width;
    slot.SuffixPlus=text.has_value();
    if(text) {slot.PlusX=*tx+*xx*(*text)[4]+*xy*cy+40.f/3.f;slot.PlusY=*ty+*yx*(*text)[4]+*yy*cy;}
    // Native animation/world-alpha products can overshoot one. The native
    // color conversion saturates; rejecting the pane makes every label blink
    // back to its placeholder art instead of following that conversion.
    if(*a>1)++mStats.SaturatedAlpha;
    slot.Color={*r,*g,*b,std::min(*a,1.f)};
    if (slot.Height<=0 || slot.Height>128 || std::abs(slot.X)>1024 || std::abs(slot.Y)>1024) {++mStats.PoseRejected;return false;}
    if(mPending.size()>=252){++mStats.CapacityRejected;return false;}
    bool suppress=!ring;
    // A shared embedded tree may be visited by its parent and by its own draw.
    if (mSeen.insert({node,slot.X,slot.Y}).second) {
      if(dpad) {
        // Four independently bound directions, positioned within the native
        // D-pad pane. Never replace the mask/instrument content around it.
        constexpr NativeControlAction actions[]{NativeControlAction::DpadUp,NativeControlAction::DpadDown,
            NativeControlAction::DpadLeft,NativeControlAction::DpadRight};
        constexpr float dx[]{0,0,-1,1},dy[]{-1,1,0,0};
        for(unsigned i=0;i<4;++i) {
          auto key=slot;key.Action=actions[i];key.Height=slot.Height*.36f;key.Width=key.Height;
          key.X+=(*xx*dx[i]* *w+*xy*dy[i]* *h)*.32f;
          key.Y+=(*yx*dx[i]* *w+*yy*dy[i]* *h)*.32f;
          if(NativeButtonOverlayGlyph(key.Action,mPrompts,mControls)) {mPending.push_back(key);++mStats.Labels;++mStats.Dpad;}
          else suppress=false;
        }
      } else {
        mPending.push_back(slot); ++mStats.Labels;
        if (ring) ++mStats.Rings; else if (hud) ++mStats.Hud; else ++mStats.Parts;
        if(owner && LayoutNamed(m,*owner,"Pictogram")) ++mStats.Pictograms;
        if(text) ++mStats.Text;
      }
    }
    else ++mStats.DuplicateCandidates;
    // Mixed A/B ring art remains native. A partially unbound D-pad retains
    // its cross; pure labels/pictograms and the exact ZR+ legend are replaced.
    if(dpad) for(auto direction:{NativeControlAction::DpadUp,NativeControlAction::DpadDown,
        NativeControlAction::DpadLeft,NativeControlAction::DpadRight})
      if(!NativeButtonOverlayGlyph(direction,mPrompts,mControls))suppress=false;
    if(dpad && !suppress)++mStats.UnboundCandidates;
    return suppress;
  }
  Counters mStats;
  NativeControlConfig mControls;
  NativePromptSet mPrompts=NativePromptSet::Touch;
  bool mActive=false;
  std::vector<NativeButtonOverlaySlot> mPending;
  std::set<std::tuple<uint32_t,float,float>> mSeen;
  NativeButtonOverlayFrames mFrames;
};
} // namespace Oot3dNativeGame
