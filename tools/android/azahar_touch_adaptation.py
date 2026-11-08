"""Reproducible post-import touch ownership fixes; no donor files are modified."""

CANCEL_METHODS = {
    "InputOverlayDrawableButton.kt": """    // Cancellation is not a release gesture (notably HOME tap/long press).
    fun cancelTouch() {
        pressedState = false
        isMotionFirstButton = false
        trackId = -1
    }

""",
    "InputOverlayDrawableDpad.kt": """    fun cancelTouch() {
        upButtonState = false
        downButtonState = false
        leftButtonState = false
        rightButtonState = false
        trackId = -1
    }

""",
    "InputOverlayDrawableJoystick.kt": """    fun cancelTouch() {
        pressedState = false
        xAxis = 0.0f
        yAxis = 0.0f
        angle = 0.0f
        radius = 0.0f
        outerBitmap.alpha = opacity
        boundsBoxBitmap.alpha = 0
        virtBounds = Rect(origBounds)
        bounds = Rect(origBounds)
        setInnerBounds()
        trackId = -1
    }

""",
}

OVERLAY_TOUCH = """    // Cancel the sink as a batch, not as synthetic button-up events: releasing
    // HOME normally is a tap and must not open a menu on pause/focus loss.
    fun cancelTouches() {
        gestureActive = false
        overlayButtons.forEach { it.cancelTouch() }
        overlayDpads.forEach { it.cancelTouch() }
        overlayJoysticks.forEach { it.cancelTouch() }
        touchscreenPointerId = -1
        buttonBeingConfigured = null
        dpadBeingConfigured = null
        joystickBeingConfigured = null
        OverlayHost.releaseAll()
        invalidate()
    }

    override fun onWindowFocusChanged(hasWindowFocus: Boolean) {
        super.onWindowFocusChanged(hasWindowFocus)
        if (!hasWindowFocus) cancelTouches()
    }

    override fun onDetachedFromWindow() {
        cancelTouches()
        super.onDetachedFromWindow()
    }

    override fun onTouch(v: View, event: MotionEvent): Boolean {
        val motionEvent = event.actionMasked
        if (motionEvent == MotionEvent.ACTION_CANCEL) {
            cancelTouches()
            return true
        }
        if (motionEvent == MotionEvent.ACTION_DOWN) {
            if (gestureActive) cancelTouches()
            gestureActive = true
        }
        // Discard trailing MOVE/UP events after cancellation or a layout edit.
        if (!gestureActive) return false
        if (motionEvent == MotionEvent.ACTION_UP) gestureActive = false
        if (isInEditMode) {
            return onTouchWhileEditing(event)
        }"""


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError("Azahar touch adaptation no longer applies: " + old.splitlines()[0])
    return source.replace(old, new)


def adapt_touch(name, source):
    if name == "InputOverlay.kt":
        changes = (
            ("    private var touchscreenPointerId = -1", "    private var touchscreenPointerId = -1\n    private var gestureActive = false"),
            ("""    override fun onTouch(v: View, event: MotionEvent): Boolean {
        if (isInEditMode) {
            return onTouchWhileEditing(event)
        }

        val motionEvent = event.action and MotionEvent.ACTION_MASK""", OVERLAY_TOUCH),
            ("""                    // TriAevum: a touch on no control belongs to the game surface below.
                    return false""", """                    // TriAevum: a touch on no control belongs to the game surface below.
                    if (motionEvent == MotionEvent.ACTION_DOWN) gestureActive = false
                    return false"""),
            ("    fun refreshControls() {\n", "    fun refreshControls() {\n        cancelTouches()\n"),
            ("""    fun setIsInEditMode(isInEditMode: Boolean) {
        this.isInEditMode = isInEditMode
    }""", """    fun setIsInEditMode(isInEditMode: Boolean) {
        if (this.isInEditMode == isInEditMode) return
        cancelTouches()
        this.isInEditMode = isInEditMode
        if (!isInEditMode) publishControlBounds()
    }"""),
        )
        for old, new in changes:
            source = replace_once(source, old, new)
        return source

    source = replace_once(source, "if (!bounds.contains(xPosition, yPosition)) {",
                          "if (trackId != -1 || !bounds.contains(xPosition, yPosition)) {")
    anchor = {
        "InputOverlayDrawableButton.kt": "    private fun buttonDown(firstBtn: Boolean, pointerId: Int, overlay: InputOverlay) {",
        "InputOverlayDrawableDpad.kt": "    fun draw(canvas: Canvas) {",
        "InputOverlayDrawableJoystick.kt": "    fun onConfigureTouch(event: MotionEvent): Boolean {",
    }[name]
    source = replace_once(source, anchor, CANCEL_METHODS[name] + anchor)
    if name == "InputOverlayDrawableJoystick.kt":
        source = replace_once(source,
                              "return oldXAxis != this.xAxis && oldYAxis != this.yAxis",
                              "return oldXAxis != this.xAxis || oldYAxis != this.yAxis")
        source = replace_once(source, """            pressedState = false
            xAxis = 0.0f
            yAxis = 0.0f
            angle = 0.0f
            radius = 0.0f
            outerBitmap.alpha = opacity
            boundsBoxBitmap.alpha = 0
            virtBounds = Rect(origBounds.left, origBounds.top, origBounds.right, origBounds.bottom)
            bounds = Rect(origBounds.left, origBounds.top, origBounds.right, origBounds.bottom)
            setInnerBounds()
            trackId = -1""", "            cancelTouch()")
    return source
