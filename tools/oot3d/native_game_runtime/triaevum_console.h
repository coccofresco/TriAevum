#pragma once

// The Windows game is a GUI program: players never see a console window
// (owner decision 12, UX plan WP-03). Developers ask for one with --console
// (anywhere on the command line; it is removed before the arguments are
// parsed) or TRIAEVUM_CONSOLE=1: the game attaches to the console of the
// program that started it, or opens its own. Output that is already
// redirected to a file or pipe (scripts, Forge's Play, release probes) stays
// where it goes. Elsewhere the option is accepted and has no effect.
namespace TriAevum {

void AttachDeveloperConsole(int& argc, char** argv);

// Removes every --console from argv (argv[argc] stays nullptr); whether
// there was one.
bool TakeConsoleOption(int& argc, char** argv);

} // namespace TriAevum
