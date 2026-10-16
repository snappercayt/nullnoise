# NullNoise

NullNoise is an Android voice-recording app built with C and the Android NDK. It lets you record audio, save clips locally, and apply a one-tap noise-reduction and voice-enhancement pass.

> **Prototype:** On one phone, a user reported successful recording and voice
> enhancement, but playback crashed after a few seconds. The full recording,
> enhancement, and playback flow passed on an Android 15 emulator; playback on
> that physical phone remains unresolved. Do not rely on NullNoise for
> important recordings.

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
- Record mono 48 kHz PCM audio and save it in the app's private storage.
- Attach Android `NoiseSuppressor` when the device reports it available.
- Run RNNoise automatically after recording when Android noise suppression
  cannot be used.
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
glue and APK packaging. Audio capture and playback use Android's native audio
APIs; enhancement uses Android `NoiseSuppressor` when available and RNNoise
otherwise. The app targets Android API 35 and compiles native code against API
30.

The custom raylib canvas does not currently expose semantic controls to
TalkBack; full screen-reader support needs native accessibility integration.

If Android noise suppression cannot be attached to the microphone session,
recording is saved with the `_RAW.wav` suffix and RNNoise runs after stop.
Otherwise, recordings use `_NS.wav`. Tapping **NullNoise** creates a paired
`_NN.wav` file. Files remain in the app's internal files directory and are not
exported automatically.

## Privacy

Recordings are intended to stay on-device. Do not add network upload or analytics involving audio without an explicit product decision and clear user disclosure.

## Repository status

NullNoise is a prototype with a single-screen record, playback, and
enhancement flow. The end-to-end flow passes on an Android 15 emulator.
Playback previously crashed on one physical phone and remains unverified on
that device; do not rely on NullNoise for important recordings.

### Releases

Browse the [NullNoise GitHub Releases](https://github.com/snappercayt/nullnoise/releases)
page for published APKs and release notes. New releases appear there
automatically. For non-release builds, see the [Android workflow runs](https://github.com/snappercayt/nullnoise/actions/workflows/android.yml);
their APK artifacts are temporary and are not published releases. APKs are
debug-signed prototypes, not production builds.

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
