# TriAevum module ABI and TAM format

## Purpose

`game.tam` is the private, locally generated code module consumed by the
redistributable runtime. It is title-neutral at the format level and is never a
public release artifact.

## Container v1

All integers are little-endian. The canonical file contains:

1. a 64-byte `TRIAEVM1` header with format version, required runtime ABI,
   section count, exact file size and section-table offset;
2. fixed 64-byte section descriptors containing type, flags, offset, size and
   SHA-256;
3. zero alignment padding and section payloads aligned to 16 bytes.

Version 1 requires exactly one UTF-8 JSON metadata section and one platform
native-image section. Compression is not supported. The reader rejects unknown
required sections, unsupported flags, overlap, nonzero padding, duplicate or
missing required sections, hash mismatch and unclaimed trailing bytes.

The metadata records recipe, target triple, source/translator identities,
native-image identity and query symbol. It explicitly marks the module private
and non-redistributable. The loader parses this as bounded JSON, rejects
duplicate fields and invalid UTF-8, and verifies its ABI, platform, image size
and image hash against the already authenticated container. After dynamic
loading, the exported module identity must equal the metadata source identity.
The metadata also declares unique, versioned host-service requirements, which
`RuntimeSession` resolves before calling title initialization.

## C ABI v1

`runtime/triaevum_module/include/triaevum/module_abi.h` is C-compatible and
uses size/version fields for forward compatibility. A native image exports
`TriAevumQueryModuleV1`, which returns callbacks for initialization, frame
execution, shutdown and portable-state serialization.

The host API intentionally exposes logging, monotonic time and a versioned
service invocation boundary rather than renderer or title classes.
`service_abi.h` now defines the first stable, pointer-free schemas for
kernel/SVC dispatch, PICA command submission, PCM output, content/save
filesystem access, abstract 3DS input and cooperative scheduling. Every
request and response carries its own size and schema version; variable data is
an inline byte range rather than a process pointer.

`HostServiceRegistry` binds those IDs to runtime implementations, seals before
a module receives its host API, rejects duplicate and unknown services and
checks response-size claims. This registry is title-neutral. Existing NRI,
audio, filesystem and input implementations remain behind adapters and do not
become part of the module ABI.

The PICA schema follows native CTR/GSP operations and its tested adapter path is
documented in `TRIAEVUM_PICA_SERVICE_BRIDGE.md`.

The input v1 schema defines one canonical 3DS digital-button layout, normalized
left/right sticks and touch coordinates, an explicit touch-pressed flag, and
gyroscope/accelerometer samples in physical units. Both service endpoints
reject unknown bits, non-finite values and incoherent touch state. The desktop
poller remains outside the module and may use keyboard, mouse or controller
profiles without exposing SDL codes through the ABI.

The filesystem v1 schema exposes only two logical roots: immutable prepared
content and mutable save data. Its pointer-free operations cover open, bounded
read/write, close, stat and resize. The host maps content names from the
authenticated `content.tap` and confines save paths below one canonical root;
absolute paths, drive prefixes, traversal and writes to content are rejected.
Consequently, a private native module does not receive or open host paths even
though the local non-redistributable index records them for the host.

Modules requiring PICA declare every physical-to-guest memory alias in the
attested metadata. Each region carries a 32-bit physical base, 32-bit guest
base and nonzero byte count. Forge canonicalizes the list; both Forge and the
runtime reject overflow, physical overlap and guest overlap before module
execution. The values come from the locally translated process image rather
than title constants in the public runtime.

Large PICA resources are the deliberate exception to pointer-free message
payloads. The module retains ownership of its 32-bit guest address space and
offers checked, lease-based mappings through `map_guest_memory`. A mapping
reports access rights, size, content version and a nonzero token; the runtime
must release it and report writes. The pointer is valid only until that token
is unmapped and may never be serialized or retained by a queued frame. This
lets the renderer inspect vertex, texture and framebuffer data without copying
the entire resource through every service request.

Framebuffer selection itself remains pointer-free. `PicaScanoutStateV1`
retains the standard two-screen, two-slot CTR state decoded by the PICA service
and exposes only the currently shown addresses to presentation. It is shared
host infrastructure rather than an OoT3D rule; LCD force-black suppresses
scanout while preserving transfer execution and completion ordering.

`GuestMemoryReadLeasePoolV1` is the generic host-side implementation for a
single synchronous service scope. It maps only requested ranges, reuses exact
ranges, exposes their content versions to renderer caches, caps concurrent
leases and releases every token in reverse order. No pointer survives the
scope or enters a queued frame.

No native image is loaded until the canonical container, all section hashes and
the runtime ABI have been validated. The loader maps the TAM instead of copying
the complete module into memory, extracts only verified native-image bytes into
a content-addressed private cache, restricts dynamic-library lookup, resolves
only the documented query export and validates every callback before
initialization. Neither malformed metadata nor a correctly hashed container
wrapped around the wrong native image can reach module initialization.

Runtime startup has an explicit two-phase path. `Load` verifies and exposes the
module metadata without executing title code; the host then constructs and
registers exactly the declared service adapters. `Initialize` seals that
registry, checks every required service and only then enters the private
module. `Start` remains the convenience form for hosts whose services do not
depend on module metadata.

## Verification

The module library is independently buildable:

```powershell
cmake -S runtime/triaevum_module -B <build-directory> -A x64
cmake --build <build-directory> --config Release --target triaevum_module_tests triaevum_tam_metadata_tests triaevum_service_registry_tests triaevum_native_module_loader_tests
```

Its tests cover a standard SHA-256 vector, a valid two-section module, ABI
rejection, payload corruption and unclaimed trailers. A second native test
builds a mock shared library, packs it into a TAM, crosses the host-service and
frame ABI, round-trips portable state, unloads it and proves a corrupt payload
cannot reach dynamic loading. Python tests independently verify Forge's binary
writer; release validation also feeds a Python-built TAM to the C++ reader to
prevent format drift. Metadata tests cover duplicate fields, invalid UTF-8,
invalid identities and the private-artifact invariant. The service-registry
test additionally crosses a typed
PICA request and verifies sealing, duplicate rejection, unknown-service
handling, malformed request rejection and response sizing. Dedicated audio,
filesystem and input tests cross their real adapters and clients, including
malformed payload/state rejection. The filesystem suite additionally exercises
real rooted content reads, save creation/write/resize and traversal rejection.
