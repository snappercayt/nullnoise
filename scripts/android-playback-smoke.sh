#!/usr/bin/env bash
set -euo pipefail

apk=${1:?APK path required}
package=${2:-com.snappercayt.nullnoise}
if [[ ! $package =~ ^com(\.[A-Za-z0-9_]+)+$ ]]; then
    echo "Invalid Android package name: $package" >&2
    exit 2
fi

show_diagnostics() {
    echo "Recent Android crash-buffer entries:" >&2
    adb logcat -d -b crash -v threadtime >&2 || true
    echo "Recent NullNoise and native-runtime entries:" >&2
    adb logcat -d -v threadtime -s NullNoise:V AndroidRuntime:E libc:FDEBUG:E >&2 ||
        true
}

fail_test() {
    echo "$1" >&2
    show_diagnostics
    exit 1
}

trap 'echo "Smoke test failed at line $LINENO: $BASH_COMMAND" >&2; show_diagnostics' ERR

adb wait-for-device
install_attempt=1
until adb install -r "$apk"; do
    if [[ $install_attempt -ge 3 ]]; then
        fail_test "Could not install APK after $install_attempt attempts"
    fi
    echo "APK install failed; retrying after Android package service settles" >&2
    adb wait-for-device
    sleep 10
    install_attempt=$((install_attempt + 1))
done
adb root
adb wait-for-device

# Start from a clean app state so the test cannot reuse an earlier recording
# or an already-granted microphone permission.
clear_result=$(adb shell pm clear "$package" | tr -d '\r')
if [[ $clear_result != "Success" ]]; then
    fail_test "Could not clear app data for $package: $clear_result"
fi
adb logcat -c
adb shell monkey -p "$package" 1 >/dev/null

app_started=0
for _ in $(seq 1 30); do
    if adb shell pidof "$package" >/dev/null; then
        app_started=1
        break
    fi
    sleep 1
done
if [[ $app_started -ne 1 ]]; then
    fail_test "NullNoise did not start"
fi

screen_size=$(
    adb shell wm size |
        sed -n 's/.*: *\([0-9]*\)x\([0-9]*\).*/\1 \2/p' |
        tail -n 1 |
        tr -d '\r'
)
read -r screen_width screen_height <<<"$screen_size"
if [[ ! $screen_width =~ ^[0-9]+$ || ! $screen_height =~ ^[0-9]+$ ]]; then
    fail_test "Could not determine emulator display size"
fi
tap_button() {
    local index=$1
    local y=$((screen_height * (565 + index * 110) / 1000))
    adb shell input tap "$((screen_width / 2))" "$y"
}

assert_app_running() {
    if ! adb shell pidof "$package" >/dev/null; then
        fail_test "NullNoise process exited during $1"
    fi
}

tap_button 0
permission_button_tapped=0
for _ in $(seq 1 45); do
    adb shell uiautomator dump /sdcard/nullnoise-window.xml >/dev/null 2>&1 || true
    hierarchy=$(adb exec-out cat /sdcard/nullnoise-window.xml 2>/dev/null || true)
    coordinates=$(python3 scripts/permission_dialog.py <<<"$hierarchy") || true
    if [[ -n $coordinates ]]; then
        read -r x y <<<"$coordinates"
        adb shell input tap "$x" "$y"
        permission_button_tapped=1
        break
    fi
    sleep 1
done
if [[ $permission_button_tapped -ne 1 ]]; then
    fail_test "Could not find an allowed microphone permission choice"
fi

# Granting permission should return to the app and start the requested capture.
sleep 3
assert_app_running "permission result and recording"
tap_button 0

files=/data/user/0/$package/files
source_file=
for _ in $(seq 1 30); do
    source_file=$(
        adb shell "ls '$files'/*_NS.wav '$files'/*_RAW.wav 2>/dev/null | head -n 1" |
            tr -d '\r'
    )
    if [[ -n $source_file ]]; then
        break
    fi
    assert_app_running "recording save"
    sleep 1
done
if [[ -z $source_file ]]; then
    fail_test "Recording was not saved after capture stopped"
fi

