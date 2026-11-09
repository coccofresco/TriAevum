# CATALOGS: headless preparation and cache handoff (local only)

Continuation of the observed-subset shader qualification, source runtime
`c7d57e76`. This is **not complete MM3D parity**, a supported Forge recipe,
or whole-title shader coverage. No renderer/shader/profile changes in this
slice. The coordinator approved a lane-local lock-owning headless launcher.

## Reproduction and ownership

`tools/mm3d/qualify_catalogs_preparation.py` invokes the actual Python Forge
`prepare_device_pipelines` host exactly once, using hash-bound copied helper,
portable pack, pipeline manifest and optional title vertex/pass pack. It checks
the retained runtime source receipt before launch, refuses an existing output
attempt, uses four preparation workers, disables crash dialogs, and checks the
owner's I: 8 GiB floor before taking the lock and again before launch.

The launcher acquires `<work>/gpu-run.lock` with mkdir and a unique
owner token, waits boundedly, and releases only its own token. It never reclaims
stale or foreign locks. Signal handling unwinds the Forge subprocess owner,
which kills/reaps its helper before the lock is released. The six launcher
unit tests use temporary **fake** lock paths; they do not touch the shared lock.
Do not nest this launcher inside another lock-owning runner.

Inputs are explicit CLI arguments: `--helper`, `--pack`, `--manifest`,
`--title-program-pack`, `--runtime-inputs`, `--output`. See its help. A new output
path is not permission to repeat an acceptance test. No compiler is launched
on the Forge user path; publisher compilation remains separate.

## Exactly two GPU executions

1. **Headless acceptance, no game boot:** RTX 3060, empty application cache,
   411/411 prepared = 165 startup parts + 246 observed recipes. Failures,
   retries, recovered failures and library fallbacks all zero. 375.331 seconds
   total, 354.834 startup / 15.016 recipes, four workers, peak working set
   1,757,446,144 bytes. Cache payload 92,848,619 bytes. Validation was **not
   requested**; the zero error counter is not Khronos validation qualification.
2. **Required cache-consumer smoke:** one 180-frame Stock Pot Inn MM3D run
   through the private lab run harness (not shipped), exit 0 / 18 seconds. Actual renderer accepted exactly
   **92,848,619 cache bytes**, loaded 23 title programs and 145 room emitters,
   prepared 165 startup parts with zero failures. Strict portable resolution:
   72 hits / zero misses. Specialized 25,001 + uber 379 draws, zero completed
   draws, all unavailable counters zero. NRI failures/fallbacks zero.

Smoke startup 1.848 seconds, recorded pipeline creation 10.052 seconds total.
These are attribution, not an FPS improvement, zero driver compilation,
cold-driver or whole-title hitch-free claim. No warm-helper confirmation run,
OoT rerun, installed-product run, or broad capture campaign was performed.

## Image boundary and provenance

The Inn screenshot was inspected: normal room, scene and native HUD visible.
Its raw BMP SHA differs from the retained wave-3 same-frame screenshot despite
equal saved JSON settings and unchanged runtime source inputs. Lossy JPEG-only
mean absolute differences are approximately 0.49–0.55/255 per channel;
1.38% of pixels differ by more than four in any channel. Async specialized/uber
readiness differs in aggregate counters, but is **not proven causal**. The
cache handoff is accepted; exact image equivalence is **not**. No retry hid this
finding. Both original BMP hashes and JPEGs are retained; new raw BMP and
large diagnostics were removed after extraction.

- Helper SHA256: `4af03723eda5b0dec3da169aa402e95c8cf4399ca68fbf7a2e2c28065038fb70`
- MM executable: `a99cb4b8df4cb4ee2ad97e8950596e9e1ff019cc548acd5d39a2c0c210b6e009`
- Portable pack: `b5f8bc9e4fe449da6cccefa9f1fcb5bd9af93aa61230cbf2991bf9db59944256`
- Title pack: `6a0ded2375662e75988240ea645dc79611849b254963758375054684b3ce2818`
- Helper-produced cache before smoke: `8ffc0c7e93dcbcbb8b7b242c8f2109af4fe85dc3e11659491bae17b6bd63468c`

Own `configure-catalogs.cmd` / `build-catalogs.cmd`, JOBS=4, registry v10,
explicit `oot3d;mm3d`, private patched `nri-src-catalogs`. The separate owned
pipeline helper was rebuilt after the previous vertex-pack regeneration.
First configure invocation failed due to shell path quoting before CMake;
correctly quoted invocation/builds passed, before any GPU launch.

Evidence: `W/analysis/parity/CATALOGS/wave4/`: `headless/{inputs,result}.json`,
`smoke-attempt/inputs.json`, `smoke-summary.json`, `image-comparison.json`,
launcher/preparer/Forge test logs and build receipts. MM build stays dual-title.
Earlier O2 cap-p13 byte identity remains historical wave-3 evidence: this
slice changes no runtime, shader, OoT data or protected GP values.

## Closure limits

Closed **subgate** of F05/G18: real Forge headless preparation of the declared
observed family, followed by verified cache acceptance in MM gameplay.
G07/F05 supported MM revision/whole-AOT module and complete asset preparation
are still absent; this verified helper contract is not a replacement recipe.
The native Rust Forge route retains its earlier unit/oracle coverage; this
particular real helper launch uses Python Forge, not a new native install.

Still open: exact smoke image equivalence; broad shader/material/effect census
and LineShader geometry translation; static-shadow eligibility with actual
all-variant stability/interaction evidence; complete rigid opaque room-shell
producer/setup identity and Forge interior masks; remaining emissive analogues,
positive material/reflection/underwater qualification; coordinator integration
and product/platform/owner-look acceptance. Do not publish the observed pack as
complete, add guessed static IDs, turn unknown fullbright materials into lights,
or reuse header fingerprints as full room geometry identities.
