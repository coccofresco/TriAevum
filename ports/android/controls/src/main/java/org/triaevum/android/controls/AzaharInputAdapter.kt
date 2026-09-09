// Axis normalization adapted from Citra/Azahar's Android JNI, GPL-2.0-or-later.
// TriAevum adaptation, 2026-09-09. See license.txt and donor_manifest.json.
package org.triaevum.android.controls

import kotlin.math.sqrt

/** Same HID bit vocabulary as TriAevum's InputStateV1, not Android keycodes. */
interface Native3dsInputTarget {
    fun button(hidMask: Int, pressed: Boolean)
    fun circlePad(x: Float, y: Float)
    fun cStick(x: Float, y: Float)
    fun home(pressed: Boolean)
    fun touchPixels(x: Float, y: Float, pressed: Boolean)
    fun moveTouchPixels(x: Float, y: Float)
    fun swapScreens(enabled: Boolean, displayRotation: Int)
    fun toggleTurbo(fromOverlay: Boolean)
    // Release this overlay source only; preserve physical controller input.
    fun releaseAll()
}

class AzaharInputAdapter(private val target: Native3dsInputTarget) : OverlayInputSink {
    override fun button(id: Int, pressed: Boolean): Boolean {
        if (id == AzaharButtonIds.BUTTON_HOME) {
            target.home(pressed)
            return true
        }
        val bit = when (id) {
            AzaharButtonIds.BUTTON_A -> 0
            AzaharButtonIds.BUTTON_B -> 1
            AzaharButtonIds.BUTTON_SELECT -> 2
            AzaharButtonIds.BUTTON_START -> 3
            AzaharButtonIds.DPAD_RIGHT -> 4
            AzaharButtonIds.DPAD_LEFT -> 5
            AzaharButtonIds.DPAD_UP -> 6
            AzaharButtonIds.DPAD_DOWN -> 7
            AzaharButtonIds.TRIGGER_R -> 8
            AzaharButtonIds.TRIGGER_L -> 9
            AzaharButtonIds.BUTTON_X -> 10
            AzaharButtonIds.BUTTON_Y -> 11
            AzaharButtonIds.BUTTON_DEBUG -> 12
            AzaharButtonIds.BUTTON_GPIO14 -> 13
            AzaharButtonIds.BUTTON_ZL -> 14
            AzaharButtonIds.BUTTON_ZR -> 15
            else -> return false
        }
        target.button(1 shl bit, pressed)
        return true
    }

    override fun stick(id: Int, x: Float, y: Float): Boolean {
        if (!x.isFinite() || !y.isFinite()) return false
        // Azahar native.cpp onGamePadMoveEvent flips Android-down Y once,
        // then clamps to the unit circle. Preserve that JNI normalization.
        var nativeX = x.coerceIn(-1f, 1f)
        var nativeY = (-y).coerceIn(-1f, 1f)
        val length = sqrt(nativeX * nativeX + nativeY * nativeY)
        if (length > 1f) { nativeX /= length; nativeY /= length }
        when (id) {
            AzaharButtonIds.STICK_LEFT -> target.circlePad(nativeX, nativeY)
            AzaharButtonIds.STICK_C -> target.cStick(nativeX, nativeY)
            else -> return false
        }
        return true
    }

    override fun touch(x: Float, y: Float, pressed: Boolean) = target.touchPixels(x, y, pressed)
    override fun touchMoved(x: Float, y: Float) = target.moveTouchPixels(x, y)
    override fun swapScreens(enabled: Boolean, displayRotation: Int) = target.swapScreens(enabled, displayRotation)
    override fun toggleTurbo(fromOverlay: Boolean) = target.toggleTurbo(fromOverlay)
    override fun releaseAll() = target.releaseAll()
}
