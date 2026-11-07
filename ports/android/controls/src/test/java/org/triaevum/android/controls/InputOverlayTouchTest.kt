package org.triaevum.android.controls

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.Rect
import android.view.MotionEvent
import androidx.preference.PreferenceManager
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** Actual Android MotionEvents through the production hit-test/dispatch owner.
 * JVM Android simulation, not a phone, JNI, vibration or game qualification. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [29])
class InputOverlayTouchTest {
    private class Sink : OverlayInputSink {
        val held = mutableSetOf<Int>()
        val axes = mutableMapOf<Int, Pair<Float, Float>>()
        var releases = 0
        var homeTaps = 0
        override fun button(id: Int, pressed: Boolean): Boolean {
            if (!pressed && id == AzaharButtonIds.BUTTON_HOME && id in held) ++homeTaps
            if (pressed) held.add(id) else held.remove(id)
            return true
        }
        override fun stick(id: Int, x: Float, y: Float): Boolean { axes[id] = x to y; return true }
        override fun releaseAll() { ++releases; held.clear(); axes.clear() }
        override fun touch(x: Float, y: Float, pressed: Boolean) = Unit
        override fun touchMoved(x: Float, y: Float) = Unit
        override fun swapScreens(enabled: Boolean, displayRotation: Int) = Unit
        override fun toggleTurbo(fromOverlay: Boolean) = Unit
        override fun drawsControl(id: Int) = false
        override fun controlBounds(bounds: Map<Int, IntArray>, width: Int, height: Int) = Unit
    }
    private lateinit var activity: Activity
    private lateinit var overlay: InputOverlay
    private lateinit var sink: Sink
    private lateinit var button: InputOverlayDrawableButton
    private lateinit var dpad: InputOverlayDrawableDpad
    private lateinit var stick: InputOverlayDrawableJoystick
    private var time = 0L

    @Suppress("UNCHECKED_CAST")
    private fun <T> controls(name: String): MutableSet<T> =
        InputOverlay::class.java.getDeclaredField(name).let {
            it.isAccessible = true
            it.get(overlay) as MutableSet<T>
        }

    @Before fun setup() {
        activity = Robolectric.buildActivity(Activity::class.java).setup().get()
        sink = Sink()
        OverlayHost.bind(activity, sink)
        PreferenceManager.getDefaultSharedPreferences(activity).edit().clear()
            .putBoolean("OverlayInit", true)
            .putBoolean("EmulationMenuSettings_ShowOverlay", false).commit()
        overlay = InputOverlay(activity, null)
        EmulationMenuSettings.hapticFeedback = false
        EmulationMenuSettings.joystickRelCenter = false
        EmulationMenuSettings.buttonSlide = ButtonSlidingMode.Disabled.int
        val bitmap = Bitmap.createBitmap(100, 100, Bitmap.Config.ARGB_8888)
        button = InputOverlayDrawableButton(activity.resources, bitmap, bitmap, AzaharButtonIds.BUTTON_HOME, 255)
        button.setBounds(0, 0, 100, 100)
        dpad = InputOverlayDrawableDpad(activity.resources, bitmap, bitmap, bitmap,
            AzaharButtonIds.DPAD_UP, AzaharButtonIds.DPAD_DOWN, AzaharButtonIds.DPAD_LEFT, AzaharButtonIds.DPAD_RIGHT, 255)
        dpad.setBounds(100, 0, 200, 100)
        stick = InputOverlayDrawableJoystick(activity.resources, bitmap, bitmap, bitmap,
            Rect(200, 0, 300, 100), Rect(235, 35, 265, 65), AzaharButtonIds.STICK_LEFT, 255)
        controls<InputOverlayDrawableButton>("overlayButtons").add(button)
        controls<InputOverlayDrawableDpad>("overlayDpads").add(dpad)
        controls<InputOverlayDrawableJoystick>("overlayJoysticks").add(stick)
    }
    @After fun teardown() { OverlayHost.unbind(sink); activity.finish() }

    private fun event(action: Int, vararg xy: Pair<Float, Float>): Boolean {
        val properties = Array(xy.size) { i -> MotionEvent.PointerProperties().apply { id = i; toolType = MotionEvent.TOOL_TYPE_FINGER } }
        val coords = Array(xy.size) { i -> MotionEvent.PointerCoords().apply { x = xy[i].first; y = xy[i].second; pressure = 1f; size = 1f } }
        val e = MotionEvent.obtain(0, ++time, action, xy.size, properties, coords, 0, 0, 1f, 1f, 0, 0, 0, 0)
        return try { overlay.onTouch(overlay, e) } finally { e.recycle() }
    }
    private fun down(x: Float, y: Float) = event(MotionEvent.ACTION_DOWN, x to y)
    private fun move(x: Float, y: Float) = event(MotionEvent.ACTION_MOVE, x to y)
    private fun up(x: Float, y: Float) = event(MotionEvent.ACTION_UP, x to y)

    @Test fun singleAxisMotionAndReturnToCentreReachTheSink() {
        down(250f, 50f)
        move(275f, 50f)
        assertEquals(.5f to 0f, sink.axes[AzaharButtonIds.STICK_LEFT])
        move(250f, 50f)
        assertEquals(0f to 0f, sink.axes[AzaharButtonIds.STICK_LEFT])
        move(250f, 25f)
        assertEquals(0f, sink.axes.getValue(AzaharButtonIds.STICK_LEFT).first, .00001f)
        assertEquals(-.5f, sink.axes.getValue(AzaharButtonIds.STICK_LEFT).second, .00001f)
        up(250f, 25f)
        assertEquals(0f to 0f, sink.axes[AzaharButtonIds.STICK_LEFT])
    }

