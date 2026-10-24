#!/usr/bin/env bash
# One-shot check: runs every test suite of the repo, each section timed and wrapped in `timeout`, stopping at
# the first failure and printing a PASS/FAIL table at the end. Runs from any cwd (the repo root is resolved
# from this script's location).
#
#   scripts/check.sh            engine Debug build + ctest (section 1) only
#   scripts/check.sh --ubsan    ... plus the engine under UndefinedBehaviorSanitizer
#   scripts/check.sh --asan     ... plus the engine under AddressSanitizer
#   scripts/check.sh --tsan     ... plus the engine under ThreadSanitizer
#   scripts/check.sh --swift    ... plus `swift test` in ChordLab/ (when ChordLab/Package.swift exists)
#   scripts/check.sh --bridge   ... plus the Obj-C++ bridge tests (when ChordLab/bridge/tests/CMakeLists.txt exists)
#   scripts/check.sh --plugin   ... plus both Xcode schemes (macOS-VST3, macOS-APP), no signing
#   scripts/check.sh --all      everything
# Flags combine; section 1 always runs first.
#
# Sanitizers: UBSan, ASan and TSan each get their own build directory. On Apple clang 17 (Xcode 16) ASan
# deadlocked before main() and TSan hung, which is why every sanitizer step has a hard timeout; since
# Apple clang 21 (Xcode 27.1, 2026-10-24) all three run the full suite cleanly. `--all` includes them.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$ROOT" || exit 2

DEFAULT_TIMEOUT=300
UBSAN_TIMEOUT=600
PLUGIN_TIMEOUT=600   # a cold xcodebuild of the plug-in exceeds the default

want_ubsan=0 want_asan=0 want_tsan=0 want_swift=0 want_bridge=0 want_plugin=0
for arg in "$@"; do
    case "$arg" in
        --ubsan) want_ubsan=1 ;;
        --asan) want_asan=1 ;;
        --tsan) want_tsan=1 ;;
        --swift) want_swift=1 ;;
        --bridge) want_bridge=1 ;;
        --plugin) want_plugin=1 ;;
        --all) want_ubsan=1 want_asan=1 want_tsan=1 want_swift=1 want_bridge=1 want_plugin=1 ;;
        -h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "check.sh: unknown option '$arg' (try --help)" >&2; exit 2 ;;
    esac
done

if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD=timeout
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD=gtimeout
else
    echo "check.sh: neither timeout nor gtimeout found; install coreutils" >&2
    exit 2
fi

LOG_DIR="${TMPDIR:-/tmp}"
LOG_DIR="${LOG_DIR%/}"

SUMMARY=()     # "name|status|seconds"
FAILED=0

record() {  # name status seconds
    SUMMARY+=("$1|$2|$3")
}

# step <seconds> <command...>: one command under timeout; prints it first. Returns its status.
step() {
    local limit="$1"
    shift
    echo "+ $*"
    "$TIMEOUT_CMD" "$limit" "$@"
    local rc=$?
    if [ "$rc" -eq 124 ]; then
        echo "check.sh: timed out after ${limit}s: $*" >&2
    fi
    return "$rc"
}

# section <name> <function>: times the function, records PASS/FAIL, remembers the first failure.
section() {
    local name="$1" fn="$2"
    echo
    echo "=== $name ==="
    local start=$SECONDS
    "$fn"
    local rc=$?
    local took=$((SECONDS - start))
    if [ "$rc" -eq 0 ]; then
        record "$name" PASS "$took"
    else
        record "$name" FAIL "$took"
        FAILED=1
    fi
}

skip() {  # name reason
    echo
    echo "=== $1 === skipped: $2"
    record "$1" "SKIP ($2)" 0
}

# ---------------------------------------------------------------------------------------------------------

engine_debug() {
    # An explicit build type overrides whatever a cached configure left in CMakeCache.txt, so this section is
    # always unoptimised with asserts on, whatever was configured in engine/build before.
    step "$DEFAULT_TIMEOUT" cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Debug || return 1
    step "$DEFAULT_TIMEOUT" cmake --build engine/build --config Debug -j8 || return 1
    (cd engine/build && step "$DEFAULT_TIMEOUT" ctest -C Debug --output-on-failure) || return 1
}

engine_ubsan() {
    step "$UBSAN_TIMEOUT" cmake -S engine -B engine/build-ubsan -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-sanitize-recover=all" \
        -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=undefined || return 1
    step "$UBSAN_TIMEOUT" cmake --build engine/build-ubsan --config Debug -j8 || return 1
    (cd engine/build-ubsan && step "$UBSAN_TIMEOUT" ctest -C Debug --output-on-failure) || return 1
}

# One sanitizer build: $1 = directory suffix, $2 = -fsanitize value, $3 = extra compile flags.
engine_sanitized() {
    local dir="engine/build-$1"
    step "$UBSAN_TIMEOUT" cmake -S engine -B "$dir" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="-fsanitize=$2 $3" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=$2" || return 1
    step "$UBSAN_TIMEOUT" cmake --build "$dir" --config Debug -j8 || return 1
    (cd "$dir" && step "$UBSAN_TIMEOUT" ctest -C Debug --output-on-failure) || return 1
}
engine_asan() { engine_sanitized asan address "-fno-omit-frame-pointer"; }
engine_tsan() { engine_sanitized tsan thread ""; }

