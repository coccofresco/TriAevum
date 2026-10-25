// The touch overlay's JNI entry points (org.triaevum.android.NativeInputBridge).
// They run on the Android UI thread and only record state in the process's
// on-screen pad; the game's input poll merges it with the physical devices.

#include "three_ds_virtual_pad.h"

#include <jni.h>

namespace {

ThreeDsRecomp::Input::VirtualPad& Pad() { return ThreeDsRecomp::Input::TouchOverlayPad(); }

} // namespace

extern "C" {

// hidMask: one 3DS HID PAD bit (the overlay adapter's vocabulary).
JNIEXPORT void JNICALL Java_org_triaevum_android_NativeInputBridge_button(JNIEnv*, jobject, jint hidMask,
                                                                          jboolean pressed) {
    const auto mask = static_cast<unsigned>(hidMask);
    for (unsigned bit = 0; bit < 32U; ++bit) {
        ThreeDsRecomp::Input::DigitalControl control{};
        if ((mask & (1U << bit)) != 0U && ThreeDsRecomp::Input::DigitalControlForHidBit(bit, &control))
            Pad().SetControl(control, pressed == JNI_TRUE);
    }
}

JNIEXPORT void JNICALL Java_org_triaevum_android_NativeInputBridge_circlePad(JNIEnv*, jobject, jfloat x, jfloat y) {
    Pad().SetCirclePad(x, y);
}

JNIEXPORT void JNICALL Java_org_triaevum_android_NativeInputBridge_cStick(JNIEnv*, jobject, jfloat x, jfloat y) {
    Pad().SetCStick(x, y);
}

JNIEXPORT void JNICALL Java_org_triaevum_android_NativeInputBridge_requestMenu(JNIEnv*, jobject) {
    Pad().RequestMenu();
}

JNIEXPORT void JNICALL Java_org_triaevum_android_NativeInputBridge_requestSettings(JNIEnv*, jobject) {
    Pad().RequestSettings();
}

JNIEXPORT void JNICALL Java_org_triaevum_android_NativeInputBridge_releaseAll(JNIEnv*, jobject) {
    Pad().ReleaseAll();
}

} // extern "C"
