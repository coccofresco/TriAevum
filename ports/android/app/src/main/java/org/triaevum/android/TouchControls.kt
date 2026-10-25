package org.triaevum.android

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.MotionEvent
import android.view.View
import org.libsdl.app.SDLActivity
import org.triaevum.android.controls.Native3dsInputTarget

/** JNI entry points in libTriAevum.so (ports/android/runtime/android_input_bridge.cpp). */
object NativeInputBridge {
    external fun button(hidMask: Int, pressed: Boolean)
    external fun circlePad(x: Float, y: Float)
    external fun cStick(x: Float, y: Float)
    external fun requestMenu()
    external fun requestSettings()
    external fun releaseAll()
}

/**
 * The touch overlay's input, on the UI thread. Controls go to the game's on-screen pad, merged
 * natively with the physical devices. Touches that miss every control go to SDL as touches, so
 * the menus, F1 and the 3DS touch screen keep their own pointer mapping. HOME is the host menu
 * key: a tap toggles the in-game menu, a long press opens the settings window.
 */
class TouchOverlayInput : Native3dsInputTarget {
    /** The overlay view, set once it is built; touches are normalized by its size. */
    var overlay: View? = null
    private val handler = Handler(Looper.getMainLooper())
    private val openSettings = Runnable {
        homeLongPressed = true
        NativeInputBridge.requestSettings()
    }
    private var homeHeld = false
    private var homeLongPressed = false
    private var touching = false
    private var lastX = 0f
    private var lastY = 0f

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

    override fun touchPixels(x: Float, y: Float, pressed: Boolean) {
        // The overlay reports a release without coordinates: it ends where the touch last was.
        if (pressed) {
            lastX = x
            lastY = y
        }
        if (pressed == touching) return
        touching = pressed
        sendTouch(if (pressed) MotionEvent.ACTION_DOWN else MotionEvent.ACTION_UP)
    }

    override fun moveTouchPixels(x: Float, y: Float) {
        lastX = x
        lastY = y
        if (touching) sendTouch(MotionEvent.ACTION_MOVE)
    }

    override fun swapScreens(enabled: Boolean, displayRotation: Int) = Unit
    override fun toggleTurbo(fromOverlay: Boolean) = Unit

    override fun releaseAll() {
        handler.removeCallbacks(openSettings)
        homeHeld = false
        if (touching) {
            touching = false
            sendTouch(MotionEvent.ACTION_UP)
        }
        NativeInputBridge.releaseAll()
    }

    /** The overlay covers the game surface exactly; SDL normalizes touches by the view size. */
    private fun sendTouch(action: Int) {
        val view = overlay ?: return
        val width = view.width.coerceAtLeast(1).toFloat()
        val height = view.height.coerceAtLeast(1).toFloat()
        val x = (lastX / width).coerceIn(0f, 1f)
        val y = (lastY / height).coerceIn(0f, 1f)
        SDLActivity.onNativeTouch(TOUCH_DEVICE, 0, action, x, y, if (action == MotionEvent.ACTION_UP) 0f else 1f)
    }

    companion object {
        private const val LONG_PRESS_MS = 600L
        // A touch device id of its own, apart from the game surface's real devices.
        private const val TOUCH_DEVICE = 0x7A0E

        /**
         * First run: show the controls OoT3D uses that the donor hides by default (ZL/ZR for
         * the TopScreen items, the C-stick, HOME as the menu key). Later changes are the user's.
         */
        fun seedDefaults(activity: Activity) {
            val preferences = activity.getSharedPreferences("${activity.packageName}_preferences", Context.MODE_PRIVATE)
            if (preferences.getBoolean(SEEDED, false)) return
            preferences.edit()
                .putBoolean("buttonToggle6", true)
                .putBoolean("buttonToggle7", true)
                .putBoolean("buttonToggle12", true)
                .putBoolean("buttonToggle13", true)
                .putBoolean(SEEDED, true)
                .apply()
        }

        private const val SEEDED = "TriAevumTouchControlsSeeded"
    }
}