validate_wav() {
    local remote_file=$1
    local minimum_samples=$2
    local expected_samples=${3:-0}
    local local_file samples

    local_file=$(mktemp)
    if ! adb pull "$remote_file" "$local_file" >/dev/null; then
        rm -f "$local_file"
        return 1
    fi
    if samples=$(python3 - "$local_file" "$minimum_samples" "$expected_samples" <<'PY'
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
minimum_samples = int(sys.argv[2])
expected_samples = int(sys.argv[3])
data = path.read_bytes()
if len(data) < 44 or data[:4] != b"RIFF" or data[8:16] != b"WAVEfmt ":
    raise SystemExit(f"{path}: missing or truncated PCM WAV header")
riff_size = struct.unpack_from("<I", data, 4)[0]
format_tag, channels, sample_rate, byte_rate, block_align, bits = (
    struct.unpack_from("<HHIIHH", data, 20)
)
data_size = struct.unpack_from("<I", data, 40)[0]
if riff_size + 8 != len(data) or data_size + 44 != len(data):
    raise SystemExit(f"{path}: WAV header sizes do not match the saved file")
if (format_tag, channels, sample_rate, byte_rate, block_align, bits) != (
    1,
    1,
    48000,
    96000,
    2,
    16,
):
    raise SystemExit(f"{path}: unexpected audio format")
samples = data_size // block_align
if data_size % block_align or samples < minimum_samples:
    raise SystemExit(f"{path}: WAV does not contain enough complete audio samples")
if expected_samples and samples != expected_samples:
    raise SystemExit(f"{path}: sample count differs from the source recording")
print(samples)
PY
    ); then
        rm -f "$local_file"
        printf '%s\n' "$samples"
    else
        rm -f "$local_file"
        return 1
    fi
}

source_samples=$(validate_wav "$source_file" 48000) ||
    fail_test "Saved recording is not a finalized WAV with at least one second of audio"
echo "Validated saved recording: $source_file ($source_samples samples)"

# RNNoise may start automatically after recording. If Android noise suppression
# was used, start enhancement explicitly; otherwise give automatic processing
# a few seconds to begin before requesting it.
enhanced_file=
for _ in $(seq 1 5); do
    enhanced_file=$(
        adb shell "ls '$files'/*_NN.wav 2>/dev/null | head -n 1" |
            tr -d '\r'
    )
    [[ -n $enhanced_file ]] && break
    sleep 1
done
if [[ -z $enhanced_file ]]; then
    tap_button 3
fi

enhanced_samples=
last_size=0
stable_seconds=0
for _ in $(seq 1 90); do
    enhanced_file=$(
        adb shell "ls '$files'/*_NN.wav 2>/dev/null | head -n 1" |
            tr -d '\r'
    )
    if [[ -n $enhanced_file ]]; then
        current_size=$(
            adb shell "stat -c %s '$enhanced_file' 2>/dev/null" |
                tr -d '\r'
        )
        if [[ $current_size =~ ^[0-9]+$ && $current_size -gt 44 ]]; then
            if [[ $current_size == "$last_size" ]]; then
                stable_seconds=$((stable_seconds + 1))
            else
                stable_seconds=0
            fi
            last_size=$current_size
            if [[ $stable_seconds -ge 2 ]] &&
                enhanced_samples=$(validate_wav "$enhanced_file" 48000 "$source_samples" 2>/dev/null); then
                break
            fi
        fi
    fi
    assert_app_running "audio enhancement"
    sleep 1
done
if [[ -z $enhanced_samples ]]; then
    fail_test "Enhanced WAV was not fully written and validated"
fi
echo "Validated enhanced recording: $enhanced_file ($enhanced_samples samples)"

# Play the enhanced clip, exercise pause/resume, and let it finish through
# AudioTrack. Then switch versions and play the saved original recording.
tap_button 1
sleep 1
assert_app_running "enhanced playback start"
tap_button 1
sleep 1
tap_button 1

wait_for_playback_completion() {
    local completed_count=$1
    for _ in $(seq 1 45); do
        assert_app_running "playback completion"
        current_count=$(
            adb logcat -d -s NullNoise:I |
                grep -Fc "Playback reached end of stream" || true
        )
        if [[ $current_count -ge $completed_count ]]; then
            return 0
        fi
        sleep 1
    done
    fail_test "Playback did not reach end of stream ($completed_count expected)"
}

wait_for_playback_completion 1
tap_button 2
tap_button 1
wait_for_playback_completion 2

playback_log=$(adb logcat -d -s NullNoise:I)
write_count=$(grep -Fc "First AudioTrack write succeeded" <<<"$playback_log" || true)
if [[ $write_count -lt 2 ]]; then
    fail_test "Both saved WAV files did not reach AudioTrack.write"
fi
if adb logcat -d -b crash -v threadtime | grep -E 'Fatal signal [0-9]+|Fatal error in native method'; then
    fail_test "Native crash detected during the playback smoke test"
fi

echo "Microphone permission, recording, WAV finalization, enhancement, and original/enhanced native playback passed."
