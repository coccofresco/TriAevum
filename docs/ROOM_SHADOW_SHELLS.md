# Interior sun / offline

`shadowCasterFaces` accepts `native`, `both`, and `interiorShells`. The base
modern environment still defaults to `interiorShells`. F1 calls it **Interior
sun / offline**, under Lighting > Lights & Shadows > Scene lights > Shadows.
The JSON spelling is retained for compatibility. Legacy `shadowCasterCulling`
keeps its meaning (`true` = native, `false` = both); the new field takes precedence.

Forge computes room masks from the user's own RomFS during installation. The
shipped runtime has an offline tool mode; no title plugin, guest process, GPU,
window, compiler, or SDK is needed. Missing or invalid caches use the same
shared builder in a lazy runtime worker. The release ships only a mask table
for the supported rooms (below); no geometry is packaged.

## Authored indoor/outdoor classification

The OOT3D adapter reads the **selected setup's authored commands**, independently
of mutable live environment flags:

| Room command 0x12 byte 5 | Scene command 0x11 | Classification |
| --- | --- | --- |
| Sun/moon disabled, nonzero | Any | Indoor |
| Sun/moon enabled, zero | Environment mode (byte 6) = 0 | Outdoor |
| Sun/moon enabled, zero | Mode = 1, skybox ID (byte 4) = 0 | Indoor |
| Sun/moon enabled, zero | Mode = 1, sky family 1/4/5/0x1d/6/8 | Outdoor |
| Missing, malformed, or unknown | Otherwise | Outdoor |

The EUR adapter reads the scene resource base at `PlayState+0x110` and selected
setup at `SaveContext+0x4e8` (`0x588e40`). Command 0x18 chooses the relative
alternate header. A null alternative continues the base header; null setup 3
first tries setup 2, matching native handler `00378ff8`. Traversal, pointers,
setup counts, and cycles are bounded. The offline tool uses this same parser
for every advertised setup, including standard and Master Quest room files.

Indoor rooms receive interior-sun masks. Outdoor and unknown rooms keep all
opaque geometry casting from both faces, with zero masks and no voxel work.
Unusable geometry retains native two-sided casting. There are no scene-name
exceptions. Class is explicit in the cache header, filename and published
snapshot; a mesh from the other class cannot become ready.

## Shared version-3 builder

1. Extract complete rigid opaque room geometry. Bind transforms use
   `Rz * Ry * Rx * S` and the parent chain. Weld exact world positions while
   retaining triangle order. Alpha-tested/blended geometry stays native;
   unsupported opaque skinning or topology prevents partial replacement.
2. Voxelize at **12 world units**, with a 23-voxel border. For each triangle,
   sample the barycentric lattice with `k=max(2,ceil(longest_edge/4.8))`.
3. For closing radii **2,4,...,20**, dilate solid with six-neighbour connectivity,
   flood the exterior from the grid border, and regrow that exterior by the
   same radius. Interior is neither original solid nor regrown exterior.
   Choose the largest consecutive relative increase in interior volume,
   dividing by `max(previous_volume,1)`. First tie wins; the first candidate
   is radius 4. Capped Manhattan distance transforms implement the exact
   six-neighbour dilations; padding makes a corner flood equivalent to seeding
   every border face.
