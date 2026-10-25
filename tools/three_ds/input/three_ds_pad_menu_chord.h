#pragma once

namespace ThreeDsRecomp::Input {

// Controller routes to the two host menus, from the stick clicks sampled
// every poll: L3+R3 together toggles the settings window (F1) once per
// chord; R3 alone toggles the in-game menu when released, unless L3 was
// pressed at any time during that hold, so the chord never opens both.
class PadMenuChord {
public:
    struct Requests {
        bool ToggleSettings = false;
        bool ToggleMenu = false;
    };

    Requests Update(bool leftStickClick, bool rightStickClick) noexcept {
        Requests requests;
        const bool chord = leftStickClick && rightStickClick;
        if (chord && !mChordHeld) {
            requests.ToggleSettings = true;
        }
        mChordHeld = chord;
        if (rightStickClick) {
            mRightHeld = true;
            mLeftDuringRight = mLeftDuringRight || leftStickClick;
        } else if (mRightHeld) {
            requests.ToggleMenu = !mLeftDuringRight;
            mRightHeld = false;
            mLeftDuringRight = false;
        }
        return requests;
    }

private:
    bool mChordHeld = false;
    bool mRightHeld = false;
    bool mLeftDuringRight = false;
};

} // namespace ThreeDsRecomp::Input
