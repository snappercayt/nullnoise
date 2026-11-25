# NullNoise

NullNoise is an Android voice-recording app built with C and the Android NDK. It lets you record audio, save clips locally, and apply a one-tap noise-reduction and voice-enhancement pass.

> **Welcome to the NullNoise v0.1.7 beta!** [Download the APK](https://github.com/snappercayt/nullnoise/releases/download/v0.1.7/NullNoise-v0.1.7.apk)
> or read the [beta release notes](https://github.com/snappercayt/nullnoise/releases/tag/v0.1.7).
> This release unifies the capture engine and hardens the automated checks;
> early feedback is that it works noticeably better than v0.1.6. Your original
> clip is always kept unchanged. **Known beta issue:** enhancement may still
> cut off word endings, so compare both versions before relying on the
> enhanced clip. This is a debug-signed prototype, not for important recordings.

## Project goals

- Make voice recording quick and reliable.
- Keep recordings on the device by default.
- Make speech clearer with a simple, one-tap processing workflow.
- Keep the audio-processing core in portable C where practical.

## User workflow

1. Tap **Record** and **Stop** to capture a clip.
2. Play or pause the selected recording.
3. Tap **NullNoise** to create an enhanced copy.
4. Switch between the original and enhanced clips without overwriting either.

The original recording should remain unchanged when an enhanced copy is created.

## Features

- Request microphone access when recording is first started and begin recording
  immediately after the user grants it.
- Capture mono 48 kHz PCM through miniaudio's Android voice-recognition input
  preset and save it in the app's private storage.
- Preserve the original recording and automatically create an enhanced copy
  with RNNoise after recording.
- Run RNNoise on demand with **NullNoise**, producing a separate normalized
  `_NN.wav` file while preserving the source clip.
- Play, pause, and switch between the original and enhanced versions.
- Show a playback timeline driven by Android `AudioTrack` playback-head
  position, with elapsed and total time.
- Use raylib for the C-rendered recording/player screen, touch input, and
  Android NativeActivity lifecycle.

## Technical overview

NullNoise is an Android NativeActivity written in C. It uses
[raylib](https://github.com/raysan5/raylib) for rendering and touch input, and
[rawdrawandroid](https://github.com/cnlohr/rawdrawandroid) for NativeActivity
glue and APK packaging. The v0.1.7 release records through Android
`AudioRecord` with an optional `NoiseSuppressor` session effect, plays back
through Android `AudioTrack`, and enhances with RNNoise. An experimental
miniaudio voice-recognition capture path exists as a separate opt-in CI build
but is not part of this release. The app targets Android API 35 and compiles
native code against API 30.

The custom raylib canvas does not currently expose semantic controls to
TalkBack; full screen-reader support needs native accessibility integration.

Beta recordings use the `_RAW.wav` suffix, and RNNoise creates a paired
`_NN.wav` file after recording stops. Files remain in the app's internal files
directory and are not exported automatically.

## Privacy

Recordings are intended to stay on-device. Do not add network upload or analytics involving audio without an explicit product decision and clear user disclosure.

## Repository status

NullNoise v0.1.7 is an early beta with a single-screen record, playback, and
enhancement flow. Recording, enhancement, playback, and sample continuity pass
the Android 15 emulator smoke test, and every build runs host compile checks
for both capture backends. Early feedback is that v0.1.7 works much better
than the previous beta, with more reliable capture and playback. Some word
endings may still be cut off by enhancement; keep the original recording and
review both versions.

### Releases

Browse the [NullNoise GitHub Releases](https://github.com/snappercayt/nullnoise/releases)
page for published APKs and release notes. New releases appear there
automatically. For non-release builds, see the [Android workflow runs](https://github.com/snappercayt/nullnoise/actions/workflows/android.yml);
their APK artifacts are temporary and are not published releases. APKs are
debug-signed beta prototypes, not production builds. The current beta is
[v0.1.7](https://github.com/snappercayt/nullnoise/releases/tag/v0.1.7).

## Roadmap

Planned improvements, ordered by impact. The full review input is in
`docs/development/ARCHITECTURE-REVIEW.md`.

Completed:

- **Host-test the audio engine.** WAV + RNNoise normalization live in a
  portable core with a host unit, and `scripts/test-local.sh` now compiles
  `audio.c` and `audio_capture.c` with `-Werror` in both backend configurations.
- **Replace the `#if` capture duality.** `audio.c` calls one `audio_capture`
  API; the `AudioRecord` and miniaudio implementations are thin backends in
  `audio_capture.c`. Only `src/miniaudio_impl.c` is conditional in the Makefile.
- **Fix the head-position cast.** Playback head position is compared in 64-bit
  instead of being truncated to `uint32_t`.
- **Harden CI and the emulator smoke test.** Validated action versions,
  restored the release gate, and fixed flaky timing checks.

Remaining:

1. **Shrink the native library for a 1 GB RAM device.** Add
   `-ffunction-sections -fdata-sections -Wl,--gc-sections` to `LDFLAGS`
   so unreferenced miniaudio/raylib backends are dropped.
2. **Tune capture and playback buffers.** `CAPTURE_BUFFER_FRAMES` (~384 KB) and
   the `AudioTrack` stream buffer are fixed magic numbers. Make them named and
   device-aware.
3. **Temp file + rename for enhanced output.** Replace the in-place seek
   rewrite so a partial `_NN.wav` is never surfaced as complete.
4. **Revisit the 390 px UI scaling target.** Confirm the phone-oriented base
   width is deliberate, or move to a responsive layout.

## Contributing and project policies

Contributions are welcome. See the [contribution guide](CONTRIBUTING.md), the
[Code of Conduct](CODE_OF_CONDUCT.md), and the [security policy](SECURITY.md).
Please use the GitHub issue forms for bug reports and feature requests.

## License

NullNoise is licensed under the [MIT License](LICENSE).

## Inspiration and credits

NullNoise uses [raylib](https://github.com/raysan5/raylib), created by
Ramon Santamaria, for its C-based Android rendering and NativeActivity
integration. Raylib's zlib/libpng license is included in
`third_party/raylib/LICENSE`. [rawdrawandroid](https://github.com/cnlohr/rawdrawandroid),
created by [CNLohr](https://github.com/cnlohr), supplies NativeActivity glue
and APK packaging and is MIT-licensed. Upstream copyright and license notices
are retained in their submodules.

Noise reduction uses [RNNoise](https://github.com/xiph/rnnoise), which is
distributed under the BSD 3-Clause License. Its license and copyright notices
are included in the RNNoise submodule at `third_party/rnnoise/COPYING`.
