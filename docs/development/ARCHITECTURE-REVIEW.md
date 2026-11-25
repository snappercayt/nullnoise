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
callbacks used by the UI. Capture sits behind one `audio_capture` API with two
thin backends selected at compile time: `AudioRecord` + Android
`NoiseSuppressor` (default) and the miniaudio voice-recognition preset
(experimental). RNNoise enhancement runs on a detached worker thread after
recording stops.

## Resolved issues

- The portable core (`src/portable_core.c`) has a host unit, and the
  Android-facing engine is now host-compiled with `-Werror` in both capture
  configurations using declaration-only stubs. This caught and fixed two
  latent `snprintf` truncation bugs and an unused helper.
- The `#if` capture duality in `audio.c` is gone; backend differences are
  confined to `src/audio_capture.c`.
- Playback head position is handled as 64-bit.

## Open issues

### High

- No physical-device audio-quality validation for the current branch. The
  known word-ending cutoff in enhanced clips is still unresolved.

### Medium

- `audio.c` remains one large module of static globals plus inline JNI and
  file I/O for playback/path management. The 60 Hz `audio_tick` loop still
  performs JNI calls and file reads every frame.
- Fixed, untunable magic buffers. `PLAY_BUFFER_SAMPLES` (960 → 3840 bytes,
  ~40 ms stream) and `CAPTURE_BUFFER_FRAMES = 48000 * 4 = 192000` frames
  (~384 KB ring buffer) have no tuning surface for a 1 GB RAM device.

### Low

- Enhanced output is written in place with an opaque second pass. It is
  correct for a seekable file but does not give an atomic "original untouched"
  guarantee; a temp file + `rename()` would.
- 390 px base-width scaling in the UI is a phone-oriented starting point,
  not a statement from the layout system.

## Remaining quick wins

1. **Shrink the native library for a 1 GB RAM device.** Add
   `-ffunction-sections -fdata-sections -Wl,--gc-sections` to `LDFLAGS`
   so unreferenced miniaudio/raylib backends are dropped by the linker.
2. **Privatize buffer sizing.** Make `CAPTURE_BUFFER_FRAMES` and the
   `AudioTrack` stream buffer named, tunable, and device-aware.
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