    @Test fun cancelClearsEveryPointerWithoutSynthesizingAHomeTap() {
        down(50f, 50f)
        event(MotionEvent.ACTION_POINTER_DOWN or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), 50f to 50f, 120f to 20f)
        event(MotionEvent.ACTION_POINTER_DOWN or (2 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), 50f to 50f, 120f to 20f, 275f to 75f)
        assertEquals(setOf(AzaharButtonIds.BUTTON_HOME, AzaharButtonIds.DPAD_UP, AzaharButtonIds.DPAD_LEFT), sink.held)
        assertTrue(sink.axes.isNotEmpty())
        event(MotionEvent.ACTION_CANCEL, 50f to 50f, 120f to 20f, 275f to 75f)
        assertTrue(sink.held.isEmpty()); assertTrue(sink.axes.isEmpty()); assertEquals(0, sink.homeTaps)
        assertEquals(-1, button.trackId); assertEquals(AzaharButtonState.RELEASED, button.status)
        assertEquals(-1, dpad.trackId); assertEquals(AzaharButtonState.RELEASED, dpad.upStatus)
        assertEquals(-1, stick.trackId); assertEquals(0f, stick.xAxis, 0f); assertEquals(0f, stick.yAxis, 0f)
        EmulationMenuSettings.buttonSlide = ButtonSlidingMode.Enabled.int
        move(50f, 50f) // late motion from the cancelled gesture must not re-arm
        assertTrue(sink.held.isEmpty())
        down(50f, 50f); up(50f, 50f)
        assertEquals(1, sink.homeTaps)
    }

    @Test fun refreshAndEditCancelInsteadOfLosingHeldOwners() {
        down(50f, 50f)
        overlay.setIsInEditMode(true)
        assertTrue(sink.held.isEmpty()); assertEquals(0, sink.homeTaps)
        overlay.setIsInEditMode(false)
        down(50f, 50f)
        overlay.refreshControls()
        assertTrue(sink.held.isEmpty()); assertEquals(0, sink.homeTaps)
    }

    @Test fun focusLossDiscardsLateMotionAndAllowsAFreshGesture() {
        down(275f, 75f)
        assertTrue(sink.axes.isNotEmpty())
        overlay.onWindowFocusChanged(false)
        assertTrue(sink.axes.isEmpty()); assertEquals(-1, stick.trackId)
        assertFalse(move(290f, 90f)); assertTrue(sink.axes.isEmpty())
        overlay.onWindowFocusChanged(true)
        down(275f, 75f)
        assertTrue(sink.axes.isNotEmpty())
        overlay.cancelTouches() // controller-hide/activity-pause boundary uses the same owner
        assertTrue(sink.axes.isEmpty()); assertEquals(-1, stick.trackId)
    }

    @Test fun detachCancelsAndLateFocusCallbackAfterUnbindIsHarmless() {
        val parent = android.widget.FrameLayout(activity)
        activity.setContentView(parent)
        parent.addView(overlay)
        assertTrue(overlay.isAttachedToWindow)
        down(50f, 50f)
        parent.removeView(overlay)
        assertTrue(sink.held.isEmpty()); assertEquals(-1, button.trackId)
        assertEquals(0, sink.homeTaps)
        OverlayHost.unbind(sink)
        overlay.onWindowFocusChanged(false)
    }

    @Test fun secondFingerCannotStealAnAlreadyHeldControl() {
        for ((x, y) in listOf(50f to 50f, 120f to 20f, 275f to 75f)) {
            down(x, y)
            event(MotionEvent.ACTION_POINTER_DOWN or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), x to y, x to y)
            event(MotionEvent.ACTION_POINTER_UP or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), x to y, x to y)
            when (x) {
                50f -> { assertEquals(0, button.trackId); assertTrue(AzaharButtonIds.BUTTON_HOME in sink.held) }
                120f -> { assertEquals(0, dpad.trackId); assertTrue(AzaharButtonIds.DPAD_UP in sink.held) }
                else -> { assertEquals(0, stick.trackId); assertTrue(sink.axes.getValue(AzaharButtonIds.STICK_LEFT).first > 0f) }
            }
            up(x, y)
            assertEquals(-1, button.trackId); assertEquals(-1, dpad.trackId); assertEquals(-1, stick.trackId)
        }
        assertEquals(1, sink.homeTaps)
    }

    @Test fun missedTouchAndOtherPointerReleaseDoNotStealHeldControl() {
        assertFalse(down(350f, 150f))
        down(50f, 50f)
        event(MotionEvent.ACTION_POINTER_DOWN or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), 50f to 50f, 275f to 75f)
        event(MotionEvent.ACTION_POINTER_UP or (1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT), 50f to 50f, 275f to 75f)
        assertTrue(AzaharButtonIds.BUTTON_HOME in sink.held)
        assertEquals(0f to 0f, sink.axes[AzaharButtonIds.STICK_LEFT])
        up(50f, 50f)
        assertTrue(sink.held.isEmpty()); assertEquals(1, sink.homeTaps)
    }
}
