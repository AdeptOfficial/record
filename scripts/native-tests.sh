#!/bin/sh
# Builds and runs the host tests for the looper core: optimised (with allocation
# checks), AddressSanitizer + UBSan, and ThreadSanitizer. Needs cmake and a C++17
# compiler, not the Android SDK. SOAK_SECONDS sets the soak length (default 20).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
for mode in none address thread; do
    echo "== $mode"
    cmake -S "$root/tests/native" -B "$root/build/native-$mode" -DSANITIZE="$mode" > /dev/null
    cmake --build "$root/build/native-$mode" > /dev/null
    "$root/build/native-$mode/looper_core_test" | tail -1
    "$root/build/native-$mode/soak_test" "${SOAK_SECONDS:-20}" 1 | tail -1
done
