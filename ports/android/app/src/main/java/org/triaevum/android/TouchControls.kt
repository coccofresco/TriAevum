package org.triaevum.android

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.View
import org.triaevum.android.controls.AzaharButtonIds
import org.triaevum.android.controls.Native3dsInputTarget

/** JNI entry points in libTriAevum.so (ports/android/runtime/android_input_bridge.cpp). */
object NativeInputBridge {
    external fun button(hidMask: Int, pressed: Boolean)
    external fun circlePad(x: Float, y: Float)
    external fun cStick(x: Float, y: Float)
    external fun requestMenu()
    external fun requestSettings()
    external fun releaseAll()
    external fun controlBounds(anchor: Int, present: Boolean, left: Float, top: Float, right: Float, bottom: Float)
    external fun titleDrawsControls(): Boolean
}

/**
 * The touch overlay's input, on the UI thread. Controls go to the game's on-screen pad, merged
 * natively with the physical devices. A touch on no control is not the overlay's: it reaches the
 * game surface below, so the menus, F1, the 3DS touch screen and the camera get every finger.
 * HOME is the host menu key: a tap toggles the in-game menu, a long press opens the settings
 * window. The game draws its own HUD art inside the A, B, X, Y, ZL, ZR and D-pad controls while it
 * says so; the overlay then leaves them blank.
 */
class TouchOverlayInput : Native3dsInputTarget {
    /** The overlay view, set once it is built; redrawn when the game takes or leaves its controls. */
    var overlay: View? = null
    private val handler = Handler(Looper.getMainLooper())
    private val openSettings = Runnable {
        homeLongPressed = true
        NativeInputBridge.requestSettings()
    }
    private var homeHeld = false
    private var homeLongPressed = false
    private var titleDraws = false
    private val watchTitle = object : Runnable {
        override fun run() {
            val draws = NativeInputBridge.titleDrawsControls()
            if (draws != titleDraws) {
                titleDraws = draws
                overlay?.invalidate()
            }
            handler.postDelayed(this, TITLE_POLL_MS)
        }
    }

    override fun button(hidMask: Int, pressed: Boolean) = NativeInputBridge.button(hidMask, pressed)
    override fun circlePad(x: Float, y: Float) = NativeInputBridge.circlePad(x, y)
    override fun cStick(x: Float, y: Float) = NativeInputBridge.cStick(x, y)

    override fun home(pressed: Boolean) {
        if (pressed == homeHeld) return
        homeHeld = pressed
        if (pressed) {
            homeLongPressed = false
            handler.postDelayed(openSettings, LONG_PRESS_MS)
        } else {
            handler.removeCallbacks(openSettings)
            if (!homeLongPressed) NativeInputBridge.requestMenu()
        }
    }

    // The overlay leaves touches on no control to the game surface; none arrive here.
    override fun touchPixels(x: Float, y: Float, pressed: Boolean) = Unit
    override fun moveTouchPixels(x: Float, y: Float) = Unit

    override fun swapScreens(enabled: Boolean, displayRotation: Int) = Unit
    override fun toggleTurbo(fromOverlay: Boolean) = Unit

    override fun releaseAll() {
        handler.removeCallbacks(openSettings)
        homeHeld = false
        NativeInputBridge.releaseAll()
    }

    override fun titleDrawsControls() = titleDraws

    override fun controlBounds(anchor: Int, present: Boolean, left: Float, top: Float, right: Float, bottom: Float) =
        NativeInputBridge.controlBounds(anchor, present, left, top, right, bottom)

    /** Follows whether the game draws its controls while the activity is in front. */
    fun resume() {
        handler.removeCallbacks(watchTitle)
        handler.post(watchTitle)
    }

    fun pause() = handler.removeCallbacks(watchTitle)

    companion object {
        private const val LONG_PRESS_MS = 600L
        // The game reports per frame; the overlay follows within this delay.
        private const val TITLE_POLL_MS = 150L

        /**
         * First run of each layout version: the controls OoT3D uses that the donor hides by default
         * (ZL/ZR, HOME as the menu key), no C-stick (a finger dragging on the game moves the
         * camera), and SELECT, HOME and START at the top centre. Later changes are the user's.
         * Runs after the overlay wrote its own defaults; refresh the overlay afterwards.
         */
        fun seedDefaults(activity: Activity) {
            val preferences = activity.getSharedPreferences("${activity.packageName}_preferences", Context.MODE_PRIVATE)
            if (preferences.getInt(SEEDED, 0) >= LAYOUT_VERSION) return
            val metrics = activity.resources.displayMetrics
            val shortSide = minOf(metrics.widthPixels, metrics.heightPixels).toFloat()
            val editor = preferences.edit()
                .putBoolean("buttonToggle6", true)
                .putBoolean("buttonToggle7", true)
                .putBoolean("buttonToggle12", false)
                .putBoolean("buttonToggle13", true)
            // Landscape positions are stored in pixels (the donor's "<id>-Y" keys).
            for (id in listOf(AzaharButtonIds.BUTTON_SELECT, AzaharButtonIds.BUTTON_HOME, AzaharButtonIds.BUTTON_START))
                editor.putFloat("$id-Y", TOP_ROW_Y * shortSide)
            editor.putInt(SEEDED, LAYOUT_VERSION).apply()
        }

        private const val SEEDED = "TriAevumTouchControlsLayout"
        private const val LAYOUT_VERSION = 2
        // Their top edge, as a fraction of the screen's short side (the donor puts them at 0.85).
        private const val TOP_ROW_Y = 0.02f
    }
}