4. For each triangle and each of **64 Y-up Fibonacci directions toward the
   source**, sample the centroid and `(2*centroid+corner)/3` for each corner.
   These are one-third toward the corners, following the approved Python
   code (the trial's prose described them differently).
5. March each sample toward the light, starting at **24 units**, stepping by
   **6 units** until outside the grid. Record whether it ever meets an interior
   voxel. Set the triangle's drop bit only if **at least 3 of 4 samples never
   enter**. Once a source ray has entered, later exits/reentries never reset
   admission. Winding and native face culling do not change this criterion.

For sector `s`, `y=(s+.5)/64`, `a=s*pi*(3-sqrt(5))`, and
`d=(sqrt(1-y*y)*cos(a), y, sqrt(1-y*y)*sin(a))`. The renderer chooses the
maximum-dot sector and degenerates masked triangles in its cull-none vertex
shader. Mesh checksum, sector and raster bias invalidate static shadow pages.

Bounds are **500,000 input triangles**, **64 Mi voxels**, **128 Mi voxelization
samples**, and **512 Mi ray steps** per build. The grid's maximum allocation is
seven bytes per voxel (three byte grids and one u32 flood queue), at most
448 MiB, plus bounded geometry/encoding storage. Cancellation is checked during
voxelization, distance passes, flood fill and tracing. Bounds/cancellation return
no replacement mesh, preserving native two-sided casting. If no interior is
detected, a valid zero-mask mesh is published conservatively; this deliberately
differs from the trial's all-drop result for a wholly empty interior volume.

This implements the approved per-room triangle-mask trial. It does not add
inter-room admission domains or receiver-specific admission pages from the
longer-term design discussion. Openings smaller than the chosen closing radius
may be sealed; geometry within the 24-unit sample start remains subject to the
trial's sampling approximation.

## Offline invocation and Forge receipt

```
TriAevum.exe --prebuild-interior-sun --romfs <directory-or-image> \
  --renderer-cache-directory <renderer-cache> --manifest <result.json> \
  [--mask-table <interior-sun.masks>] [--export-mask-table <path>]
```

Rooms are read and fingerprinted in inventory order; each distinct content and
class is then prepared once, largest first, on up to six workers (half the
logical cores). A cold run of the supported RomFS took 235 s instead of about
13 minutes on the 12-thread reference PC, with byte-identical caches.

**Mask table.** The release computes every interior room once and ships its
masks, keyed by the content fingerprint below: `"TISP"`, u32 format 1, u32
criterion version, u32 entry count; per entry u64 fingerprint (strictly
increasing), u32 triangle count, u32 kind (0 masks, 1 the work/memory bound
fallback), u64 first mask; the u64 masks; then the u64 FNV-1a checksum below of
everything before it. It holds no positions or indices. A room found in the
table is welded from the user's RomFS and takes its masks (`precomputed`) when
the welded triangle count matches; otherwise it is built. A known bound fallback
is reported without repeating the work. With the table the supported RomFS
prepares in about 2.4 s, byte-identical to a local build. `--export-mask-table`
writes the table of a run (publisher step:
`python -m tools.triaevum_release.precomputed_tables`).

The mode accepts a RomFS directory containing `scene/`, a raw Level-3 image,
or an IVFC image with Level-3 at 0x1000, matching Forge's existing RomFS inputs.
It reads bounded metadata/payloads, enumerates standard and `_dd` room files,
resolves scene and room alternatives together, and writes caches atomically.
The JSON manifest records each room/setup, class, cache path, build/hit/fallback,
triangle count, time, chosen closing radius, interior voxels and peak grid bytes.
Repeated identical identities reuse the first result. Missing/unsupported
geometry or a work bound produces an explained native fallback. Invalid input
images, failed cache writes and failed manifest writes return a tool error.

`tools/triaevum_release/interior_sun.py` invokes the integrity-checked catalog
runtime through `native_process.run_native`, preserving the frozen-app DLL
search/environment handling and hidden-window convention. The stage runs before
transactional activation and uses the same renderer directory passed to the
game. Receipts under `data/interior-sun/` bind **actual RomFS SHA-256, criterion
version 3, runtime SHA-256 and cache directory**. Reuse verifies manifest identity,
cache headers and every output SHA-256. Missing, altered or old outputs rerun the
tool. A failed process cannot publish a successful receipt. No compilation path
or packaged ROM-derived asset is introduced.

## Cache format and identity, version 3

Files live under `${TRIAEVUM_RENDERER_CACHE_DIR}/shadow-shells/`:
`<16-hex-fingerprint>.bin` for indoor and `<fingerprint>-exterior.bin` for outdoor.
An empty renderer directory disables persistence but permits lazy memory builds.

The OOT3D key starts with the legacy FNV hash of command bytes `[0,128)` (file
`[0x10,0x90)`). Nonzero selected command offsets are folded into a 13-byte key:
base hash as u64, offset as u32, and byte 3. The resulting value seeds hashing of
each captured CMB's u64 length and bytes, in relative-address order. Thus rooms
with identical headers but different standard/MQ CMB content cannot share masks.
The installed room's relocated mesh entry-table pointer is handled separately;
command arguments and CMB offsets remain relative to the resource base.

All fields are little-endian. The **40-byte header** is `u32 magic` (`RSSH`),
`u32 version`, `u64 fingerprint`, `u32 sector count`, `u32 legacy raster size`,
`u32 vertex count`, `u32 triangle count`, `u32 interior` (0/1), `u32 reserved` (0).
Payload is float32 XYZ welded positions, three u32 indices per triangle, u64
masks, then a u64 checksum of all preceding bytes. The FNV-1a basis is the
intentionally nonstandard **1469598103934665603**, prime **1099511628211**.

Reads check version, identity/class, exact length, checksum, finite coordinates,
counts and indices; outdoor masks must be zero. Versions 1/2 are rejected and
rebuilt. The legacy raster field remains 512 for runtime compatibility but is
not used by the voxel algorithm. Writes use the renderer's atomic cache helper.

Only loaded room requests and one active worker build remain in memory; retired
queued work is dropped. Runtime request identity also includes source address
and scene scope. Immutable snapshots carry class through the light binding.
Replacement is atomic across loaded rooms because native draws have no individual
room provenance. Pending, failed or mixed snapshots retain native two-sided
architecture. Actors, foliage/alpha draws and contact shadows retain native
geometry. Enabled PICA blend with ADD and ONE/ZERO is treated as opaque.

## Validation and game lab

- `renderer3ds_room_shadow_shell_tests`: roof and inner platform/stack, separated
  volumes with leave/reentry, winding independence, authored outdoor zero masks,
  cancellation/bounds, v3 corruption/persistence/class checks and GPU shader.
- `oot3d_native_room_shadow_shell_tests`: CMB parsing, synthetic selected headers,
  setup-3 fallback/cycles, class changes, publication, standard/MQ inventory,
  directory/raw-Level-3/IVFC equivalence, malformed images and cache reuse.
  Optional scene-directory argument validates user ROM geometry; developer
  `--compare-trial <approved-v2-cache>` compares all 64 bits, and
  `--benchmark-room <room.zsi>` measures fresh builds using authored classification.
- Forge `test_interior_sun.py` and `test_precompiled_titles.py`: receipt reuse,
  corruption/missing-output regeneration, RomFS/runtime/version invalidation,
  failed tool/malformed manifest and shared cache routing before activation.
- `triaevum_f1_settings_smoke --scene-environment`: real mode-label selection,
  preview/publication, defaults and persistence.

In the game lab, check Deku Tree admission and inner platforms against the approved
trial; Zora's Domain, Temple of Time and Shadow Temple; Forest Temple courtyards
and Hyrule Field remaining two-sided; alternate setups/Master Quest; room
transitions with both loaded rooms; and first launch showing `event=cache_hit`
for prebuilt rooms. Remove one disposable cache and verify one lazy rebuild.
Use a separate lab profile/cache; no play-kokiri settings are required to change.

`TRIAEVUM_INTERIOR_SUN` tool logs report offline progress. Runtime
`TRIAEVUM_ROOM_SHADOW_SHELL` logs report request, cache hit/miss, build and write
outcomes. Existing shadow-pass diagnostics report ready/pending snapshots,
replacement counts and ownership rejection reasons. Developer `--guest-room`
accepts a u32 guest base plus an installed room span, without running the title.

## Round 29 qualification

The twelve approved Deku rooms agree at **100% each** (0 differing bits out of
767,808), including the actual caches produced from raw RomFS. The full local
RomFS pass processed 610 standard/MQ rooms and 1,185 room/setup entries, writing
879 caches. Sixteen distinct builds (23 room/setup entries) exceeded the voxel
bound and retained native two-sided casting: `ganon_demo_0` setups 0..3;
`hakadan_21` standard/MQ setup 0; `kenjyanoma_0` setups 0..6; `spot00_0` setups
4..6; `spot02_1` setups 4/5; `spot08_0` setups 1..3; `spot13_1` setup 0;
`spot16_0` setup 8. Ordinary field setup 0 and Forest Temple courtyards classify
outdoor and allocate no volume. Counters for built/hit/fallback count distinct
identities; `entries` also includes duplicate room/setup references.

Three-build median timings (ms), measured alongside another prebuild worker:
Deku 0 1415.620, Zora 1 7823.609, Temple of Time 0 5561.096, Shadow Temple 0
618.278, Hyrule Field 0 2.022. Their peak voxel counts are respectively 4,700,184,
18,618,228, 16,246,604, 1,563,510 and 0. The largest grid over the full inventory
was 48,634,290 voxels (324.67 MiB of grid buffers). A cold full pass took about
13.5 minutes; the actual populated-cache Forge tool stage took 6.73 seconds and
receipt reuse 1.85 seconds. A saved installed Deku room hit its offline cache.
