# Scene point-light settings ownership

F1 **Lighting > Lights & Shadows > Scene lights > Dynamic scene point lights**
is the single owner. There are four independent groups, not a new pair of
Graphics settings:

| Index | Group | Sources |
|---|---|---|
| 0 | Companion (title-supplied display name; Navi by default) | Companion fairy |
| 1 | Fairy | Other fairy actors |
| 2 | Flame | Native actor lights and synthetic fires/glows |
| 3 | Architecture | Semantic room emitters and their surface-emission follow set |

Every group has Enabled, intensity (0–8), radius scale (0.1–8), and RGB tint.
The same `PicaScenePointLightSettings` reaches plain lighting, installed GGX,
point shadows and surface lighting. A native source's membership is title data,
not inferred from colour, distance, scene or texture brightness.

## Profiles and presets

`SceneEnvironmentRuntime` owns `pointLights[0..3]` in the existing environment
codec. Scene/group, day/night, release defaults, Save/Reload, group standards,
hot reload and interpolation are the same as for the other environment fields.
User files use `environments/<title namespace>/`; the legacy title's namespace
is empty. Point groups do **not** depend on `Use scene environment`.

Graphics Authentic/Enhanced/Toon/Custom/Enhanced-for-Mobile presets choose a
lighting model, not another copy of the point controls. Modern models consume
the selected environment's point settings. Native/NativePBR/Authentic and F2
retain their existing admission policy; changing presets does not erase the
saved environment. Default point controls are enabled, unit intensity/radius,
white tint in the base and shipped group profiles. Authoring remains in those
profiles, not `Graphics` JSON or a second fairy-settings file.

## Off and zero intensity

An identity-proved renderer-owned light must not survive through its native
per-draw direct/specular contribution when its group is disabled. Vertex-native
lighting already suppresses that matching direct term independently of Enabled.
The native-fragment ledger now does the same: after validating the exact source
and hardware slot, Off/zero suppress direct/specular, retain independent native
ambient, and publish no replacement point/shadow row. Enabled groups retain the
native radiance/LUT/material response and apply their controls once.

This supersedes the historical native-fragment **“Off keeps native”** policy in
earlier qualification notes. Off means **this added-light group off**, not a
shortcut to the Native lighting model. Unknown, contradictory, duplicate and
untracked owners still fail closed and stay native. Supplemental emitters have
no native term to suppress. No shader source, packed ABI or legacy family is
changed by this correction: existing zero-gain recomposition is used.

Owning regressions: pipeline-cache group-mask/zero/proof tests; production
scene-point and installed-GGX GPU probes; real F1 controls/save/reload/presets.
