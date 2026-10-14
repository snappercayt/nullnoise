#!/usr/bin/env bash
set -euo pipefail

apk=${1:?APK path required}
package=com.snappercayt.nullnoise

adb wait-for-device
adb install -r "$apk"
adb root
adb wait-for-device
adb shell pm revoke "$package" android.permission.RECORD_AUDIO
adb logcat -c
adb shell monkey -p "$package" 1
sleep 3

screen_size=$(
    adb shell wm size |
        sed -n 's/.*: *\([0-9]*\)x\([0-9]*\).*/\1 \2/p' |
        tail -n 1 |
        tr -d '\r'
)
read -r screen_width screen_height <<<"$screen_size"
if [[ ! $screen_width =~ ^[0-9]+$ || ! $screen_height =~ ^[0-9]+$ ]]; then
    echo "Could not determine emulator display size" >&2
    exit 1
fi
tap_button() {
    local index=$1
    local y=$((screen_height * (565 + index * 110) / 1000))
    adb shell input tap "$((screen_width / 2))" "$y"
}

tap_button 0
permission_granted=0
for _ in $(seq 1 30); do
    permission=$(
        adb shell pm check-permission "$package" android.permission.RECORD_AUDIO |
            tr -d '\r'
    )
    if [[ $permission == granted ]]; then
        permission_granted=1
        break
    fi

    adb shell uiautomator dump /sdcard/nullnoise-window.xml >/dev/null 2>&1 || true
    hierarchy=$(adb exec-out cat /sdcard/nullnoise-window.xml 2>/dev/null || true)
    coordinates=$(
        python3 - "$hierarchy" <<'PY'
import re
import sys
import xml.etree.ElementTree as ET

try:
    root = ET.fromstring(sys.argv[1])
except ET.ParseError:
    raise SystemExit(1)

for node in root.iter("node"):
    label = node.attrib.get("text", "").strip().lower()
    if label not in {"while using the app", "only this time", "allow"}:
        continue
    bounds = re.fullmatch(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", node.attrib.get("bounds", ""))
    if bounds:
        left, top, right, bottom = map(int, bounds.groups())
        print((left + right) // 2, (top + bottom) // 2)
        break
else:
    raise SystemExit(1)
PY
    ) || true
    if [[ -n $coordinates ]]; then
        read -r x y <<<"$coordinates"
        adb shell input tap "$x" "$y"
    fi
    sleep 1
done
if [[ $permission_granted -ne 1 ]]; then
    echo "Could not grant microphone permission in emulator" >&2
    adb logcat -d -s NullNoise:E AndroidRuntime:E
    exit 1
fi

assert_app_running() {
    if ! adb shell pidof "$package" >/dev/null; then
        echo "NullNoise process exited during $1" >&2
        adb logcat -d -s NullNoise:E AndroidRuntime:E
        exit 1
    fi
}

# Permission acceptance should return to the app and start the requested recording.
sleep 3
assert_app_running "permission result"
tap_button 0
sleep 2

# A successful recording and enhancement must produce both source and NN files.
files=/data/user/0/$package/files
for _ in $(seq 1 30); do
    source_file=$(adb shell "ls '$files'/*_NS.wav '$files'/*_RAW.wav 2>/dev/null | head -n 1" | tr -d '\r')
    if [[ -n $source_file ]]; then
        break
    fi
    sleep 1
done
if [[ -z ${source_file:-} ]]; then
    echo "No recording was saved after permission was granted" >&2
    adb logcat -d -s NullNoise:E AndroidRuntime:E
    exit 1
fi
source_size=$(adb shell "stat -c %s '$source_file'" | tr -d '\r')
if [[ ! $source_size =~ ^[0-9]+$ || $source_size -le 44 ]]; then
    echo "Recording WAV is empty or malformed" >&2
    exit 1
fi

tap_button 3
enhanced_file=
for _ in $(seq 1 45); do
    enhanced_file=$(adb shell "ls '$files'/*_NN.wav 2>/dev/null | head -n 1" | tr -d '\r')
    if [[ -n $enhanced_file ]]; then
        break
    fi
    assert_app_running "RNNoise enhancement"
    sleep 1
done
if [[ -z $enhanced_file ]]; then
    echo "NullNoise did not create an enhanced WAV" >&2
    adb logcat -d -s NullNoise:E AndroidRuntime:E
    exit 1
fi
enhanced_size=$(adb shell "stat -c %s '$enhanced_file'" | tr -d '\r')
if [[ ! $enhanced_size =~ ^[0-9]+$ || $enhanced_size -le 44 ]]; then
    echo "Enhanced WAV is empty or malformed" >&2
    exit 1
fi

# Play, pause, resume, and let both versions reach end-of-stream.
tap_button 1
sleep 1
assert_app_running "enhanced playback"
tap_button 1
sleep 1
tap_button 1
sleep 4
assert_app_running "enhanced playback completion"
tap_button 2
tap_button 1
sleep 4
assert_app_running "original playback completion"

if adb logcat -d | grep -E 'FATAL EXCEPTION|Fatal signal [0-9]+|Fatal error in native method'; then
    echo "Android crash detected during the playback smoke test" >&2
    exit 1
fi

echo "Microphone permission, recording, enhancement, and original/enhanced playback passed."
