# OOT3D native startup closure

This directory is a narrow, revision-pinned import from Zelda3drecomp commit
`8f00bb640074dc8fbbe3f144406fc87a8b4dd445`. It contains the target-word-lowered
sources and direct headers needed to enter the decompiled game through
`oot3d_process_entry` instead of bypassing process startup with a direct
`nnMain` call.

The files are immutable inputs. `build_startup_closure.py` verifies every hash,
compiles only the four C units through `Oot3dGuestStorageLegalizer`, and emits a
small host archive plus only the absolute guest-data bindings not already
provided by the pinned base archive.
