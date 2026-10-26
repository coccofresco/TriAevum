# Release notes template

Copy this file to `docs/releases/v<version>.md` and fill it in. Keep the
"Updating" section: it is what most players read. Replace every `<...>`;
delete the notes in HTML comments before publishing.

````markdown
# TriAevum v<version>

## <A short title for this release>

> [!WARNING]
> **Still a beta.** If something breaks (or works), please
> [open an issue](https://github.com/coccofresco/TriAevum/issues/new) with
> the support bundle from Forge's **Diagnostics** page (**Save support
> bundle...**; on Android, **Share diagnostics**) and what you were doing
> right before it went wrong.

<One paragraph: what this release is about, and which platforms it ships
for.>

### You reported it, we fixed it

- **<Short symptom in the reporter's words>** (#<issue>): <what was wrong
  and what changed, in one or two sentences>.

### <Feature section>

<What players see, and where to find it.>

### Updating

Your saves and settings are kept, and your ROM is not needed. Back up
first anyway: in Forge, **Data & backups > Back up your files...**
(Android: **Saves and backups > Back up your files…**).

<!-- TODO(WP-04B): check the Windows steps against the shipped Forge
(button names, signature check, downgrade question) before publishing. -->

- **Windows**: download the new ZIP. Open `TriAevumForge.exe` from your
  TriAevum folder and choose **Home > Update > Choose the downloaded ZIP**.
  Or close the game and Forge, extract the ZIP into the same folder
  (replace the files), open `TriAevumForge.exe` and press **Update**.
  Start the game only after the update finished.
- **Linux AppImage**: replace the AppImage with the new one (point your
  Steam shortcut to it if the file name changed) and open it. Forge opens
  and offers **Update**.
- **Linux Flatpak**: `flatpak install --user TriAevum-<version>-Linux-x86_64.flatpak`,
  then start TriAevum. Forge opens and offers **Update**.
- **Android**: install the new APK over the app. Do not uninstall the app
  first: that deletes your saves.
  <!-- TODO(WP-09): replace with the installer's update step once it exists. -->

If your HUD layout differs from the standard one, Forge asks whether to
keep it (**Keep mine**, the default) or reset it (**Use standard**).
<Name any setting this release changes on update, or say "No setting
changes on update.">

<Supported inputs: "Use your own decrypted <regions> `.3ds` or `.cci` dump
(<revisions>)." List what is not supported yet.>

The full guide: [USER_GUIDE.md](../USER_GUIDE.md). Previous notes:
[v<previous>](v<previous>.md).
````
