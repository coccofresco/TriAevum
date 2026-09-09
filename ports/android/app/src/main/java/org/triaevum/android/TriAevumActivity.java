package org.triaevum.android;

import org.libsdl.app.SDLActivity;
import java.io.File;
import android.os.Bundle;
import android.view.WindowManager;

/** SDL owns Surface/lifecycle. Game, renderer and title loading retain their native owners. */
public final class TriAevumActivity extends SDLActivity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
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
