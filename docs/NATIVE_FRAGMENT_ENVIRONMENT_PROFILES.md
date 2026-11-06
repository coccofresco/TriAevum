# Environment profiles on native fragment lighting

Design, 2026-11-06 (P5.1). The profile runtime, codec, precedence, day/night blending,
preview/save and release-resource shipping are shared with vertex-lit titles.
No guest address, actor/scene identity or image-derived value enters this mechanism.

## Boundary and equation

Only full scene-lighting world surfaces, with the native-light capture hooks and a
valid layout, enter the balance variant. Native, Native+PBR, Authentic, F2,
native-memory, isolated/atmosphere/UI and unsupported water draws keep their existing
routing. Unknown/unproved native source ownership is Local, never Scene.

Capture the native per-slot diffuse, ambient and specular **before** its clamp.
For proved environment Scene slots, scale direct by `nativeDirectStrength` and
ambient by the resolved native ambient gain. Local/Unknown slots keep their entire
native response at `nativeLocalStrength`. Global ambient uses the relative ambient
gain (one when Preserve scene brightness owns the baseline). Preservation retains
ambient, not a second opposite directional fill. Never infer a baked surface by
zeroing a directly lit source.

Add the shared material-weighted ambient, key/fill, placed/scene-point and rim
responses to that ledger, once. Key colour/direction use the existing scene-main,
scene-ambient or manual resolvers (including offsets and the horizon rule); only
the key receives its directional shadow. A scene-following key is admitted only
when the draw carries a proved Scene directional: a room lit only by local lamps
must not acquire the scene's actor key. Manual keys, fill, ambient, rim and placed
lights remain explicit additions even on local-only draws. Native Scene terms are
not uploaded again as additive scene directions. Scene points already present as
native sources are excluded by identity, not position/colour; their renderer-owned
replacement and native point-shadow receiver are the dynamic-light lane's scope.

Ordinary native-fragment surfaces use the existing material diffuse/ambient
coefficients, not the baked outdoor ratio. The TEV's later vertex-colour/AO/tint
multiply and native alpha/coverage replay are untouched. Native-fragment PBR material
replacement retains its existing dedicated evaluator (not a second balance overlay).

Independent rim and scene-light rim use the existing surface helpers and eligibility.
Fog strength travels through the existing environment uniform after lighting, not
through native guest fog edits. Placed light kinds, ranges, cones, area forms and
point-shadow assignment reuse the shared evaluator, not a title-specific substitute.

## Inertness and tests

The appended balance payload keeps the legacy surface block size and descriptor
range unchanged. Shader text changes are confined to the balance variant; legacy
plain/PBR/uber/pass sources stay pinned. An exact identity bypass is permitted only
when every retained native term is unit gain and every added term is zero.
Tests cover fractional and above-unit gains, every profile shape, local-only and
unproved slots, no duplicate directions/points, key fallback, ambient-only changes,
rim/fog, off/capability/water routing and canonical source hashes. GPU probes should
exercise the production generated balance body, not a duplicate shader equation.

## Content

Titles ship `environments/<namespace>/group-*.environment.json` using the existing
resource-root loader (user scene > release scene > user group > release group >
title fallback; night falls back to day). A missing file, not a second hard-coded
profile selected ahead of files, is the only reason to use the title fallback.
Group values are starting proposals, with owner look review still required.
