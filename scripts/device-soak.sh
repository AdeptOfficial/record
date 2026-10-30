#!/bin/sh
# Runs LooperSoakTest on a connected phone or emulator for MINUTES (default 30) and
# records crash markers and memory from outside the app. Build first:
#   ./gradlew assembleDebug assembleDebugAndroidTest
# Usage: scripts/device-soak.sh [minutes] [seed]
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
adb=${ANDROID_HOME:+$ANDROID_HOME/platform-tools/}adb
minutes=${1:-30}
seed=${2:-1}
out="$root/build/device-soak"
apks="$root/app/build/outputs/apk"
mkdir -p "$out"

"$adb" install -r -g "$apks/debug/app-debug.apk" > "$out/install.log" 2>&1 || exit 1
"$adb" install -r "$apks/androidTest/debug/app-debug-androidTest.apk" >> "$out/install.log" 2>&1 || exit 1
"$adb" shell setprop log.tag.RecordStats DEBUG
"$adb" logcat -c
"$adb" logcat -v time > "$out/logcat.log" 2>&1 &
logcat=$!
(
    while true; do
        echo "$(date +%T) $("$adb" shell dumpsys meminfo dev.adeptproductions.record | grep -E 'TOTAL PSS|TOTAL:' | head -1 | tr -s ' ')"
        sleep 300
    done
) > "$out/meminfo.log" &
sampler=$!

"$adb" shell am instrument -w -e soakMinutes "$minutes" -e soakSeed "$seed" \
    -e class dev.adeptproductions.record.LooperSoakTest \
    dev.adeptproductions.record.test/androidx.test.runner.AndroidJUnitRunner > "$out/instrument.log" 2>&1
status=$?
kill "$sampler" "$logcat" 2>/dev/null

tail -3 "$out/instrument.log"
echo "crash or ANR markers: $(grep -c -E 'FATAL EXCEPTION|ANR in dev.adeptproductions|Fatal signal' "$out/logcat.log")"
grep 'RecordSoak' "$out/logcat.log" | sed 's/^.*RecordSoak[^:]*: //' | tail -3
echo "logs: $out"
exit $status
