# NullNoise

NullNoise is an Android voice-recording app built with C and the Android NDK. It lets you record audio, save clips locally, and apply a one-tap noise-reduction and voice-enhancement pass.

> **Prototype:** The APK builds in GitHub Actions, but recording, playback, and
> noise suppression are not yet validated on a device. Do not rely on it for
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

## Initial scope

### Implemented prototype features (device validation pending)

- Request microphone access when recording is first started.
- Record mono 48 kHz PCM audio and save it in the app's private storage.
- Attach Android `NoiseSuppressor` when the device reports it available.
- Run RNNoise automatically after recording when Android noise suppression
  cannot be used.
- Run RNNoise on demand with **NullNoise**, producing a separate normalized
  `_NN.wav` file while preserving the source clip.
- Play, pause, and switch between the original and enhanced versions.

### Not yet included

- A recordings browser, rename/delete actions, export/share, and quality
  controls.
- Physical-device validation across Android vendors and microphone routes.

## Technical direction

- **Platform:** Android
- **App framework:** [rawdrawandroid](https://github.com/cnlohr/rawdrawandroid), included as a pinned recursive Git submodule.
- **Languages:** C for the native Android activity, app shell, and audio engine.
- **Build:** rawdrawandroid's GNU Make-based Android NDK build, with NullNoise-specific app/package names and manifest.
- **Audio pipeline:** Capture mono 48 kHz PCM, use Android `NoiseSuppressor`
  when available, and retain a separate RNNoise-enhanced output.

If Android noise suppression cannot be attached to the microphone session,
recording is saved with the `_RAW.wav` suffix and RNNoise runs after stop.
Otherwise, recordings use `_NS.wav`. Tapping **NullNoise** creates a paired
`_NN.wav` file. Files remain in the app's internal files directory and are not
exported automatically.

## Initial Android setup

The app uses rawdrawandroid's NativeActivity and GLES rendering path. Runtime
microphone permission is requested before recording. The manifest supports
Android 6.0 (API 23) and targets API 35; native code is compiled against API 30.
The first build downloads the RNNoise model published for the pinned RNNoise
submodule and verifies it against RNNoise's `model_version` SHA-256 before
extracting the generated C model into the ignored `build/` directory.

### Requirements

- GNU Make, Git, `curl`, `sha256sum`, `zip`, `unzip`, and `envsubst`
- Java 17 or newer
- Android SDK Platform 30 and Build Tools 35.0.0
- Android NDK (side-by-side)
- `adb` to install and launch on a connected, USB-debugging-enabled device

Install Android Studio or the Android command-line tools, accept the SDK licenses, and set `ANDROID_HOME` (or `ANDROID_SDK_ROOT`) to your SDK directory. For example, with command-line tools installed:

```sh
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --licenses
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" \
  "platforms;android-30" \
  "build-tools;35.0.0" \
  "ndk;27.2.12479018" \
  "platform-tools"
```

Clone the project with its framework and nested dependencies, then build:

```sh
git clone --recurse-submodules https://github.com/snappercayt/nullnoise.git
cd nullnoise
make
```

The APK is written to `build/nullnoise.apk`. To install and launch it on a connected device, run `make run`. The upstream Makefile creates a local debug keystore on the first build; this key is ignored by Git and must not be used for release signing.

The initial build targets `arm64-v8a`. The Android NDK/SDK are not vendored;
build locally after installing the requirements above.

GitHub Actions builds the APK on pushes to `main` and pull requests. To publish the CI-built APK as a GitHub Release, push a version tag such as `v0.1.0`. The attached APK is debug-signed for testing; it is not a production release build.

## Development roadmap

1. **Device validation:** test capture, playback, NoiseSuppressor availability,
   and RNNoise output on physical Android devices.
2. **Library UX:** add a recordings list and safe rename/delete/export actions.
3. **Audio quality:** evaluate output quality and latency; expose controls only
   after listening tests.

## Privacy

Recordings are intended to stay on-device. Do not add network upload or analytics involving audio without an explicit product decision and clear user disclosure.

## Repository status

The current MVP has a functional single-screen record, playback, and
enhancement flow; broader device testing and library management remain future
work.

## License

NullNoise is licensed under the [MIT License](LICENSE).

## Inspiration and credits

NullNoise is inspired by and will use [rawdrawandroid](https://github.com/cnlohr/rawdrawandroid), created by [CNLohr](https://github.com/cnlohr), as the foundation for its C-based Android app. rawdrawandroid is MIT-licensed. Any upstream code incorporated into NullNoise will retain its required copyright and license notices.

Noise reduction uses [RNNoise](https://github.com/xiph/rnnoise), which is
distributed under the BSD 3-Clause License. Its license and copyright notices
are included in the RNNoise submodule at `third_party/rnnoise/COPYING`.
