# TriAevum title-neutral runtime session

## Implemented boundary

`RuntimeSession` is the first executable orchestration layer shared by future
3DS recompilations. It owns a verified `game.tam`, an immutable host-service
registry, private content-index bytes and module lifecycle. It does not include
OoT3D addresses, assets, scene policy, SDL, NRI or an audio device.

Startup is deliberately ordered:

1. read a bounded private `content.tap` without exposing its path to title code;
2. authenticate and semantically validate `game.tam`;
3. compare every metadata-declared service with registered host adapters;
4. seal the service registry;
5. initialize the module and retain all callback/input storage for its lifetime.

The session then exposes frame execution, portable state save/load and clean
shutdown. It also mediates checked guest-memory leases for host adapters; title
memory remains owned by the module. Missing services fail before title initialization. Service IDs and
schemas are generated into the TAM by Forge and cannot silently depend on
current C++ renderer classes.

## Integration sequence

The generic runtime target is not yet a playable executable. Complete it by
adapting existing implementations in this order:

1. PICA command submission to the existing NRI renderer;
2. abstract HID snapshot and scheduler/kernel requests;
3. content/save filesystem operations over `content.tap`;
4. PCM submission to the existing audio output;
5. window, presentation timing, configuration and diagnostics around the
   session.

Each adapter remains in the public runtime. The private module owns title CPU,
memory, scheduler state and translated functions. The linked whole-AOT product
stays as the regression oracle until the module path matches framebuffer, PCM
and save-state evidence.

## Verification

The native loader test now runs both the low-level API and `RuntimeSession`
against a dynamically loaded mock TAM. It proves that a declared missing
service blocks startup, a registered service permits frame execution, metadata
remains available, and shutdown unloads the running session.
