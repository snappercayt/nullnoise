#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

compiler=${CC:-cc}
if ! command -v "$compiler" >/dev/null 2>&1; then
    echo "C compiler '$compiler' is required for local tests" >&2
    exit 1
fi

bash -n scripts/android-playback-smoke.sh
PYTHONDONTWRITEBYTECODE=1 \
    python3 -m unittest discover -s tests -p 'test_*.py' -v

test_binary=$(mktemp "${TMPDIR:-/tmp}/nullnoise-wav-test.XXXXXX")
queue_test_binary=$(mktemp "${TMPDIR:-/tmp}/nullnoise-audio-queue-test.XXXXXX")
trap 'rm -f "$test_binary" "$queue_test_binary"' EXIT
"$compiler" -std=c11 -Wall -Wextra -Werror -Isrc \
    src/wav.c tests/test_wav.c -o "$test_binary"
"$test_binary"

"$compiler" -std=c11 -Wall -Wextra -Werror \
    -Ithird_party/raylib/src/external tests/test_audio_capture_queue.c \
    -pthread -ldl -lm -o "$queue_test_binary"
"$queue_test_binary"

# Portable audio core host test: WAV + RNNoise normalization (src/portable_core.c).
portable_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/nullnoise-portable-test.XXXXXX")
trap 'rm -rf "$portable_test_dir"' EXIT

model_hash=$(cat third_party/rnnoise/model_version)
model_c="$portable_test_dir/rnnoise_data.c"
if [ ! -f "$model_c" ]; then
    curl --fail --location --silent --show-error --max-time 120 \
        "https://media.xiph.org/rnnoise/models/rnnoise_data-${model_hash}.tar.gz" \
        -o "$portable_test_dir/model.tar.gz"
    tar -xzf "$portable_test_dir/model.tar.gz" -C "$portable_test_dir" \
        src/rnnoise_data.c src/rnnoise_data.h
    cp "$portable_test_dir/src/rnnoise_data.c" "$model_c"
fi
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$portable_test_dir/model.tar.gz" | awk '{print $1}')
    if [ "$actual" != "$model_hash" ]; then
        echo "model archive checksum mismatch (got $actual, want $model_hash)" >&2
        exit 1
    fi
fi

"$compiler" -std=c11 -Wall -Wextra -Werror \
    -Isrc -Ithird_party/rnnoise/include -Ithird_party/rnnoise/src \
    -I"$portable_test_dir/src" \
    -Wno-error=cpp \
    tests/test_portable_core.c \
    src/wav.c \
    src/portable_core.c \
    third_party/rnnoise/src/denoise.c \
    third_party/rnnoise/src/rnn.c \
    third_party/rnnoise/src/pitch.c \
    third_party/rnnoise/src/kiss_fft.c \
    third_party/rnnoise/src/celt_lpc.c \
    third_party/rnnoise/src/nnet.c \
    third_party/rnnoise/src/nnet_default.c \
    third_party/rnnoise/src/parse_lpcnet_weights.c \
    third_party/rnnoise/src/rnnoise_tables.c \
    "$model_c" \
    -pthread -ldl -lm -o "$portable_test_dir/portable_test"
"$portable_test_dir/portable_test"

git diff --check HEAD
echo "Local build-independent checks passed."