swift_tests() {
    (cd ChordLab && step "$DEFAULT_TIMEOUT" swift test) || return 1
}

bridge_tests() {
    step "$DEFAULT_TIMEOUT" cmake -S ChordLab/bridge/tests -B ChordLab/build-bridge-tests || return 1
    step "$DEFAULT_TIMEOUT" cmake --build ChordLab/build-bridge-tests -j8 || return 1
    (cd ChordLab/build-bridge-tests && step "$DEFAULT_TIMEOUT" ctest --output-on-failure) || return 1
}

# One Xcode scheme, built unsigned. Fails when "BUILD SUCCEEDED" is missing from the log, or when the log
# holds any error or warning that points into ChordLab/ or engine/ (third-party and SDK noise is ignored).
# The Swift driver's "Could not read priors" warning names a .priors file under ChordLab/build-mac/; it only
# means the incremental-build cache was stale (first build after an Xcode update), so it is ignored too.
plugin_scheme() {
    local scheme="$1"
    local log="$LOG_DIR/chordlab-xcodebuild-$scheme.log"
    echo "+ xcodebuild -scheme $scheme (log: $log)"
    "$TIMEOUT_CMD" "$PLUGIN_TIMEOUT" xcodebuild -project ChordLab/projects/ChordLab-macOS.xcodeproj \
        -scheme "$scheme" -configuration Debug \
        CODE_SIGN_STYLE=Manual CODE_SIGN_IDENTITY=- DEVELOPMENT_TEAM= PROVISIONING_PROFILE_SPECIFIER= \
        CODE_SIGN_ENTITLEMENTS= build >"$log" 2>&1
    local rc=$?
    if [ "$rc" -eq 124 ]; then
        echo "check.sh: xcodebuild $scheme timed out after ${PLUGIN_TIMEOUT}s (see $log)" >&2
        return 1
    fi
    if [ "$rc" -ne 0 ]; then
        echo "check.sh: xcodebuild $scheme exited with status $rc (see $log); tail:" >&2
        tail -n 25 "$log" >&2
        return 1
    fi
    if ! grep -q "BUILD SUCCEEDED" "$log"; then
        echo "check.sh: BUILD SUCCEEDED missing for scheme $scheme; tail of $log:" >&2
        tail -n 25 "$log" >&2
        return 1
    fi
    local noise
    noise="$(grep -E "error:|warning:" "$log" | grep -E "ChordLab/|engine/" | grep -v "Could not read priors")"
    if [ -n "$noise" ]; then
        echo "check.sh: diagnostics in project sources for scheme $scheme (see $log):" >&2
        echo "$noise" >&2
        return 1
    fi
    echo "scheme $scheme: BUILD SUCCEEDED, no diagnostics in ChordLab/ or engine/"
}

plugin_builds() {
    if [ ! -d ChordLab/projects/ChordLab-macOS.xcodeproj ]; then
        echo "check.sh: ChordLab/projects/ChordLab-macOS.xcodeproj not found" >&2
        return 1
    fi
    plugin_scheme macOS-VST3 || return 1
    plugin_scheme macOS-APP || return 1
}

# ---------------------------------------------------------------------------------------------------------

print_summary() {
    echo
    echo "================ check.sh summary ================"
    printf '%-28s %-40s %s\n' "section" "result" "seconds"
    printf '%-28s %-40s %s\n' "----------------------------" "----------------------------------------" "-------"
    local row
    for row in "${SUMMARY[@]}"; do
        IFS='|' read -r name status secs <<EOF2
$row
EOF2
        printf '%-28s %-40s %s\n' "$name" "$status" "$secs"
    done
    echo "=================================================="
    if [ "$FAILED" -eq 0 ]; then
        echo "ALL PASS"
    else
        echo "FAILED (stopped at the first failing section)"
    fi
}

section "1 engine Debug" engine_debug

if [ "$FAILED" -eq 0 ] && [ "$want_ubsan" -eq 1 ]; then
    section "2 engine UBSan" engine_ubsan
fi
if [ "$FAILED" -eq 0 ] && [ "$want_asan" -eq 1 ]; then
    section "2a engine ASan" engine_asan
fi
if [ "$FAILED" -eq 0 ] && [ "$want_tsan" -eq 1 ]; then
    section "2b engine TSan" engine_tsan
fi

if [ "$FAILED" -eq 0 ] && [ "$want_swift" -eq 1 ]; then
    if [ -f ChordLab/Package.swift ]; then
        section "3 swift test" swift_tests
    else
        skip "3 swift test" "no ChordLab/Package.swift"
    fi
fi

if [ "$FAILED" -eq 0 ] && [ "$want_bridge" -eq 1 ]; then
    if [ -f ChordLab/bridge/tests/CMakeLists.txt ]; then
        section "4 bridge tests" bridge_tests
    else
        skip "4 bridge tests" "no ChordLab/bridge/tests/CMakeLists.txt"
    fi
fi

if [ "$FAILED" -eq 0 ] && [ "$want_plugin" -eq 1 ]; then
    section "5 plugin (xcodebuild)" plugin_builds
fi

print_summary
exit "$FAILED"
