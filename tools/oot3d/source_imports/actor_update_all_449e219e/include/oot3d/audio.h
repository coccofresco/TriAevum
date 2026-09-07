#ifndef OOT3D_AUDIO_H
#define OOT3D_AUDIO_H

#include "oot3d/types.h"

enum {
    OOT3D_ACTOR_AUDIO_POSITION_OFFSET = 0x28,
    OOT3D_AUDIO_ACTOR_SFX_TOKEN = 4,
    OOT3D_AUDIO_SFX_BANK_COUNT = 7,
    OOT3D_AUDIO_SFX_SLOT_SIZE = 0xA0,
    OOT3D_AUDIO_SFX_PARAMS_SIZE = 0x18,
};

enum {
    OOT3D_SFX_FLAG_RAMP = 0x0008,
    OOT3D_SFX_FLAG_LIMIT_POSITION = 0x0020,
    OOT3D_SFX_FLAG_RANDOM_PITCH_MASK = 0x00C0,
    OOT3D_SFX_FLAG_REQUIRE_ACCEPT = 0x0100,
    OOT3D_SFX_FLAG_SLOT_98 = 0x4000,
};

typedef struct Oot3dAudioVec3f {
    float x;
    float y;
    float z;
} Oot3dAudioVec3f;

/*
 * Prefix-only actor view used by the native sound adapter. The full actor
 * layout remains subsystem work; this view fixes only the observed position.
 */
typedef struct Oot3dActorAudioView {
    u8 unk_00[OOT3D_ACTOR_AUDIO_POSITION_OFFSET];
    Oot3dAudioVec3f worldPosition;
} Oot3dActorAudioView;

typedef struct Oot3dAudioSfxMetadata {
    u16 unk_00;
    u16 flags;
} Oot3dAudioSfxMetadata;

typedef struct Oot3dAudioSfxParams {
    u32 unk_00;
    u32 unk_04;
    u32 unk_08;
    u32 priority;
    u32 unk_10;
    u8 unk_14;
    u8 unk_15;
    u8 unk_16;
    u8 unk_17;
} Oot3dAudioSfxParams;

typedef struct Oot3dAudioRuntime {
    u32 unk_00;
    u32 resourceContext;
} Oot3dAudioRuntime;

typedef struct Oot3dAudioSfxSlot {
    u8 unk_00[0x83];
    u8 bank;
    u32 position;
    u32 frequencyScale;
    u32 volumeScale;
    u32 reverb;
    float pitch;
    u8 flag_98;
    u8 flag_99;
    u8 flag_9A;
    u8 unk_9B;
    u32 handle;
} Oot3dAudioSfxSlot;

typedef u32 (*Oot3dAudioSfxStartCallback)(
    void* engine,
    u32* handle,
    u32 sfxId,
    s32 mode,
    u32 argument
);

typedef void (*Oot3dAudioRampUnaryCallback)(void* object);

#if defined(__cplusplus)
static_assert(
    offsetof(Oot3dActorAudioView, worldPosition) ==
        OOT3D_ACTOR_AUDIO_POSITION_OFFSET,
    "audio actor position offset"
);
static_assert(sizeof(Oot3dAudioSfxMetadata) == 0x04, "SFX metadata size");
static_assert(sizeof(Oot3dAudioSfxParams) == OOT3D_AUDIO_SFX_PARAMS_SIZE, "SFX params size");
static_assert(sizeof(Oot3dAudioSfxSlot) == OOT3D_AUDIO_SFX_SLOT_SIZE, "SFX slot size");
static_assert(offsetof(Oot3dAudioSfxSlot, position) == 0x84, "SFX slot position offset");
static_assert(offsetof(Oot3dAudioSfxSlot, pitch) == 0x94, "SFX slot pitch offset");
static_assert(offsetof(Oot3dAudioSfxSlot, handle) == 0x9C, "SFX slot handle offset");
#else
_Static_assert(
    offsetof(Oot3dActorAudioView, worldPosition) ==
        OOT3D_ACTOR_AUDIO_POSITION_OFFSET,
    "audio actor position offset"
);
_Static_assert(sizeof(Oot3dAudioSfxMetadata) == 0x04, "SFX metadata size");
_Static_assert(sizeof(Oot3dAudioSfxParams) == OOT3D_AUDIO_SFX_PARAMS_SIZE, "SFX params size");
_Static_assert(sizeof(Oot3dAudioSfxSlot) == OOT3D_AUDIO_SFX_SLOT_SIZE, "SFX slot size");
_Static_assert(offsetof(Oot3dAudioSfxSlot, position) == 0x84, "SFX slot position offset");
_Static_assert(offsetof(Oot3dAudioSfxSlot, pitch) == 0x94, "SFX slot pitch offset");
_Static_assert(offsetof(Oot3dAudioSfxSlot, handle) == 0x9C, "SFX slot handle offset");
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* DAT_0054AC20 and DAT_0054AC24. */
extern float gOot3dSfxDefaultFreqAndVolScale;
extern s8 gOot3dSfxDefaultReverb;
extern u32 gOot3dSfxRandomState;
extern u8 gOot3dSfxBankDisabled[OOT3D_AUDIO_SFX_BANK_COUNT];
extern u8 gOot3dSfxBankLimits[OOT3D_AUDIO_SFX_BANK_COUNT];
extern uintptr_t
    gOot3dSfxMetadataTables[OOT3D_AUDIO_SFX_BANK_COUNT];

