package org.triaevum.android.controls

import org.junit.Assert.*
import org.junit.Test

class AzaharInputAdapterTest {
    private class Target : Native3dsInputTarget {
        var buttons = 0
        var left = 0f to 0f
        var right = 0f to 0f
        var home = false
        var touch: Triple<Float, Float, Boolean>? = null
        var screen: Pair<Boolean, Int>? = null
        var turbo = false
        override fun button(hidMask: Int, pressed: Boolean) {
            buttons = if (pressed) buttons or hidMask else buttons and hidMask.inv()
        }
        override fun circlePad(x: Float, y: Float) { left = x to y }
        override fun cStick(x: Float, y: Float) { right = x to y }
        override fun home(pressed: Boolean) { home = pressed }
        override fun touchPixels(x: Float, y: Float, pressed: Boolean) { touch = Triple(x, y, pressed) }
        override fun moveTouchPixels(x: Float, y: Float) { touch = Triple(x, y, touch?.third ?: false) }
        override fun swapScreens(enabled: Boolean, displayRotation: Int) { screen = enabled to displayRotation }
        override fun toggleTurbo(fromOverlay: Boolean) { turbo = fromOverlay }
        override fun releaseAll() { buttons = 0; left = 0f to 0f; right = left; home = false; touch = null }
    }

    @Test fun allNativeButtonsMatchTheExistingHidContract() {
        val target = Target()
        val adapter = AzaharInputAdapter(target)
        val buttons = listOf(700, 701, 705, 704, 712, 711, 709, 710,
                             774, 773, 702, 703, 781, 782, 707, 708)
        buttons.forEachIndexed { bit, id ->
            assertTrue(adapter.button(id, true))
            assertEquals(1 shl bit, target.buttons)
            assertTrue(adapter.button(id, false))
            assertEquals(0, target.buttons)
        }
        assertFalse(adapter.button(AzaharButtonIds.BUTTON_SWAP, true))
        assertFalse(adapter.button(AzaharButtonIds.BUTTON_TURBO, true))
        assertFalse(adapter.button(-1, true))
    }

    @Test fun independentInputsDoNotReleaseOtherButtonsOrSticks() {
        val target = Target()
        val adapter = AzaharInputAdapter(target)
        adapter.button(AzaharButtonIds.BUTTON_A, true)
        adapter.button(AzaharButtonIds.BUTTON_B, true)
        adapter.stick(AzaharButtonIds.STICK_LEFT, .25f, -.5f)
        adapter.stick(AzaharButtonIds.STICK_C, -.5f, .25f)
        adapter.button(AzaharButtonIds.BUTTON_A, false)
        assertEquals(2, target.buttons)
        assertEquals(.25f to .5f, target.left)
        assertEquals(-.5f to -.25f, target.right)
        adapter.releaseAll()
        assertEquals(0, target.buttons)
        assertEquals(0f to 0f, target.left)
        assertEquals(0f to 0f, target.right)
    }

    @Test fun axesMatchAzaharJniNormalization() {
        val target = Target()
        val adapter = AzaharInputAdapter(target)
        adapter.stick(AzaharButtonIds.STICK_LEFT, 2f, -2f)
        assertEquals(1f, target.left.first * target.left.first + target.left.second * target.left.second, 0.00001f)
        assertTrue(target.left.second > 0f)
        val before = target.left
        assertFalse(adapter.stick(AzaharButtonIds.STICK_LEFT, Float.NaN, 0f))
        assertFalse(adapter.stick(999, 0f, 0f))
        assertEquals(before, target.left)
    }

    @Test fun touchAndHostActionsRemainOutsideHidButtons() {
        val target = Target()
        val adapter = AzaharInputAdapter(target)
        adapter.touch(800f, 100f, true)
        adapter.touchMoved(802f, 104f)
        assertEquals(Triple(802f, 104f, true), target.touch)
        adapter.button(AzaharButtonIds.BUTTON_HOME, true)
        adapter.swapScreens(true, 3)
        adapter.toggleTurbo(true)
        assertEquals(0, target.buttons)
        assertTrue(target.home)
        assertTrue(target.turbo)
        assertEquals(true to 3, target.screen)
    }
}
