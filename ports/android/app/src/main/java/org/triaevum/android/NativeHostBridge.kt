package org.triaevum.android

/** The game host's JNI entry points in libTriAevum.so (ports/android/runtime/android_host.cpp). */
object NativeHostBridge {
    /**
     * The installer page the in-game menu asked for before the game quit ("saves" or
     * "diagnostics"), once; null when none was asked for.
     */
    external fun takeInstallerPage(): String?
}
