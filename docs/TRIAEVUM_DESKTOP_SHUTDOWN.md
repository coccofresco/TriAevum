# Desktop Shutdown

## Fault and Ownership

The Linux donor crash handler treated SIGINT/SIGTERM/SIGQUIT as `exit(1)`.
GDB reproduced a signal during shader compilation followed by static Context
destruction, NRI descriptor destruction and allocator corruption inside the
Vulkan driver. Cleanup was running from the signal handler while the original
render stack and worker threads were still live. This was not an SSSR shading
or alignment failure.

`ship/utils/ShutdownRequest.h` now publishes only a lock-free atomic request.
The shared `Fast3dWindow::IsRunning()` observes it on the normal host loop.
No logger, allocator, SDL call, destructor or driver operation runs from the
shutdown signal handler. Repeated requests remain idempotent. SIGKILL is not
registered because it cannot be handled. Fatal crash diagnostics are unchanged.

The host owns process lifetime; effect providers and title gameplay must not
implement separate signal handlers or interrupt their own resource teardown.

## Qualification

- `test_shutdown_request.py` compiles and executes a real C++ signal test on
  Windows and Linux: a live scope survives two termination signals, then is
  destroyed once through ordinary scope exit. Reset also clears the request.
- Portable SDK runtime rebuild changed the host objects only, not title AOT.
- Before correction, two Flatpak debugger runs reproduced allocator failures
  following SIGTERM. The deeper stack identified `Ship::ShutdownHandler ->
  exit -> Context destruction`, interrupting `shaderc_compile_into_spv`.
- After correction, the same cold-cache Flatpak signal test exited normally.
  GDB's batch exit status is not the game status: the script asks for a stack
  after exit, so its trailing `No stack` is expected. Inspect the inferior exit.
- The normal frozen Forge launcher then updated an isolated installation and
  ran the game for 45 seconds. SIGTERM sent to that exact runtime process
  produced launcher exit 0 after 46.17 seconds total, with no allocator error.
  The title receipt changed automatically. The fixture contained no save files;
  an unchanged empty save inventory is not save-compatibility evidence.
- This run reused all 23 pass shaders and 140 PICA shaders with zero compilation.
- Release discovery: 365 tests on each OS, no failures; 9 Windows skips and
  13 Linux skips. The Windows signal test compiled/executed on Windows, but the
  complete Windows runtime rebuild remains blocked by the full J: build drive.

Installed private Flatpak: `cd068c68c9f5cec93e80be63277879b6d22b570e57d14568f5a553dd17c122c0`.
Runtime SHA-256: `629afe741ca4ae262b5ed60c8385381ba62ee7becabc474c27b0702cfc88e5b6`.
This is not a public release or a declaration of complete desktop parity.

Private evidence is under the installed activation's
`qualification/sssr-signal-{debug,stack,fixed}/gdb.log` on the Linux test host.
Do not distribute the fixture's ROM-derived data or user configuration.
