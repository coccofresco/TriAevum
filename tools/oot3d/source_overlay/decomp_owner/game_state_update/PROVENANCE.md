# GameState_Update source-owner provenance

- Owner entry: `GameState_Update@0x00417014`
- Callback continuation: `0x00417024`
- Source package: `I:\oot3dre_work\source_integration\ded8104e66ea-6e7b2907b523`
- Decompiled source revision: `ded8104e66eabe336b389e6a6a3510cb36ac8b58`
- Closure hash: `7bee4264b6767140ba6d1d97ff0ef03dbf679170ac2de266677efb7282221fe3`
- Runtime acceptance hash: `8db2325b0475ea682c9e99640b3ec15b99847b3ec7773b109ab9d23d94d7a4ce`
- Target body hash: `cd3896654716c877313830f1ff251959e8ba219d7cdfdfd9d578fb4a62e4895a`

The accepted semantic body calls `GameState.main` from offset `0x04`, waits for
that callback to return, and then increments the 32-bit frame counter at
`0xF8`. While callback targets still span source and whole-AOT, the overlay
materializes those two data operations from C and retains the already validated
entry/continuation ABI boundary. Once the callback graph is source-native, the
same owner can collapse to the direct C call without changing its data layout.
