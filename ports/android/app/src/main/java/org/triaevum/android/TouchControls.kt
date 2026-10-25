package org.triaevum.android

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import org.triaevum.android.controls.AzaharButtonIds
import org.triaevum.android.controls.AzaharInputAdapter
import org.triaevum.android.controls.InputOverlay
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
    external fun reportOverlaySettings(appliedRevision: Int, visible: Boolean, opacity: Int, size: Int,
                                       vibration: Boolean,
                                       hideWithController: Boolean)
    external fun overlayRequests(): IntArray
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
    var overlay: InputOverlay? = null
    private var activity: Activity? = null
    private var parent: ViewGroup? = null
    private var editBar: View? = null
    private var hiddenForController = false
    private var appliedRevision = 0
    private var appliedEdit = 0
    private var appliedReset = 0
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
            applyRequests()
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

    /** The activity's overlay and the layout it sits in, for its settings and layout editing. */
    fun attach(activity: Activity, overlay: InputOverlay, parent: ViewGroup) {
        this.activity = activity
        this.overlay = overlay
        this.parent = parent
        reportSettings()
    }

    private fun preferences() = activity?.let {
        it.getSharedPreferences("${it.packageName}_preferences", Context.MODE_PRIVATE)
    }

    /** The overlay's saved settings, for the game's menus. */
    private fun reportSettings() {
        val preferences = preferences() ?: return
        NativeInputBridge.reportOverlaySettings(
            appliedRevision,
            preferences.getBoolean(SHOW_OVERLAY, true),
            preferences.getInt(OPACITY, 50),
            preferences.getInt(SCALE, 50) + 50,
            preferences.getBoolean(VIBRATION, true),
            preferences.getBoolean(HIDE_WITH_CONTROLLER, true))
    }

    /** The game's menus ask for a change, layout editing or the default layout. */
    private fun applyRequests() {
        val requests = NativeInputBridge.overlayRequests()
        if (requests.size < 8) return
        val view = overlay ?: return
        val preferences = preferences() ?: return
        if (requests[0] != appliedRevision) {
            appliedRevision = requests[0]
            preferences.edit()
                .putBoolean(SHOW_OVERLAY, requests[1] != 0)
                .putInt(OPACITY, requests[2])
                .putInt(SCALE, requests[3] - 50)
                .putBoolean(VIBRATION, requests[4] != 0)
                .putBoolean(HIDE_WITH_CONTROLLER, requests[5] != 0)
                .apply()
            view.refreshControls()
            reportSettings()
        }
        if (requests[7] != appliedReset) {
            appliedReset = requests[7]
            view.resetButtonPlacement()
            activity?.let { placeTopRow(it, preferences.edit()).apply() }
            view.refreshControls()
        }
        if (requests[6] != appliedEdit) {
            appliedEdit = requests[6]
            editLayout(true)
        }
    }

    /** Layout editing: every control drawn and movable until Done. */
    private fun editLayout(editing: Boolean) {
        val view = overlay ?: return
        val host = activity ?: return
        val layout = parent ?: return
        if (editing == (editBar != null)) return
        if (editing) {
            NativeInputBridge.releaseAll()
            showControls()
            val done = Button(host).apply {
                text = "Done"
                setOnClickListener { editLayout(false) }
            }
            val reset = Button(host).apply {
                text = "Reset"
                setOnClickListener {
                    view.resetButtonPlacement()
                    preferences()?.let { placeTopRow(host, it.edit()).apply() }
                    view.refreshControls()
                }
            }
            val bar = LinearLayout(host).apply {
                orientation = LinearLayout.HORIZONTAL
                addView(reset)
                addView(done)
            }
            layout.addView(bar, ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT))
            bar.post {
                bar.x = (layout.width - bar.width) / 2f
                bar.y = layout.height * 0.4f
            }
            editBar = bar
            view.setIsInEditMode(true)
            // The overlay draws every control while it is edited; the game
            // draws its full HUD meanwhile.
            controlBoundsCleared()
        } else {
            layout.removeView(editBar)
            editBar = null
            view.setIsInEditMode(false)
            view.refreshControls()
        }
        view.invalidate()
    }

    /** A game controller was used: the controls step aside while the user wants that. */
    fun controllerUsed() {
        val view = overlay ?: return
        if (hiddenForController || editBar != null || preferences()?.getBoolean(HIDE_WITH_CONTROLLER, true) != true) return
        hiddenForController = true
        releaseAll()
        view.visibility = View.INVISIBLE
        // Without anchors the game draws its whole HUD again.
        controlBoundsCleared()
    }

    /** A touch brings hidden controls back. */
    fun touched() {
        if (!hiddenForController) return
        showControls()
    }

    private fun showControls() {
        val view = overlay ?: return
        if (!hiddenForController) return
        hiddenForController = false
        view.visibility = View.VISIBLE
        view.refreshControls()
    }

    private fun controlBoundsCleared() {
        for (anchor in 0 until AzaharInputAdapter.ANCHOR_COUNT)
            NativeInputBridge.controlBounds(anchor, false, 0f, 0f, 0f, 0f)
    }

    /** Follows whether the game draws its controls while the activity is in front. */
    fun resume() {
        handler.removeCallbacks(watchTitle)
        handler.post(watchTitle)
    }

    fun pause() = handler.removeCallbacks(watchTitle)

    companion object {
        private const val LONG_PRESS_MS = 600L
        private const val SHOW_OVERLAY = "EmulationMenuSettings_ShowOverlay"
        private const val VIBRATION = "EmulationMenuSettings_HapticFeedback"
        private const val OPACITY = "controlOpacity"
        private const val SCALE = "controlScale"
        private const val HIDE_WITH_CONTROLLER = "TriAevumHideWithController"
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
            val editor = preferences.edit()
                .putBoolean("buttonToggle6", true)
                .putBoolean("buttonToggle7", true)
                .putBoolean("buttonToggle12", false)
                .putBoolean("buttonToggle13", true)
            placeTopRow(activity, editor).putInt(SEEDED, LAYOUT_VERSION).apply()
        }

        /** SELECT, HOME and START at the top centre (landscape positions are pixels). */
        fun placeTopRow(activity: Activity, editor: android.content.SharedPreferences.Editor):
                android.content.SharedPreferences.Editor {
            val metrics = activity.resources.displayMetrics
            val shortSide = minOf(metrics.widthPixels, metrics.heightPixels).toFloat()
            for (id in listOf(AzaharButtonIds.BUTTON_SELECT, AzaharButtonIds.BUTTON_HOME, AzaharButtonIds.BUTTON_START))
                editor.putFloat("$id-Y", TOP_ROW_Y * shortSide)
            return editor
        }

        private const val SEEDED = "TriAevumTouchControlsLayout"
        private const val LAYOUT_VERSION = 2
        // Their top edge, as a fraction of the screen's short side (the donor puts them at 0.85).
        private const val TOP_ROW_Y = 0.02f
    }
}
