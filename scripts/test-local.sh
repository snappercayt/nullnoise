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

git diff --check HEAD
echo "Local build-independent checks passed."
