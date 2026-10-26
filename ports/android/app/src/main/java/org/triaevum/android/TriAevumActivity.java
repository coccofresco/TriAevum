package org.triaevum.android;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;
import org.json.JSONObject;
import android.system.Os;
import java.util.Iterator;
import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import android.content.Context;
import android.os.Bundle;
import android.util.Log;
import android.view.ViewGroup;
import android.view.WindowManager;
import org.triaevum.android.controls.AzaharInputAdapter;
import org.triaevum.android.controls.InputOverlay;
import org.triaevum.android.controls.OverlayHost;
import org.triaevum.android.diagnostics.StartupDiagnostics;

/** SDL owns Surface/lifecycle. Game, renderer and title loading retain their native owners. */
public final class TriAevumActivity extends SDLActivity {
    private InputOverlay touchOverlay;
    private TouchOverlayInput touchInput;
    private AzaharInputAdapter touchAdapter;

    @Override protected SDLSurface createSDLSurface(Context context) {
        int maximumShortEdge = 720;
        File config = new File(getExternalFilesDir(null), "TriAevum.android.host.json");
        if (config.isFile()) {
            try {
                maximumShortEdge = new JSONObject(new String(Files.readAllBytes(config.toPath()), StandardCharsets.UTF_8))
                    .getInt("maximum_surface_short_edge");
                if (maximumShortEdge < 0) throw new IllegalArgumentException("Negative surface limit");
            } catch (Exception error) {
                Log.w("TriAevum", "Invalid Android host config; using 720p surface limit", error);
                maximumShortEdge = 720;
            }
        }
        return new TriAevumSurface(context, maximumShortEdge);
    }

    @Override protected void onCreate(Bundle state) {
        // Startup stages for the installer's diagnostics report (no network, no personal data).
        StartupDiagnostics.beginGameSession(this);
        StartupDiagnostics.stage(this, "activity_created");
        applyDeveloperEnvironment();
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (!mBrokenLibraries) {
            attachTouchControls();
            StartupDiagnostics.stage(this, "touch_controls_attached");
        }
    }

    @Override public void loadLibraries() {
        StartupDiagnostics.stage(this, "libraries_loading");
        try {
            super.loadLibraries();
        } catch (RuntimeException | UnsatisfiedLinkError error) {
            StartupDiagnostics.stage(this, "libraries_failed " + error.getClass().getName());
            throw error;
        }
        StartupDiagnostics.stage(this, "libraries_loaded");
    }

    /** SDL's main thread asks for the entry point right before the native game starts. */
    @Override protected String getMainFunction() {
        StartupDiagnostics.stage(this, "native_main_starting");
        return super.getMainFunction();
    }

    /** The touch controls above the game surface, the same size, without taking key focus. */
    private void attachTouchControls() {
        touchInput = new TouchOverlayInput();
        touchAdapter = new AzaharInputAdapter(touchInput);
        // The overlay reads its preferences through the bound host while it is built.
        OverlayHost.INSTANCE.bind(this, touchAdapter);
        touchOverlay = new InputOverlay(this, null);
        // TriAevum's layout goes over the overlay's own first-run defaults.
        TouchOverlayInput.Companion.seedDefaults(this);
        touchOverlay.refreshControls();
        touchOverlay.setFocusable(false);
        touchInput.attach(this, touchOverlay, mLayout);
        mLayout.addView(touchOverlay, new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        mSurface.requestFocus();
    }

    @Override protected void onPause() {
        StartupDiagnostics.stage(this, "paused");
        if (touchAdapter != null) touchAdapter.releaseAll();
        if (touchInput != null) touchInput.pause();
        super.onPause();
    }

    @Override protected void onResume() {
        super.onResume();
        StartupDiagnostics.stage(this, "resumed");
        if (touchInput != null) touchInput.resume();
    }

    // A controller hides the touch controls (when the user wants that); a touch shows them.
    @Override public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (touchInput != null && isController(event.getSource())) touchInput.controllerUsed();
        return super.dispatchKeyEvent(event);
    }

