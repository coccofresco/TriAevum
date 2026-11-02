# TriAevum for Apple Silicon Macs

This macOS package is a release candidate. Its deployment floor is macOS 14.0
on Apple Silicon (M1 or newer); that binary check is not a claim that every
supported macOS version has been tested. Intel Macs are not supported by this
package. Consult the accompanying qualification report for the versions tested.

1. Extract the complete ZIP and move `TriAevum.app` to Applications or another
   folder you control. Keep the app intact.
2. Open TriAevum. Forge asks for your supported decrypted `.3ds` or `.cci` ROM.
3. Let Forge verify the ROM and prepare your data and graphics caches, then
   choose **Play**. A missing or corrupt title module requires a complete
   replacement package. Forge never translates game code, compiles or links
   a title, or installs an SDK.

The app includes the precompiled translated title logic. It does not include
a ROM, keys, extracted game assets or saves. Optional HD packs and mods retain
their separate import/download choices. Your original ROM is unchanged.

## First opening and Gatekeeper

This candidate is **ad-hoc signed and not notarized**. Ad-hoc signing seals the
bundle but does not identify an Apple-registered developer. A downloaded app
may be quarantined and blocked by Gatekeeper even when `codesign` verifies it.

After attempting to open a copy you trust, macOS may offer **Open Anyway** in
System Settings > Privacy & Security. Confirm that the alert names TriAevum.
Use Apple's [instructions for opening an unverified app](https://support.apple.com/en-us/102445).
Do not disable Gatekeeper system-wide. A warning that the app is damaged or
modified should be resolved by obtaining a complete fresh copy and checking
its published digest; an ad-hoc seal alone does not authenticate a publisher.

The app supports a read-only/translocated bundle: Forge writes user data
outside it. Move the app to its intended location before installing your ROM.
If it is moved later, open Forge from its new location and import again to
refresh package paths, preserving existing saves and settings.

## Data, settings and updates

Your data is under `~/Library/Application Support/TriAevum`:

- `TriAevum.launch.json` is the installed launch profile.
- `data/` contains extracted inputs, prepared title data, settings, saves,
  logs and graphics caches. `game.tam`, `content.tap` and device caches remain
  private local artifacts.
- The signed title library stays inside the app and is selected by the
  launch profile. Nothing is compiled or copied into the signed app.

Back up your data before replacing the app. Close the game and Forge, replace
the whole app with a complete newer package, then open Forge to refresh the
installation. In-place Windows ZIP updates do not apply to a signed macOS
bundle. Removing the app leaves your data; removing that Application Support
folder also removes your local saves and settings.

Advanced/developer use can redirect this activation directory with the
absolute environment variable `TRIAEVUM_MACOS_DATA_ROOT`. It must point
outside the app. This override is separate from normal Mac user storage.

## Graphics and included libraries

The renderer uses the bundled Khronos Vulkan loader and MoltenVK over Metal.
Users need no Vulkan SDK, Homebrew, compiler or separately installed driver.
Initial shader/device preparation can take longer than later starts.

Default Apple graphics settings are owned by the runtime. Use the F1 menu
to inspect or change them. Performance, audio, input and Retina qualification
are reported separately; successful installation alone does not verify them.

The package retains donor notices, corresponding source and translated title
source under `TriAevum.app/Contents/Resources`. MoltenVK's license and the Vulkan
loader's complete license text are in `LICENSES/` there. See `docs/SOURCE_OFFER.md`
and `docs/THIRD_PARTY_NOTICES.md` for the project's source and license boundaries.
