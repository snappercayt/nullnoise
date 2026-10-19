# Architecture review — NullNoise

Input for this review is the C/NativeActivity source, the Makefile, the CI
workflow, and the developer guide (`docs/development/README.md`). The host
build-independent suite (`bash scripts/test-local.sh`) was run and passes
(WAV header tests, miniaudio PCM-ring tests, permission-dialog XML tests,
shell syntax, `git diff --check`).

## Architecture summary

A single-screen Android `NativeActivity` (raylib + OpenGL ES2) whose `main.c`
drives a 60 Hz polling loop and whose `audio.c` is the audio engine. It holds
static globals (state, paths, JNI refs, buffers) and presents `audio.h`
callbacks used by the UI. There are two parallel capture implementations
selected by the compile-time flag `NULLNOISE_MINIAUDIO_CAPTURE`
(`AudioRecord` + Android `NoiseSuppressor` in the default build, miniaudio
voice-recognition preset in the experimental build). RNNoise enhancement runs
on a detached worker thread after recording stops.

## Issues

### High

- **The core audio engine is never host-compiled.**
  `scripts/test-local.sh` compiles only `src/wav.c` and
  `tests/test_audio_capture_queue.c`, both portable. `src/audio.c` and
  `src/main.c` require the Android NDK (`jni.h`, `android_native_app_glue.h`)
  and have no host compile or unit coverage. The most logic-dense file in the
  project drives a real microphone, a WAV writer, and playback, and has zero
  host validation. This is the largest actual risk in the repo.

### Medium

- **`#if`-driven two-implementation split is the main maintenance cost.**
  `audio_start_recording`, `audio_tick`, `audio_stop_recording`, and
  `audio_get_recording_samples` each branch on `NULLNOISE_MINIAUDIO_CAPTURE`.
  Two parallel capture and bookkeeping flows must stay in sync.
- **`audio.c` is one large module of static globals plus inline JNI and
  file I/O.** The 60 Hz `audio_tick` loop performs JNI calls and file reads
  every frame, entangling the timing path with platform calls.
- **Fixed, untunable magic buffers.** `PLAY_BUFFER_SAMPLES` (960 → 3840
  bytes, ~40 ms stream) and `CAPTURE_BUFFER_FRAMES = 48000 * 4 = 192000`
  frames (~384 KB ring buffer) have no tuning surface for a 1 GB RAM device.

### Low

- **Latent 32-bit truncation of the playback head.** `audio_tick:600,660`
  cast the `AudioTrack` head to `uint32_t` into 64-bit variables. Unreachable
  for voice-length clips, but a real trap if the file grows.
- **Enhanced output is written in place with an opaque second pass.**
  `process_recording:1007-1147` rewrites samples into the same file with
  backward seeks. Correct for a seekable file, but fragile and it does not
  give the "original never touched" guarantee in an atomic form.
- **390 px base-width scaling in the UI.** `main.c:577` scales from a 390 px
  layout target. This works for landscape TV output but is a phone-oriented
  starting point, not a statement from the layout system.

## Quick wins

1. **Host-test the audio engine (blocking).** Move WAV + RNNoise-normalize
   logic into a portable core with a host unit folded into
   `scripts/test-local.sh` (`-Werror`). This must precede any behavior
   change.
2. **Shrink the native library for a 1 GB RAM device.** Add
   `-ffunction-sections -fdata-sections -Wl,--gc-sections` to `LDFLAGS`
   (`Makefile:45`) so unreferenced miniaudio/raylib backends are dropped by
   the linker.
3. **Replace the `#if` capture duality with one API and two thin backends.**
   This is the structural payoff of #1 and makes #2 testable.
4. **Privatize buffer sizing.** Make `CAPTURE_BUFFER_FRAMES` and the
   `AudioTrack` stream buffer named, tunable, and device-aware.
5. **Temp file + rename for enhanced output.** Switch the two-pass in-place
   rewrite to a temp file + `rename()` so the source clip is guaranteed
   untouched and a partial `_NN.wav` can never be surfaced as complete.
6. **Fix the head-position cast.** Cast once to `int64_t`; compare 64-bit.
7. **Non-blocking validation.** Confirm the UI scaling target (390 px) is a
   deliberate choice; consider a responsive layout instead of hard-coded
   width math.

## Roadmap for improvement

See the README for the full discussion. Order of impact:

1. Host-test the audio engine (blocking).
2. Replace the `#if` capture duality with a single API and two backends.
3. Add linker section flags to shrink the native library on a 1 GB RAM
   device.
4. Tune capture and playback buffers with named, device-aware constants.
5. Convert enhanced output to temp file + rename.
6. Fix the 64-bit head-position handling.
7. Revisit the 390 px UI scaling target.

Related: `docs/development/README.md` already documents the experimental
miniaudio capture path and its known limitation (voice-recognition preset is
not equivalent to an explicit Android `NoiseSuppressor`); that remains
experimental until the `#if` duality is resolved and the capture path is
host-validated.
