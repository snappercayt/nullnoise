# Developer guide

This guide covers local tools, builds, tests, and the CI/release process for
NullNoise. User-facing behavior and project status are described in the
[project README](../../README.md).

## Requirements

For build-independent tests:

- A host C compiler
- Python 3
- Bash

For a local Android APK build:

- GNU Make, Git, `curl`, `sha256sum`, `zip`, `unzip`, and `envsubst`
- Java 17 or newer
- Android SDK Platform 30 and Build Tools 35.0.0
- Android NDK 27.2.12479018 (side-by-side)
- `adb` to install and launch on a connected, USB-debugging-enabled device

Install Android Studio or the Android command-line tools, accept the SDK
licenses, and set `ANDROID_HOME` (or `ANDROID_SDK_ROOT`) to your SDK directory.
For example:

```sh
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --licenses
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" \
  "platforms;android-30" \
  "build-tools;35.0.0" \
  "ndk;27.2.12479018" \
  "platform-tools"
```

The manifest supports Android 6.0 (API 23) and targets API 35; native code is
compiled against API 30. The build uses raylib's Android NativeActivity and
GLES backend, with rawdrawandroid providing glue and APK packaging. The first
build downloads the RNNoise model matching the pinned RNNoise submodule,
verifies its SHA-256, then generates C model data under the ignored `build/`
directory.

## Clone and build

Clone the project and all pinned dependencies:

```sh
git clone --recurse-submodules https://github.com/snappercayt/nullnoise.git
cd nullnoise
make
```

The APK is written to `build/nullnoise.apk`. To install and launch it on a
connected device, run `make run`. The upstream Makefile creates a local debug
keystore on the first build; it is ignored by Git and must not be used for
release signing.

Local builds target `arm64-v8a`; GitHub Actions also builds `x86_64` for
emulator testing. Android SDK/NDK tools are not vendored, and local APK builds
are optional.

## Tests and CI

Run the build-independent checks before using Actions minutes for an Android
build:

```sh
bash scripts/test-local.sh
```

These checks cover smoke-test shell syntax, permission-dialog XML selection,
and the portable WAV-header writer. They do not build an APK or require
Android SDK/NDK tools.

GitHub Actions builds and verifies the APK on pushes to `main` and pull
requests. To run the end-to-end smoke test manually, select **Actions →
Android APK → Run workflow**. The Android 15 emulator test clears app data,
exercises microphone permission, records and validates a WAV, waits for
enhancement, then plays both enhanced and original audio through `AudioTrack`.
A passing emulator run does not replace physical-device testing.

## Publishing a prototype release

In **Actions → Android APK → Run workflow**, enter a new version (for example,
`v0.1.5`) in `release_tag`. Leave `diagnostic_package` disabled for the regular
app. The workflow publishes the pre-release only after the APK build and
emulator smoke test pass. Enable `diagnostic_package` only when you want the
side-by-side diagnostic edition, which has a separate app ID and storage.

Published APKs are debug-signed prototypes, not production release builds.
They are listed on the [GitHub Releases page](https://github.com/snappercayt/nullnoise/releases).
CI runs without a release tag create Actions artifacts only. No separate tag
build runs after a release is created.

Diagnostic builds log playback milestones and, on Android 11 or newer, display
the previous process exit category after a crash when Android makes that
information available. To reproduce a problem in the diagnostic edition,
record a fresh clip there; it does not share recordings with the regular app.
