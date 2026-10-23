// Ported from mm3d-dekui src/mm3d/file_select_prompt.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "items_map_prompt.h"
#include "native_file_select.h"
#include "options_menu.h"

namespace Mm3dDekui::FileSelectPrompt {
using namespace NativeInventory;
using Options = OcarinaPrompt::Options;
inline std::atomic<float> x{0},y{0},scale{1},icon_scale{1},spacing{22};
inline std::atomic<bool> enabled{true};
inline void Apply(const Options& o) {
    enabled=o.enabled;x=o.x;y=o.y;scale=o.scale;icon_scale=o.icon_scale;spacing=o.spacing;
}
struct Hook {
    ItemsMapPrompt::Hook source;
    template<class M> U32 Root(M& m,const NativeFileSelect::Hook& files,
        const OptionsMenu::Hook& options,U32 layout,U32 sp,U32 content,bool wide) {
        auto& prompt=source.prompt;prompt.pass=~0u;
        // Options replaces the file panel. Append once, to its visible top copy,
        // and never to the retained bottom layouts used for native input.
        const bool option=options.panel&&options.pass==3&&layout==options.panel&&
            LayoutNamed(m,layout,"MenuOption");
        const bool file=!options.panel&&files.active&&files.pass==3&&layout==files.panel&&
            LayoutNamed(m,layout,"FileSelect");
        if(enabled&&(option||file)) {
            prompt.icon=prompt.text=0;prompt.texture=prompt.Parts(m);
            NativeInventory::Hook helper;
            source.Templates(m,helper,layout,"SelectBack");
            if(file&&files.summary)source.Templates(m,helper,files.summary,"SelectBack");
            // Options labels may be textured. Its native help window supplies
            // the same font before gameplay has ever been entered.
            if(!prompt.text&&m.Read(0x001c5074)==0xe59f0004u&& // ref 0x1c527c
                m.Read(0x001c5078)==0xe8bd8070u&&m.Read(0x001c5080)==0x006b3700u) { // ref 0x1c5280, 0x1c5288, 0x6b36f8
                auto window=m.Read(0x006b3700+0x2c); // ref 0x6b36f8
                if(window&&LayoutNamed(m,*window,"MessageWindow"))
                    source.Templates(m,helper,*window,"SelectBack");
            }
            if(prompt.icon&&prompt.text&&prompt.texture) {
                prompt.panel=layout;prompt.pass=3;
                prompt.native_alpha=file?files.prompt_opacity:NativeStartup::BottomVisibility(m);
            }
        }
        OcarinaPrompt::PairPrompt pair{
            {true,x.load(),y.load(),scale.load(),icon_scale.load(),spacing.load()},
            content,{"Select","Back"},{112,128}}; // Native atlas: A at112, B at128.
        const auto root=prompt.Root(m,layout,sp,wide,nullptr,&pair);
        return root==layout+8?content:root;
    }
    template<class M> U32 End(M& m){return source.prompt.End(m);}
};
}
