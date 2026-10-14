# NullNoise

NullNoise is an Android voice-recording app built with C and the Android NDK. It lets you record audio, save clips locally, and apply a one-tap noise-reduction and voice-enhancement pass.

## Project goals

- Make voice recording quick and reliable.
- Keep recordings on the device by default.
- Make speech clearer with a simple, one-tap processing workflow.
- Keep the audio-processing core in portable C where practical.

## Planned user workflow

1. Start and stop a recording.
2. Save the original clip to local app storage.
3. Preview the original or enhanced audio.
4. Tap **Enhance** to create a noise-reduced, voice-focused version.
5. Keep, rename, or delete either version.

The original recording should remain unchanged when an enhanced copy is created.

## Initial scope

### MVP

- Android app shell and microphone permission flow.
- Record, stop, playback, and locally save audio clips.
- A simple recordings list with rename and delete actions.
- One-tap enhancement that writes a separate output file.
- Clear processing, permission, and storage error states.

### Later exploration

- Compare original and enhanced clips.
- Tune enhancement strength and voice profile options.
- Export or share a selected clip.
- Evaluate additional denoising and speech-enhancement algorithms.

## Technical direction

- **Platform:** Android
- **App framework:** [rawdrawandroid](https://github.com/cnlohr/rawdrawandroid), included as a pinned recursive Git submodule.
- **Languages:** C for the native Android activity, app shell, and audio engine.
- **Build:** rawdrawandroid's GNU Make-based Android NDK build, with NullNoise-specific app/package names and manifest.
- **Audio pipeline:** Capture audio, persist the unprocessed recording, process a copy, then save the enhanced result separately.

The recording format and audio-processing algorithm remain open implementation decisions. Choose them after validating device/API requirements and audio quality rather than treating this outline as a fixed design.

## Initial Android setup

The starter app uses rawdrawandroid's NativeActivity and GLES rendering path. At startup it checks Android's `NoiseSuppressor.isAvailable()` and displays the result. This only checks whether the device exposes an NS effect; attaching it to an `AudioRecord`, recording `_NS.wav`, and the RNNoise `_NN.wav` fallback are not implemented yet.
The manifest currently supports Android 5.1 (API 22) and targets API 35; native code is compiled against API 30.

### Requirements

- GNU Make, Git, `zip`, `unzip`, and `envsubst`
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

The initial build targets `arm64-v8a`. The Android NDK/SDK are not vendored; build locally after installing the requirements above.

GitHub Actions builds the APK on pushes to `main` and pull requests. To publish the CI-built APK as a GitHub Release, push a version tag such as `v0.1.0`. The attached APK is debug-signed for testing; it is not a production release build.

## Development roadmap

1. **Foundation:** choose the minimum Android version, UI approach, audio format, and build setup; create a runnable app shell.
2. **Recording:** implement microphone permission handling, capture, playback, and local persistence.
3. **Enhancement:** add a C processing interface and evaluate a denoising/voice-enhancement implementation against sample recordings.
4. **Integration:** connect the one-tap action to processing, preserve the original, and handle cancellation and failures.
5. **Quality:** test on physical devices, check audio quality and latency, and document supported formats and limitations.

## Privacy

Recordings are intended to stay on-device. Do not add network upload or analytics involving audio without an explicit product decision and clear user disclosure.

## Repository status

The repository contains an initial rawdrawandroid-based NativeActivity shell and Android build setup. Audio recording and noise reduction/voice enhancement remain future work.

## License

NullNoise is licensed under the [MIT License](LICENSE).

## Inspiration and credits

NullNoise is inspired by and will use [rawdrawandroid](https://github.com/cnlohr/rawdrawandroid), created by [CNLohr](https://github.com/cnlohr), as the foundation for its C-based Android app. rawdrawandroid is MIT-licensed. Any upstream code incorporated into NullNoise will retain its required copyright and license notices.
