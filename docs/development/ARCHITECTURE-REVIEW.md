# Architecture review — NullNoise

Input for this review is the C/NativeActivity source, the Makefile, the CI
workflow, and the developer guide (`docs/development/README.md`). The current
release is v0.1.7; its release run passed both the APK build and the Android 15
emulator smoke test.

The host suite (`bash scripts/test-local.sh`) passes. It covers WAV header
writing, miniaudio PCM-ring wraparound/full/empty behavior, permission-dialog
XML selection, the portable RNNoise core, shell syntax, `git diff --check`,
and `-Werror` host compilation of `audio.c` plus both capture backends.

## Architecture summary

A single-screen Android `NativeActivity` (raylib + OpenGL ES2) whose `main.c`
drives a 60 Hz polling loop and whose `audio.c` is the audio engine. It holds
static globals (state, paths, JNI refs, buffers) and presents `audio.h`
callbacks used by the UI. Capture sits behind one `audio_capture` API with two
thin backends selected at compile time: `AudioRecord` + Android
`NoiseSuppressor` (default, used by v0.1.7) and the miniaudio
voice-recognition preset (experimental). RNNoise enhancement runs on a
detached worker thread after recording stops.

## Resolved issues

- The portable core (`src/portable_core.c`) has a host unit, and the
  Android-facing engine is host-compiled with `-Werror` in both capture
  configurations using declaration-only stubs. This caught and fixed two
  latent `snprintf` truncation bugs and an unused helper.
- The `#if` capture duality in `audio.c` is gone; backend differences are
  confined to `src/audio_capture.c`, and only `src/miniaudio_impl.c` is
  conditional in the Makefile.
- Playback head position is compared as 64-bit instead of being truncated to
  `uint32_t`.
- CI uses published action versions, runs on the stable `ubuntu-24.04` image,
  restores the emulator-smoke release gate, and the smoke harness now measures
  capture from actual WAV creation, retries a missed Record tap, and avoids a
  `pipefail`/`grep -q` SIGPIPE that hid successful file detection.

## Open issues

### High

- No automated physical-device audio-quality check exists. Early device
  feedback on v0.1.7 reports much better recording and playback reliability,
  but the known word-ending cutoff in enhanced clips remains unresolved.

### Medium

- `audio.c` is still one large module of static globals plus inline JNI and
  file I/O for playback and path management. The 60 Hz `audio_tick` performs
  JNI calls and file reads every frame; capture reads now live in the backend
  but are still driven from that tick.
- Fixed, untunable magic buffers. `PLAY_BUFFER_SAMPLES` and
  `RECORD_BUFFER_SAMPLES` are both 960 samples (3840 bytes, ~40 ms), while
  `CAPTURE_BUFFER_FRAMES = 48000 * 4 = 192000` frames (~384 KB ring buffer) is
  only used on the miniaudio path. None has a tuning surface for a 1 GB RAM
  device.

### Low

- Enhanced output is written in place with an opaque second pass
  (`src/portable_core.c`). It is correct for a seekable file and refuses to
  overwrite an existing output, but a crash mid-write can leave a partial
  `_NN.wav`; a temp file + `rename()` would make publication atomic.
- `draw_screen` scales from a 390 px base width (`src/main.c:558`). This is a
  phone-oriented starting point, not a statement from the layout system.

## Remaining quick wins

1. **Shrink the native library for a 1 GB RAM device.** Add
   `-ffunction-sections -fdata-sections -Wl,--gc-sections` to `LDFLAGS`
   so unreferenced miniaudio/raylib backends are dropped by the linker.
2. **Privatize buffer sizing.** Make the capture and playback buffers named,
   tunable, and device-aware.
3. **Temp file + rename for enhanced output.** Switch the two-pass in-place
   rewrite to a temp file + `rename()` so a partial `_NN.wav` can never be
   surfaced as complete.
4. **Non-blocking validation.** Confirm the UI scaling target (390 px) is a
   deliberate choice; consider a responsive layout instead of hard-coded
   width math.

## Roadmap for improvement

See the README for the full discussion. Remaining work, in impact order:

1. Add linker section flags to shrink the native library on a 1 GB RAM device.
2. Tune capture and playback buffers with named, device-aware constants.
3. Convert enhanced output to temp file + rename.
4. Revisit the 390 px UI scaling target.

Related: `docs/development/README.md` documents the experimental miniaudio
capture path. Its voice-recognition preset is not equivalent to an explicit
Android `NoiseSuppressor`, and it still needs physical-device audio validation
before it stops being experimental.