#if defined(OOT3D_HOST_AUDIO_SFX_PTR32_RESOLVER)
void* oot3d_host_audio_sfx_ptr32_resolve(u32 address);
u32 oot3d_host_audio_sfx_ptr32_encode(const void* pointer);
#endif

#if defined(OOT3D_HOST_AUDIO_SFX_CALLBACK_RESOLVER)
Oot3dAudioSfxStartCallback
oot3d_host_audio_sfx_start_callback_resolve(u32 address);
#endif

#if defined(OOT3D_HOST_AUDIO_RAMP_PTR32_RESOLVER)
void* oot3d_host_audio_ramp_ptr32_resolve(u32 address);
u32 oot3d_host_audio_ramp_ptr32_encode(const void* pointer);
#endif

#if defined(OOT3D_HOST_AUDIO_RAMP_CALLBACK_RESOLVER)
Oot3dAudioRampUnaryCallback
oot3d_host_audio_ramp_unary_callback_resolve(u32 address);
#endif

s32 Oot3d_AudioGetSfxBank(u32 sfxId);
s32 Oot3d_AudioGetSfxMode(u32 sfxId);
s32 Oot3d_AudioGetSfxIndex(s32 bank, u32 sfxId);
Oot3dAudioRuntime* Oot3d_AudioGetRuntime(void);
s32 Oot3d_AudioGetSfxParams(
    u32 resourceContext,
    u32 sfxId,
    Oot3dAudioSfxParams* params
);
Oot3dAudioSfxSlot* Oot3d_AudioFindSfxSlot(
    Oot3dAudioVec3f* position,
    u32 sfxId
);
Oot3dAudioSfxSlot* Oot3d_AudioAllocSfxSlot(
    Oot3dAudioVec3f* position,
    s32 bank,
    u8 limit,
    u32 priority
);
s32 Oot3d_AudioHasFlag20Conflict(
    Oot3dAudioVec3f* position,
    u32 sfxId
);
s32 Oot3d_AudioRequestFlag100(u32 sfxId);
u8 Oot3d_AudioStartSfxSlot(
    Oot3dAudioSfxSlot* slot,
    u32 sfxId,
    u32 arg
);
u8 Oot3d_AudioRefreshSfxSlot(
    Oot3dAudioSfxSlot* slot,
    u32 sfxId,
    u32 arg
);
void Oot3d_AudioSetChannelRamp(
    s32 channel,
    s32 index,
    u32 target,
    s32 duration
);
void Oot3d_AudioSetGlobalRamp(u32 target, s32 duration);
float FUN_0030b44c(const void* ramp);
void FUN_002d4a10(void* object, s32 duration, float target);
void FUN_003102dc(void* object, s32 value);
void FUN_0033c950(void);

u32 FUN_002dd384(
    void* engine,
    u32* handle,
    u32 sfxId,
    u32 argument
);
u32 FUN_002dd484(
    void* engine,
    u32* handle,
    u32 sfxId,
    u32 argument
);

void Audio_PlaySoundGeneral(
    u32 sfxId,
    Oot3dAudioVec3f* position,
    u8 token,
    float* frequencyScale,
    float* volumeScale,
    s8* reverb
);
void Audio_PlayActorSound2(
    Oot3dActorAudioView* actor,
    u32 sfxId
);

#ifdef __cplusplus
}
#endif

#endif
