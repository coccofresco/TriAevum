// TriAevum adaptation, 2026-09-09: namespace and host boundary; see donor_manifest.json.
// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.triaevum.android.controls

import androidx.preference.PreferenceManager

object EmulationMenuSettings {
    private val preferences =
        PreferenceManager.getDefaultSharedPreferences(OverlayHost.appContext)

    var joystickRelCenter: Boolean
        get() = preferences.getBoolean("EmulationMenuSettings_JoystickRelCenter", true)
        set(value) {
            preferences.edit()
                .putBoolean("EmulationMenuSettings_JoystickRelCenter", value)
                .apply()
        }
    var dpadSlide: Boolean
        get() = preferences.getBoolean("EmulationMenuSettings_DpadSlideEnable", true)
        set(value) {
            preferences.edit()
                .putBoolean("EmulationMenuSettings_DpadSlideEnable", value)
                .apply()
        }
    var buttonSlide: Int
        get() = preferences.getInt("EmulationMenuSettings_ButtonSlideMode", ButtonSlidingMode.Disabled.int)
        set(value) {
            preferences.edit()
                .putInt("EmulationMenuSettings_ButtonSlideMode", value)
                .apply()
        }

    var hapticFeedback: Boolean
        get() = preferences.getBoolean("EmulationMenuSettings_HapticFeedback", true)
        set(value) {
            preferences.edit()
                    .putBoolean("EmulationMenuSettings_HapticFeedback", value)
                    .apply()
        }
    var swapScreens: Boolean
        get() = preferences.getBoolean("EmulationMenuSettings_SwapScreens", false)
        set(value) {
            preferences.edit()
                .putBoolean("EmulationMenuSettings_SwapScreens", value)
                .apply()
        }
    var showOverlay: Boolean
        get() = preferences.getBoolean("EmulationMenuSettings_ShowOverlay", true)
        set(value) {
            preferences.edit()
                .putBoolean("EmulationMenuSettings_ShowOverlay", value)
                .apply()
        }
}
