# Source-native boot contract overlays

These overlays are narrow, address-owned corrections for source functions whose
baseline C types do not preserve reviewed target semantics. They are linked over
the pinned baseline through the target registry and its host-symbol interposer.

Guest functions whose historical names collide with the host C runtime also
receive a distinct address-owned implementation. Host calls remain on their
standard ABI; only target-address dispatch uses the guest contract.

`overlays.json` records the guest owner, replacement symbol, exact evidence
revision, and reason for each correction. The isolated closure builder compiles
only these files; adding an overlay does not rebuild the 1065-source baseline.

Direct calls from the imported process entry are bound to their validated owner
at compile time as well as through the target registry. This is required when a
baseline declaration lost target arguments: target-address interposition alone
cannot change a direct host linker reference.

Target helper objects retain their target widths even when the host ABI uses
64-bit pointers. In particular, a scoped-lock guard stores one `u32` guest
address. Widening that field to a host pointer overwrites adjacent target or
native stack storage and violates the caller's nonvolatile-register contract.

The overlay registry also owns reviewed indirect entrypoints that are present
in original vtables but absent from the baseline Ghidra function index. Their
original ARM bytes and the pinned decompiled owner must both identify the same
contract; an unresolved address alone is not sufficient evidence for adding an
entry.

Return-value overlays preserve constructor and observer chaining when an older
decompilation declared a target function as `void`. The replacement must retain
the full reviewed body as well as its return contract; coercing the stale host
function's incidental return register is not a valid repair.

The pinned baseline also predates the reviewed distinction between callback
values and addresses of 32-bit virtual-dispatch slots. Its resolver therefore
accepts a slot only after direct lookup fails, the slot is readable through the
active guest address space, and the contained target is already registered.
Current source owners must still use the explicit `OOT3D_INDIRECT_SLOT_CALL*`
contract; this compatibility path exists only for unpromoted baseline owners.

When a later reviewed revision closes a function family that was absent from
the pinned target index, all entries in that owner are imported together. This
keeps vtable reachability complete and avoids treating each missing virtual
entry as an unrelated boot failure.
