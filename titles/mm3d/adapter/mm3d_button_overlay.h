// SPDX-License-Identifier: GPL-2.0-or-later
// Native tree traversal follows mm3d-dekui's NativeInventory widget ABI.
#pragma once

#include "native_button_overlay.h"
#include "../dekui/ocarina_prompt.h"
#include "../dekui/button_emission.h"
#include <set>
#include <tuple>
#include <memory>
#include <deque>
#include <span>

namespace Oot3dNativeGame {
// EUR v1.1 widget emission ABI: 134DE4 supplies each actually rendered pane,
// including attached Pictograms and dekui's temporary prompt roots. Resource
// identity is the live registry entry, not pixels, a scene or a texture hash.
class Mm3dButtonOverlay {
public:
  struct Counters { uint64_t Layouts=0, Labels=0, Parts=0, Hud=0, Rings=0, Frames=0, Presented=0, Pictograms=0, Dpad=0, Text=0, SaturatedAlpha=0, StandaloneLabels=0, EmptyRecords=0, PassRejected=0, GuardRejected=0, Candidates=0, TintRejected=0, PoseRejected=0, UnboundCandidates=0, RingGeometryRejected=0, CapacityRejected=0, DuplicateCandidates=0, InlineGlyphs=0; };
  const Counters& Stats() const { return mStats; }
  size_t PendingLabels() const { size_t n=0;for(const auto& pane:mPending)n+=pane.Slots.size();return n; }
  size_t PendingPanes() const { return mSeen.size(); }
  void Configure(const NativeControlConfig& controls, NativePromptSet prompts) {
    const bool active = controls.NativeButtonLabels && prompts != NativePromptSet::Touch;
    mControls = controls; mPrompts = prompts; mActive = active;
    mStyle=std::make_shared<Style>(Style{controls,prompts});
  }
  void Reset() { mPending.clear(); mSeen.clear(); mDraws.clear(); mSubmitted.reset(); mPresented.clear(); }
  // Called at actual GSP submission, BEFORE that list is decoded. A host
  // refresh may produce zero, one or several lists, or yield halfway through
  // building the next one. Never drain captures at a host-refresh boundary.
  void SubmitCommandList(uint32_t begin,uint32_t bytes) {
    auto snapshot=std::make_shared<Snapshot>();
    const uint64_t end=uint64_t(begin)+bytes;
    if(bytes && end<=uint64_t(UINT32_MAX)+1) {
      for(auto it=mPending.begin();it!=mPending.end();) {
        if(it->Cursor>=begin && it->Cursor<end) {
          mSeen.erase(it->Identity);it->Cursor=0;it->Identity={};
          snapshot->push_back(std::move(*it));it=mPending.erase(it);
        } else ++it;
      }
    }
    ++mStats.Frames;if(snapshot->empty())++mStats.EmptyRecords;
    mSubmitted=std::move(snapshot);
  }
  // Runs synchronously on successful packet enqueue, before the guest can
  // submit/reuse another command buffer. No guest addresses survive in draws.
  void ObserveDraw(uint64_t draw) {
    if(!mDraws.empty() && draw<=mDraws.back().Last)mDraws.clear();
    if(!mDraws.empty() && mDraws.back().Data==mSubmitted && mDraws.back().Last+1==draw)
      mDraws.back().Last=draw;
    else {
      if(mDraws.size()==512)mDraws.pop_front();
      mDraws.push_back({draw,draw,mSubmitted});
    }
  }
  void Select(std::span<const uint64_t> draws) {
    mPresented.clear();std::set<const Snapshot*> seen;
    for(auto id:draws) {
      auto it=std::lower_bound(mDraws.begin(),mDraws.end(),id,[](const auto& group,uint64_t value){return group.Last<value;});
      if(it!=mDraws.end() && it->First<=id && it->Data && seen.insert(it->Data.get()).second) {
        mPresented.push_back(it->Data);
        for(const auto& pane:*it->Data)mStats.Presented+=pane.Slots.size();
      }
    }
  }
  std::vector<oot3d::ui::UiPrimitive> Compose(float pixelsPerUnit) const {
    std::vector<oot3d::ui::UiPrimitive> result;
    for(const auto& snapshot:mPresented)for(const auto& pane:*snapshot)
      AppendNativeButtonOverlay(result,pane.Slots,pane.Settings->Prompts,pane.Settings->Controls,pixelsPerUnit);
    return result;
  }
  static constexpr uint32_t CommandCursorAddress=0x006A3A88U;
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
  // Individual native text records may use the parts atlas inside prose.
  // Observe at the glyph renderer, not by suppressing the whole text pane.
  template<class M> bool CaptureInline(M& m,uint32_t node,uint32_t record,bool shadow) {
    using namespace Mm3dDekui::NativeInventory;
    if(!mActive || !Pointer(node,0x140) || !Pointer(record,60))return false;
    Mm3dDekui::OcarinaPrompt::Hook resources;const auto parts=resources.Parts(m);
    const auto descriptor=parts?m.Read(parts+0x4c):std::optional<uint32_t>{};
    const auto pixels=descriptor && Pointer(*descriptor,0x24)?m.Read(*descriptor+0x20):std::optional<uint32_t>{};
    if(!pixels || !*pixels || m.Read(record)!=pixels || m.Read(record+0xc)!=std::optional<uint32_t>(256) ||
        m.Read(record+0x10)!=std::optional<uint32_t>(128) || m.Read(record+0x38)!=std::optional<uint32_t>(~0u))return false;
    const auto u=Float(m,record+0x28),v=Float(m,record+0x2c),uw=Float(m,record+0x30),vh=Float(m,record+0x34),
        ux=Float(m,node+0x110),uy=Float(m,node+0x114),ut=Float(m,node+0x118),
        vx=Float(m,node+0x11c),vy=Float(m,node+0x120),vt=Float(m,node+0x124);
    if(!u||!v||!uw||!vh||!ux||!uy||!ut||!vx||!vy||!vt||*uy!=0||*vx!=0||*ux<=0||*vy<=0)return false;
    const float left=*ux**u+*ut,top=*vy**v+*vt,width=*ux**uw,height=*vy**vh;
    auto action=PartsButton(left,top,width,height),modifier=NativeControlAction::Count;
    if(top==32.f/128 && width==16.f/256 && height==16.f/128) {
      if(left==160.f/256 || left==176.f/256)action=NativeControlAction::X;
      if(left==192.f/256 || left==208.f/256)action=NativeControlAction::Y;
      if(left==160.f/256 || left==208.f/256)modifier=NativeControlAction::Zr;
    }
    if(action==NativeControlAction::Count || !NativeButtonOverlayGlyph(action,mPrompts,mControls) ||
        (modifier!=NativeControlAction::Count && !NativeButtonOverlayGlyph(modifier,mPrompts,mControls)))return false;
    const auto x=Float(m,record+0x14),y=Float(m,record+0x18),w=Float(m,record+0x20),h=Float(m,record+0x24),
        xx=Float(m,node+0xe0),xy=Float(m,node+0xe4),yx=Float(m,node+0xf0),yy=Float(m,node+0xf4),
        tx=Float(m,node+0xec),ty=Float(m,node+0xfc),r=Float(m,node+0x128),g=Float(m,node+0x12c),
        b=Float(m,node+0x130),a=Float(m,node+0x134);
    if(!x||!y||!w||!h||!xx||!xy||!yx||!yy||!tx||!ty||!r||!g||!b||!a||*a<=0||*w<=0||*h<=0)return false;
    NativeButtonOverlaySlot slot;slot.Action=action;slot.Modifier=modifier;
    const float cx=*x+*w*.5f,cy=*y+*h*.5f;
    slot.X=*tx+*xx*cx+*xy*cy+40.f/3.f;slot.Y=*ty+*yx*cx+*yy*cy;
    slot.Width=std::hypot(*xx,*yx)**w;slot.Height=std::max(slot.Width,std::hypot(*xy,*yy)**h);
    slot.Color={*r,*g,*b,std::min(*a,1.f)};
    if(!std::isfinite(slot.X)||!std::isfinite(slot.Y)||slot.Height<=0||slot.Height>128||
        std::abs(slot.X)>1024||std::abs(slot.Y)>1024||std::max({*r,*g,*b})<=0)return false;
    const auto cursor=m.Read(CommandCursorAddress);
    if(!cursor || !Pointer(*cursor,8) || (*cursor&7) || PendingLabels()>=1020 || mPending.size()>=1020)return false;
    if(shadow)return true; // omit only this proven control glyph's native shadow
    const Key key{*cursor,record,slot.X,slot.Y};
    if(mSeen.insert(key).second) {
      mPending.push_back({*cursor,key,{slot},mStyle});++mStats.Labels;++mStats.InlineGlyphs;
    }
    return true;
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
    if (!text && parts && *texture==parts && u&&v&&uw&&vh) {
      // Pane::BuildMatrix animates the UV affine separately from shared
      // geometry (also consumed by ScheduleDetail::Clip). Pictogram.mfl's
      // nominal x96 cell alone cannot identify its currently displayed button.
      const auto ux=Float(m,node+0x110),uy=Float(m,node+0x114),ut=Float(m,node+0x118),
          vx=Float(m,node+0x11c),vy=Float(m,node+0x120),vt=Float(m,node+0x124);
      if(ux&&uy&&ut&&vx&&vy&&vt && *uy==0 && *vx==0 && *ux>0 && *vy>0) {
        const float left=*ux**u+*ut,top=*vy**v+*vt,width=*ux**uw,height=*vy**vh;
        action=PartsButton(left,top,width,height);
        // Restoration's amber inventory/inline cells, proven parts atlas
        // row32: I at160, X at176, Y at192, II at208.
        if(top==32.f/128 && width==16.f/256 && height==16.f/128) {
          if(left==160.f/256 || left==176.f/256)action=NativeControlAction::X;
          if(left==192.f/256 || left==208.f/256)action=NativeControlAction::Y;
          if(left==160.f/256 || left==208.f/256)modifier=NativeControlAction::Zr;
        }
      }
    }
    const auto owner=m.Read(node+4);
    // The notebook's attached A badge has animated native UVs. Its nominal
    // Pictogram.mfl cell is x96, not the final atlas A cell. Use the same
    // validated anchor binding as NativeNotebook, not a static crop guess.
    if(action==NativeControlAction::Count && parts && *texture==parts && owner &&
        LayoutNamed(m,*owner,"Pictogram") && Named(m,node,"pict")) {
      const auto anchor=m.Read(*owner+0x180);
      if(anchor && Named(m,*anchor,"disp_a_icon_n"))action=NativeControlAction::A;
    }
    if (action==NativeControlAction::Count && owner &&
        (LayoutNamed(m,*owner,"HudItemIconSet") || LayoutNamed(m,*owner,"ItemIconSet2"))) {
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
    const auto cursor=m.Read(CommandCursorAddress);
    if(!cursor || !Pointer(*cursor,8) || (*cursor&7)){++mStats.GuardRejected;return false;}
    if(PendingLabels()>=1020 || mPending.size()>=1020){++mStats.CapacityRejected;return false;}
    bool suppress=!ring;
    const Key key{*cursor,node,slot.X,slot.Y};
    // Dedup only the same emission destination, not a later native frame
    // which happens to reuse the same pane and position before host draining.
    if (mSeen.insert(key).second) {
      PendingPane pane{*cursor,key,{},mStyle};
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
          if(NativeButtonOverlayGlyph(key.Action,mPrompts,mControls)) {pane.Slots.push_back(key);++mStats.Labels;++mStats.Dpad;}
          else suppress=false;
        }
      } else {
        pane.Slots.push_back(slot); ++mStats.Labels;
        if (ring) ++mStats.Rings; else if (hud) ++mStats.Hud; else ++mStats.Parts;
        if(owner && LayoutNamed(m,*owner,"Pictogram")) ++mStats.Pictograms;
        if(text) ++mStats.Text;
      }
      mPending.push_back(std::move(pane));
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
  struct Style { NativeControlConfig Controls;NativePromptSet Prompts; };
  using Key=std::tuple<uint32_t,uint32_t,float,float>;
  struct PendingPane { uint32_t Cursor;Key Identity;std::vector<NativeButtonOverlaySlot> Slots;std::shared_ptr<const Style> Settings; };
  using Snapshot=std::vector<PendingPane>;
  struct DrawGroup { uint64_t First,Last;std::shared_ptr<const Snapshot> Data; };
  std::shared_ptr<const Style> mStyle;
  Snapshot mPending;
  std::set<Key> mSeen;
  std::shared_ptr<const Snapshot> mSubmitted;
  std::deque<DrawGroup> mDraws;
  std::vector<std::shared_ptr<const Snapshot>> mPresented;
};
} // namespace Oot3dNativeGame
