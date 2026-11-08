# Audio settings

UX plan WP-06C. The host had no volume control: the game's DSP mix went
straight to the output player. This adds a master volume, Mute and "Mute when
the window loses focus", with one owner, one F1 surface (Game > Audio) and
the matching rows in the in-game menu's Game tab, on desktop and Android.

## What the setting is, and what it is not

- `NativeAudioCue::MasterVolume` (`tools/oot3d/native_game_runtime/
  oot3d_native_audio.h`) is the guest sequence player's per-note master
  volume, decoded from the sound archive. It is title data, never a user
  setting, and this work does not touch it.
- The user's volume is a **host output policy**: it scales the finished
  stereo PCM16 that the guest DSP mix produced, at the output player, after
  the game has mixed it. The guest DSP state, the guest PCM, save states,
  timelines, captures and every guest digest are unchanged by it.

## Owner

`Ship::AudioOutputPolicy` (`runtime/three_ds_recomp/include/ship/audio/
AudioOutputPolicy.h`) is the only owner. It is title-neutral host code: it
lives in the host audio service, `Ship::Audio`, which already owns the output
player (`Ship::AudioPlayer`) and its backend choice.

- `Ship::Audio::Init` creates the policy with a persistence port backed by the
  runtime configuration (`Ship::Config`, the `Audio` block) and binds every
  output player it creates to it, so a backend or channel change keeps the
  gain.
- The F1 section and the menu rows read the policy's settings and change them
  only through `AudioOutputPolicy::Apply`, which validates, applies at once
  and saves. A failed save keeps the value for the session and reports the
  error; `RetrySave` writes it again (the same contract as the TopScreen and
  graphics owners).
- Nothing else writes the `Audio` block.

### Settings and persistence

| Key (runtime configuration) | Type | Default | Meaning |
| --- | --- | --- | --- |
| `Audio.MasterVolume` | integer 0..100 | 100 | Master volume in percent |
| `Audio.Muted` | bool | false | Silence the output |
| `Audio.MuteWhenUnfocused` | bool | false | Silence while the game window does not have the keyboard focus (desktop) |

The runtime configuration is the platform's file: `config/TriAevum.json` on
desktop, `TriAevum.android.json` on Android (both given to the runtime with
`--config` by Forge, `forge-core/src/publish.rs`). Forge creates the file
only when it is missing, so the block survives updates. A value outside the
range is clamped, a value of the wrong type falls back to its default; the
block is written only when the user changes a setting. Without a
configuration (`Ship::Config` absent) the policy is "session only".

The volume is a device preference, not part of a game moment: save states
"with settings" do not carry it and loading one never changes it.

### Gain law

The effective gain is 0 when muted (or unfocused with Mute when unfocused on),
otherwise `(MasterVolume / 100)^2`, held as an unsigned Q16 integer
`round(v * v * 65536 / 10000)`: 100 % is exactly 65536 (unity), 50 % is
16384 (0.25, about -12 dB, roughly half the perceived loudness), 0 % is 0.
The square law makes the slider steps sound even; a linear law would leave
most of the audible change in the bottom fifth of the range.

## Where the gain is applied

`Ship::AudioPlayer::Play` (`runtime/three_ds_recomp/src/ship/audio/
AudioPlayer.cpp`) is the one entry every backend shares (WASAPI, SDL - the
Android backend - CoreAudio, Null), called from the window frame
(`oot3d_native_a32_window_frame.cpp`) and from the module host's audio
service (`triaevum_audio_player_backend.cpp`). The gain stage
(`Ship::AudioOutputGain`) runs there on the stereo input, before the optional
5.1 matrix decoder and before the backend:

- **Inert at 100 %.** While the current and target gains are both unity the
  stage returns without touching or copying the buffer, so the bytes given to
  the backend are the guest's own (proved by the digest test below).
- **Integer, deterministic.** `out = (s * g + 32768) >> 16` in 64-bit, clamped
  to PCM16, the same on x64 and AArch64.
- **Ramp.** A change of target ramps linearly per stereo frame over 20 ms of
  output (sample rate / 50 frames), from the gain in effect, so a change
  never clicks; a change during a ramp starts a new ramp from where it is.
  `Flush` (queued audio discarded) snaps to the target, there being nothing
  to ramp from.
- The player keeps an FNV-1a 64 digest of the bytes it passed on (after the
  gain) and the count of frames it scaled; the run summary reports them next
  to the guest PCM digest (`dsp_audio.output_pcm_fnv1a64`,
  `dsp_audio.output_gain_q16`, `dsp_audio.output_frames_scaled`).

## Music / SFX split: not offered

The guest mixer exposes no verified category. The DSP HLE
(`tools/oot3d/third_party/azahar_audio/audio_core/hle/`) mixes 24 voice
sources into three intermediate mixes with per-source gains
(`source.cpp` `MixInto`, `shared_memory.h` `gain[3][4]`); the two auxiliary
mixes are effect sends (reverb/delay returns, `mixers.cpp`
`aux_return_volume`), not music and sound effects. Which voice plays music
and which plays an effect is decided by the guest's own sound player (NW4C
sequence, stream and wave-sound players allocating DSP voices dynamically).
Splitting would need a title-specific, verified map from the guest's sound
handles to DSP voices every frame, which does not exist. The host therefore
offers the master volume only; a split stays out until such a map is
verified in the title adapter.

## Surfaces

One F1 surface: **Game > Audio** (`tools/oot3d/native_game_runtime/
triaevum_audio_settings_panel.cpp`), an application section beside Save
states and Language:

- "Master volume" slider 0..100 % (applies while dragged, saved once on
  release, as the graphics sliders), "Mute", and on desktop "Mute when the
  window loses focus".
- Status: "Audio: saved", "Audio: session only", or the save error with a
  "Retry saving audio" button. Without an output player the controls stay
  visible and the section says the output is unavailable (the setting still
  saves).

The in-game menu's **Game** tab gets an **Audio** heading with "Master
volume" (a number, 5 % steps), "Mute" and, on desktop, "Mute when unfocused".
The rows are built by `tools/oot3d/ui_menu/oot3d_menu_audio.cpp` from
callbacks and bound by `InGameMenu` to the owner; they report the menu's
status vocabulary (Saved, Session only, Could not save: Retry).

### Feedback while the game is paused

The game is held behind the menu (decision 3b), so no guest audio runs there.
After a volume change from the menu, the host plays a short preview tone
(120 ms, 660 Hz, -12 dBFS before the gain, 10 ms fades) through the same
player and gain stage, so it is heard at the chosen level. It is generated by
the host (`Ship::GenerateAudioPreviewTone`), runs no guest code, is queued
only while the game is held and the player's queue is short (a held arrow key
does not pile tones up), and is never played when muted. F1 does not hold the
game, so the game's own sound is the feedback there.

Scripted fixed-delta runs (`--fixed-delta-seconds`) queue game audio faster than real
time, so the output queue can stay seconds long behind the menu. There the
first tone of a hold is queued after that backlog at once, and a later tone
waits only until the previous tone has drained to under 50 ms
(`Ship::AudioPreviewToneMayQueue`, covered by `audio_output_policy_tests`).
The rule decides only about the tone; the game's audio and its bytes are
unchanged.

## Mute when unfocused

The window frame tells the owner each frame whether the game window has the
keyboard focus (`SDL_GetKeyboardFocus`); the owner turns that into a target
gain of 0 only when the setting is on. Off by default, so scripted runs and
captures are unaffected. Not offered on Android, where the activity pauses
in the background.

## Real-time native clock and restore diagnostics

Real-time audio uses the existing bounded native-refresh catch-up policy in
`NativePresentationScheduler`, in both Native30 and Enhanced60 modes. Ordinary
30 Hz presentation jitter must not discard a native DSP refresh permanently:
25 ms followed by 41.667 ms still represents four 60 Hz refreshes. Work remains
bounded to three refreshes per presentation and eight deferred refreshes: the
third refresh provides recovery capacity above a paced 30 Hz host's normal two.
The explicit Switch slow-host policy stays unchanged (one refresh, no new
Native30 debt). Longer stalls can still exhaust the bound and underrun; this is not concealment by
resampling, time stretching or increased device latency. Guest timing fields,
native mixer/effects and PCM processing are unchanged.

This is recovery capacity, not a slow-host continuity guarantee. With a batch
limit of three, fewer than 20 host presentations per second cannot supply 60
native refreshes per second even before stalls. A larger fixed limit alone can
create a catch-up spiral; diagnose guest/render costs and resource contention
before changing it. Report dropped native time separately from device underruns.

Fixed-delta and throughput runs keep their previous catch-up policy. A bounded
`--frames N` run disables the real-time presentation pacer; for device timing
acceptance use `--frames 0 --max-seconds N`, without fixed delta. An unpaced
capture is not a real-time device qualification.

For scripted state round trips, bind `--quick-state <file>` and use pathless
`--savestate-action FRAME:save` / `FRAME:load`. A request path suffix names a
load source, not a save destination. Runtime audio queue checkpoints record
cumulative PCM offsets and underruns at successful saves/loads, allowing exact
native-tail replay comparisons. Loading flushes stale device audio; diagnostics
restart priming and submission-gap tracking without clearing cumulative errors.
These are native pre-policy PCM receipts, not physical-device recordings.
Frame-based actions may not be reached before a real-time wall limit on a slow
host. A successful process exit does not prove that the scheduled load or
screenshot happened; inspect actual action counters and artifacts.

`tools/oot3d/qualify_audio_restore.py --runtime runtime.json --pcm native.wav
--output replay.json` validates capture rate/extent/nonzero count/FNV against the
runtime, exactly one save/subsequent live load, restored host frame, and at least
five seconds of non-silent byte-identical native replay. It also accepts WAV.gz.
Silent equal tails, missing actions and short/mismatched captures fail. Its exit
code describes **native replay only**: pacing, backend and all underruns are
reported separately under `device_queue`. A fixed-clock native replay can pass
while real-time playback fails. Even a non-silent pass proves no particular
room/effect tail unless that native scenario was independently established.

## Verification

- `triaevum_audio_output_tests`: gain law, codec clamping, owner apply/save
  failure/retry/session-only, focus mute, backend rebinding, and the
  **digest test**: a deterministic 10 s stereo PCM16 signal through a real
  `AudioPlayer` (capturing backend) in DSP-sized chunks. At 100 % the
  output bytes and FNV-1a digest equal the input's; at 50 % every sample
  after the 20 ms ramp equals `(s * 16384 + 32768) >> 16` and the digest
  equals that reference's; the ramp is monotonic with steps of at most one
  ramp increment; mute reaches exact silence.
- In game: one run at 100 % where `output_pcm_fnv1a64 == pcm_fnv1a64`, and
  one at 50 % where the guest `pcm_fnv1a64` is unchanged and the output
  digest differs.
- `oot3d_menu_tests` (the audio rows, steps, mute availability, failures) and
  the F1 actual-widget smoke (`CheckAudioPanel`).
