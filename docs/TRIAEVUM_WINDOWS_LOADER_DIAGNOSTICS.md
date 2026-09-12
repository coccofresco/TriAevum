# Windows startup diagnostics

Error 0xC0000139 means an imported DLL entry point could not be resolved.
This happens before the program's entry point, ROM processing and GPU creation.
A GPU that runs another Vulkan game does not prove that all DLL imports or
required features of this executable are available. Do not download replacement
system DLLs from third-party DLL sites or copy DLLs from another game.

## Alpha.2 users

If Forge fails during runtime preflight, the ordinary game log may not exist:
the runtime has not started. Use the standalone diagnostic below.

Open Windows PowerShell in the extracted folder containing TriAevum.exe
(Shift + right-click the folder background, then Open PowerShell window here).
Run:

```powershell
Invoke-WebRequest 'https://raw.githubusercontent.com/coccofresco/TriAevum/main/tools/triaevum_release/diagnose_windows_loader.ps1' -OutFile '.\TriAevum-loader-check.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\TriAevum-loader-check.ps1' -Installation '.' -Output '.\TriAevum-loader-report.json'
```

The execution-policy override applies only to this diagnostic process. No admin
rights, Python, compiler installation or ROM are needed. The script does not
change the game, system libraries, drivers or registry. It writes the JSON report
you requested. Review paths for personal information before attaching the report
to your issue, together with your GPU model and driver version.

## Scope and limitations

The script reads eager PE imports from the runtime and its root-level packaged
DLLs. It maps providers without running their entry points and checks exports
without calling their addresses. It reports DLL versions, missing symbols,
mapping errors, Windows version and package-module hashes.

It is not an exact Windows loader trace: provider selection, forwarded exports,
API sets and already loaded modules can require further investigation. It does
not recursively certify system dependencies, delayed imports, dynamic plugins,
Vulkan features or GPU driver initialization. An empty problem list is not
proof that the application can start on that machine.

## September 12 issue 12 follow-up

The reporter's alpha.2 screenshot still shows 0xC0000139 on Windows 10
21H2/build 19044.7548 after a fresh extraction. The previous PyInstaller
DLL-search isolation fix therefore does not establish the cause on that PC.

Static inspection of the actual alpha.2 executable confirms eager dependencies
on both vulkan-1.dll and d3d12.dll, even during --product-info. This is a reason
to inspect their exports, not evidence that either is the faulty provider.

The diagnostic passed against the published Windows payload on the development
machine. Windows PowerShell regression tests cover a known system export, an
intentionally missing export (exact importer/DLL/symbol), and a corrupt PE image
that must report incomplete diagnostics rather than success.

Forge source now saves failed native preflight results to
TriAevum-preflight-error.json beside the runtime, including NTSTATUS, captured
output and runtime hash. Failure to write this log is reported explicitly and
does not hide the original error. This logging change is not in the existing
alpha.2 ZIP; the standalone script works with that ZIP without rebuilding.

Keep issue 12 open until the reporter's missing dependency is identified and a
correction is verified there. No GPU requirement or rendering code is changed
on the strength of this screenshot.
