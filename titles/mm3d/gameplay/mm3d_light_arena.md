# MM3D light arena: EUR v1.1 contract

This is the H8 allocation-safety prerequisite, **not complete actor/foliage parity**.

## Independent evidence

Original image SHA256: `6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d`.

`GameState_Update` initializes the arena every native graphics epoch:

- `0x1067B4`: loads literal `0x106B90 = 0x72F91C`.
- `0x1067BC..0x1067D8`: reads `gfx+0x60`, selects low bit, computes
  `base + bank * 255 * 512`, then calls `0x1393A0(gfx+0x40, base, 0x19C00)`.
- `0x1393A0..0x1393B4`: stores `{size, base, head=base, tail=base+size}`.
- Thus banks are `0x72F91C` and `0x74F71C`, each 412 records of 256 bytes.
- `0x1EFC7C..0x1EFCFC`: allocator reads `gfx+0x4C`, subtracts `0x100`,
  stores tail, fills ambient light data. Both ambient-select branches store tail.

Read-only retained-state corroboration:

| State | Bank | Head | Tail | Native records used |
|---|---|---|---|---|
| sct_day1_noon | 0 | 0x72F91C | 0x74891C | 12 |
| sct_day1_night | 1 | 0x74F71C | 0x76931C | 0 |
| nct_day1 | 1 | 0x74F71C | 0x76721C | 33 |

The earlier MAP_3 arena/reset unknowns are resolved by this disassembly and these
states. A single-bank assumption was rejected offline; overflow must preserve
native double-buffer lifetime, not reclaim all records at every Play_Draw.

## Implementation

`mm3d_light_arena.h` checks allocator, initializer, reset, literal and both store
paths. These are **data proofs**, not new observer PCs. The existing title-session
allocator observation runs overflow planning before the lighting binding observer;
there is still only one registered lighting site at `0x1EFC7C`.

The shared `tools/three_ds/downward_arena.h` only plans cursor transitions. MM owns
all addresses, sizes and policy. Existing OoT logic is unchanged.

Renderer EDD enables relocation. The two overflow banks occupy disjoint intervals
of one private mapping, with a header and inter-bank gap. Each has 2048 records.
No wrap occurs: exhaustion or unavailable storage stops execution rather than
aliasing live light records. Mapping extent, permissions and identity words are
rechecked after restores. A partially overlapping/foreign mapping is never removed.
Native cursor resets reclaim the corresponding bank; turning EDD off completes an
in-flight overflow epoch without new relocation. No gameplay timing changes.

## Qualification limits

Tests exercise all guards (corrupted/unmapped words), original image verification,
412 native allocations per bank, all 2048 overflow slots per bank, exhaustion,
wrong-bank cursor, head growth, alignment, native resets, EDD changes and failures.
The product executable compiles with developer tools OFF and both titles enabled.

This does not add actor culling rewrites, foliage callers, object slots, or a list
of auxiliary light-allocating actor callbacks. Runtime transition and stress
acceptance, including an actual overflowing scene, remain separate gates.
