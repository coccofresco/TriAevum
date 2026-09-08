# Microsoft Visual C++ Runtime

The Windows package includes `msvcp140.dll`, `vcruntime140.dll`, and
`vcruntime140_1.dll`, copyright Microsoft Corporation, for the dynamically
linked C++ runtime dependency of `shaderc_shared.dll`.

These are unmodified release binaries copied from the licensed publisher's
Visual Studio `VC/Redist/MSVC/<version>/x64/Microsoft.VC143.CRT` directory,
not from Windows system directories. They retain Microsoft's license and
are not relicensed under TriAevum's GPL. The package manifest records the
exact file hashes. No Visual Studio SDK, debug runtime or compiler is included.

Redistribution is subject to the publisher's Microsoft Software License Terms
and the [Visual Studio 2022 redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution).
See Microsoft's [Visual C++ redistribution documentation](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170)
for application-local deployment requirements. This notice does not replace
or expand Microsoft's license terms.
