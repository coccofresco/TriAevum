# TriAevum PICA service bridge

## Implemented path

The module ABI now describes the CTR GPU operations consumed by the existing
frontend rather than a renderer-specific draw object:

- hardware-register writes, with optional masks;
- GSP command packets and inline command-list words;
- framebuffer selection and LCD force-black state;
- GPU interrupt retrieval;
- frame-sequence flush.

`PicaHostServiceAdapterV1` performs bounded decode, copies only command words
needed for alignment, and dispatches a title-neutral C++ backend. Variable
texture, vertex and framebuffer resources are not embedded in every request;
the backend obtains them through checked guest-memory leases.

`PicaServiceClientV1` is the symmetric module-side encoder. It validates the
host ABI, bounds every variable payload, owns request storage for the complete
call and rejects malformed responses. `NativeA32CtrPicaBridge` is a small
renderer-independent process contract; `TriAevumOot3dPicaClientBridge` maps
the existing CTR host's register writes, command queue, native framebuffer,
LCD state and interrupts onto that client. The linked whole-AOT oracle keeps
using its direct frontend path, so module transport can be compared without
changing oracle behavior.

`TriAevumOot3dPicaServiceBackend` is the thin title adapter. It forwards
register and GSP operations to the existing `Oot3dNativePicaFrontend`, returns
deferred-transfer flags, exposes native interrupt IDs, and leaves final flush
as an injected host callback. It contains no scene or material policy.

`PicaScanoutStateV1` implements the common CTR display state: two screens, two
framebuffer slots per screen, the selected slot and LCD force-black. The
assembled host binds framebuffer service requests to this state. The OoT3D
renderer adapter presents a transfer only when its native output address is
selected for the top screen; force-black suppresses presentation without
discarding GPU work or its completion interrupt.

`BuildTriAevumOot3dPicaPhysicalMemoryView` converts the validated TAM mappings
into the existing submission-queue memory view. Reads acquire bounded module
leases and expose their content versions; injected begin/end hooks release the
scope around each synchronous GSP command. No physical address is supplied by
the public runtime and no guest pointer is retained by a queued frame.

`TriAevumOot3dPicaHost` assembles that path after `RuntimeSession::Load` and
before `RuntimeSession::Initialize`: it owns the lease pool, physical view,
submission queue, frontend, backend and typed adapter. Its single registration
method supplies the PICA service declared by the module, while
`TakePendingBatch` exposes renderer work without importing NRI into the module
or service ABI.

`TriAevumOot3dPicaBatchDispatcher` validates the atomic queue drain before
submitting any work, then preserves the native ordering among memory fills,
draws, display transfers and their completion IDs. A failed partial submission
faults the dispatcher instead of replaying GPU work.

`TriAevumOot3dPicaRenderer` is the thin title-to-renderer adapter. It converts
draws with the existing native PICA planner, submits all operations through the
title-neutral `PicaRenderBackend`, and maps opaque backend completion tokens
back to native GSP interrupt IDs. Vulkan therefore waits for its frame fence;
OpenGL may acknowledge once it has copied all guest spans into backend-owned
resources. Completed interrupts enter the PICA service queue and are returned
to the private module by `TRIAEVUM_PICA_TAKE_INTERRUPTS_V1`; no A32 relay-queue
write is performed by the public runtime.

## Verified evidence

The isolated tests cover all generic service operations, reject malformed
batch dependencies before submission, assert exact CTR operation order, and
exercise the NRI completion-to-interrupt round trip with a conformance backend.
A module-to-host round-trip test covers every PICA client operation, including
deferred submission flags and hostile response validation. A second isolated
test verifies the OoT3D CTR process bridge against the same service adapter.
A second test was compiled and linked against the same native PICA frontend,
submission, planner and bridge libraries used by the validated whole-AOT
product. It proved that ABI requests update real frontend state and that the
new adapter uses the established draw/transfer/fill conversion path. The
scanout tests additionally cover slot switching, left/right address selection,
invalid requests, force-black, and the complete module-service-to-NRI
presentation decision.

## Remaining frame integration

The renderer transport and generic scanout selection are implemented, but this
is not yet a module-produced window frame. The next vertical slice must:

1. call `DrainPending` inside an active NRI frame and poll delayed Vulkan
   completions on following frames;
2. compare one module-produced frame with the linked whole-AOT oracle.

TopScreen suppression, HUD layout and diagnostic capture are deliberately not
part of this adapter. They remain independently selectable presentation policy
above the native scene stream.