    @Override public boolean dispatchGenericMotionEvent(android.view.MotionEvent event) {
        if (touchInput != null && isController(event.getSource())) touchInput.controllerUsed();
        return super.dispatchGenericMotionEvent(event);
    }

    @Override public boolean dispatchTouchEvent(android.view.MotionEvent event) {
        if (touchInput != null) touchInput.touched();
        // The window's touches (overlay and game surface) arrive as they happen,
        // not batched to the next display frame, for the rest of the gesture.
        if (event.getActionMasked() == android.view.MotionEvent.ACTION_DOWN)
            getWindow().getDecorView().requestUnbufferedDispatch(event);
        return super.dispatchTouchEvent(event);
    }

    private static boolean isController(int source) {
        return (source & android.view.InputDevice.SOURCE_GAMEPAD) == android.view.InputDevice.SOURCE_GAMEPAD
            || (source & android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK;
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        if (!hasFocus && touchAdapter != null) touchAdapter.releaseAll();
        // Stick motion reaches the game as it arrives, not batched to the next
        // display frame (the window is attached by now).
        if (hasFocus && android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R)
            getWindow().getDecorView().requestUnbufferedDispatch(android.view.InputDevice.SOURCE_CLASS_JOYSTICK);
        super.onWindowFocusChanged(hasFocus);
    }

    @Override protected void onDestroy() {
        if (touchOverlay != null) mLayout.removeView(touchOverlay);
        if (touchAdapter != null) OverlayHost.INSTANCE.unbind(touchAdapter);
        if (touchInput != null) touchInput.pause();
        touchOverlay = null;
        touchAdapter = null;
        touchInput = null;
        super.onDestroy();
        // The native game runs once per process (title library, guest memory). When the game
        // ends, end the process too, so the next launch starts clean instead of reusing it.
        if (isFinishing()) {
            StartupDiagnostics.stage(this, "exited");
            System.exit(0);
        }
    }

    /**
     * Developer comparisons only: the optional "environment" object of the host config sets the same
     * diagnostic variables the desktop harnesses export, before the native libraries load.
     */
    private void applyDeveloperEnvironment() {
        File config = new File(getExternalFilesDir(null), "TriAevum.android.host.json");
        if (!config.isFile()) return;
        try {
            JSONObject environment = new JSONObject(new String(Files.readAllBytes(config.toPath()),
                StandardCharsets.UTF_8)).optJSONObject("environment");
            if (environment == null) return;
            for (Iterator<String> keys = environment.keys(); keys.hasNext();) {
                String key = keys.next();
                String value = environment.getString(key);
                Os.setenv(key, value, true);
                Log.i("TriAevum", "Developer environment " + key + "=" + value);
            }
        } catch (Exception error) {
            Log.w("TriAevum", "Invalid Android host environment; ignored", error);
        }
    }

    @Override protected String[] getLibraries() {
        return new String[] { "SDL2", "triaevum_title_bootstrap", "TriAevum" };
    }

    @Override protected String[] getArguments() {
        File root = getExternalFilesDir(null);
        return new String[] {
            "--launch-profile", new File(root, "TriAevum.android.launch.json").getAbsolutePath(),
            "--title-plugin", new File(getApplicationInfo().nativeLibraryDir, titleLibrary(root)).getAbsolutePath()
        };
    }

    /**
     * The title library Forge installed (TriAevum.android.title.json); the APK's default title
     * library when Forge has not run. Forge verified the library against its catalog.
     */
    private static String titleLibrary(File root) {
        String fallback = "libtriaevum_title_aot.so";
        File title = new File(root, "TriAevum.android.title.json");
        if (!title.isFile()) return fallback;
        try {
            String library = new JSONObject(new String(Files.readAllBytes(title.toPath()), StandardCharsets.UTF_8))
                .getString("plugin_library");
            if (library.matches("lib[A-Za-z0-9_.-]+\\.so")) return library;
            Log.w("TriAevum", "Invalid title library name " + library + "; using " + fallback);
        } catch (Exception error) {
            Log.w("TriAevum", "Invalid Android title record; using " + fallback, error);
        }
        return fallback;
    }
}
