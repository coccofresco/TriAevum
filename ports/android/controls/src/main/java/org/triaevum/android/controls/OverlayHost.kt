package org.triaevum.android.controls

import android.content.Context

/** Platform boundary only: the donor draws controls; the app owns input routing. */
interface OverlayInputSink {
    fun button(id: Int, pressed: Boolean): Boolean
    fun stick(id: Int, x: Float, y: Float): Boolean
    // Coordinates are overlay-local pixels, never guest touchscreen coordinates.
    // The app must use the same inverse presentation mapping as native UI input.
    fun touch(x: Float, y: Float, pressed: Boolean)
    fun touchMoved(x: Float, y: Float)
    fun swapScreens(enabled: Boolean, displayRotation: Int)
    fun toggleTurbo(fromOverlay: Boolean)
    fun releaseAll()
}

object OverlayHost {
    const val TouchScreenDevice = "Touchscreen"
    private var applicationContext: Context? = null
    private var sink: OverlayInputSink? = null
    val appContext: Context get() = checkNotNull(applicationContext) {
        "Bind the Android overlay host before constructing InputOverlay"
    }

    // UI-thread lifecycle. The sink queues/merges input on the runtime side; it
    // must not execute gameplay or mutate the renderer from an Android callback.
    fun bind(context: Context, input: OverlayInputSink) {
        check(sink == null) { "An Android input overlay is already bound" }
        applicationContext = context.applicationContext
        sink = input
    }

    fun unbind(input: OverlayInputSink) {
        if (sink !== input) return
        input.releaseAll()
        sink = null
    }

    private fun input() = checkNotNull(sink) { "Android input overlay is not bound" }
    fun releaseAll() = input().releaseAll()
    fun onGamePadEvent(device: String, id: Int, state: Int): Boolean {
        require(device == TouchScreenDevice)
        // These are presentation/host actions, dispatched separately by Azahar.
        if (id == AzaharButtonIds.BUTTON_SWAP || id == AzaharButtonIds.BUTTON_TURBO) return true
        return input().button(id, state == AzaharButtonState.PRESSED)
    }
    fun onGamePadMoveEvent(device: String, id: Int, x: Float, y: Float): Boolean {
        require(device == TouchScreenDevice)
        return input().stick(id, x, y)
    }
    fun onTouchEvent(x: Float, y: Float, pressed: Boolean) = input().touch(x, y, pressed)
    fun onTouchMoved(x: Float, y: Float) = input().touchMoved(x, y)
    fun swapScreens(enabled: Boolean, rotation: Int) = input().swapScreens(enabled, rotation)
    fun toggleTurbo(fromOverlay: Boolean) = input().toggleTurbo(fromOverlay)
}
