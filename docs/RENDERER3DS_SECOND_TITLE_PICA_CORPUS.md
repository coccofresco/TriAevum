# Nintendo 3DS Renderer Second-Title PICA Corpus

## Purpose

This corpus checks that the shared `Renderer3dsPicaCore` boundary describes
real PICA200 work from a structurally different Nintendo 3DS title. It is a
renderer conformance input, not a second gameplay port and not a runtime asset
source. Raw captures, game data, vertices, uniforms, texture payloads and asset
hashes stay outside Git.

The independent title used for this gate is Super Mario 3D Land (EUR), title
ID `0004000000053f00`, product code `CTR-P-AREP`.

## Reproducible Input

The supplied image contains decrypted NCCH data but its header does not set the
NoCrypto flag. The original was preserved unchanged:

- original SHA-256:
  `EF99F6CDDB641550646B6D69592ACD062BB9101AE0B23E3ED4F0B29D453E0D01`
- diagnostic-copy change: byte `0x418F`, `0x00` to `0x04`
- diagnostic-copy SHA-256:
  `FF4B882601D1BA50663A4558890F70D339B576903D7A74B34F314CD0459B565C`

`ctrtool --info` then identifies `Crypto Key None` and the expected 64 MiB
application-memory mode. No content bytes were transformed.

Azahar's existing PICA frame recorder is title-independent despite its legacy
`OOT3D_PICA_DUMP*` environment-variable names. Two external evidence sets were
captured under:

```text
I:\oot3dre_work\visual-parity\cross-title-sm3dl-20260826\azahar-boot-smoke
I:\oot3dre_work\visual-parity\cross-title-sm3dl-20260826\azahar-idle-30s
```

Texture payload capture was disabled. The checked-in code consumes JSONL one
line at a time and emits only aggregate capability information.

## Observed Surface

The stable three-frame set contains 67 command lists and 169 draws. All
`169/169` draws are structurally valid and representable by the shared PICA
contract, with zero unknown-state or ordering categories. It exercises:

| PICA surface | Draws/variants |
| --- | ---: |
| Indexed draws | 141 |
| Programmable primitive-setup topology | 141 |
| Explicit geometry shader | 0 |
| Fragment lighting | 135 |
| Fog | 118 |
| Alpha blending | 111 |
| Stencil test | 63 |
| Compressed ETC1/ETC1A4 textures | 109 |
| Distinct TEV states | 42 |

It also contains all three native cull modes, depth test/write, alpha test,
logic operations, 1-light fragment configurations, six native texture formats
and a broad TEV operation/source mix. The command stream records 11 vertex
program uploads, 2,504 program words and 509 swizzle words.

The current Azahar executable does not add an explicit `shader_identity`
object to these draw records. Shader upload coverage is therefore measured
from command-register events; this gate does not claim shader-source replay,
texture decode parity or visual parity for the second title.

## Tooling And Gate

The implementation is isolated in the ThreeDsRecomp test/tool layer:

```text
tests/renderer_3ds_pica_capture_conformance.{h,cpp}
tests/renderer_3ds_pica_capture_inspector.cpp
tests/renderer_3ds_pica_capture_conformance_tests.cpp
```

`renderer_3ds_pica_capture_inspector` links the shared PICA core plus JSON only.
It does not link the monolithic runtime or a title module. The cross-title CMake
boundary scanner includes all four diagnostic files and rejects OOT3D/Zelda
tokens. The synthetic consumer canary now also carries the real-corpus feature
classes: compressed native texture format, programmable-setup topology,
structured fragment lighting, depth, blend, alpha test and stencil.

Example repeat command:

```powershell
.\three_ds_recomp_runtime\tests\renderer_3ds_pica_capture_inspector.exe --input <capture-directory> --output <report.json> --corpus-id <stable-id> --source-image-sha256 FF4B882601D1BA50663A4558890F70D339B576903D7A74B34F314CD0459B565C
```

The canonical local aggregate report is:

```text
I:\oot3dre_work\visual-parity\cross-title-sm3dl-20260826\reports\idle-conformance.json
```

## Architectural Result

No title-specific assumption in `Renderer3dsPicaCore` failed this real corpus.
The result validates the transport/state vocabulary, not the complete rendering
backend. The next meaningful reuse extraction is the generic NRI PICA execution
layer still physically located under `fast/oot3d`: pipeline creation, native
texture upload/decode, draw binding and display transfer should become shared
`renderer3ds` services, while each title retains only capture/frontend policy.
