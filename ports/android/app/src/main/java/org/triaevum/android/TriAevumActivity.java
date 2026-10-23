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
import android.view.WindowManager;

/** SDL owns Surface/lifecycle. Game, renderer and title loading retain their native owners. */
public final class TriAevumActivity extends SDLActivity {
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
        applyDeveloperEnvironment();
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
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
            "--title-plugin", new File(getApplicationInfo().nativeLibraryDir,
                "libtriaevum_title_aot.so").getAbsolutePath()
        };
    }
}
