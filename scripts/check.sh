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
#   scripts/check.sh --plugin   ... plus both Xcode schemes (macOS-VST3, macOS-APP), no signing; INSTALLS them
#                               (the project installs into ~/Library/Audio/Plug-Ins/VST3 and ~/Applications)
#   scripts/check.sh --shell    ... plus the same two schemes built into a scratch directory, installing nothing:
#                               $CHORDLAB_SHELL_OUT (default ${TMPDIR:-/tmp}/chordlab-shell-gate), which must lie
#                               inside /private/tmp or /private/var/folders (where macOS's own $TMPDIR is; $TMPDIR
#                               itself is no allowed root). Each invocation works in a fresh run.XXXXXX directory
#                               there (the previous runs are removed): DSTROOT, SYMROOT, OBJROOT, -derivedDataPath and
#                               the logs, all cold (scripts/tests/check_shell_probes.sh). Fails if the installed VST3
#                               or app binary changes. Then builds ChordLab/Tests/vst3host in the same run directory and
#                               runs it against the scratch ChordLab.vst3 (section 5t)
#   scripts/check.sh --all      everything but --shell (--shell with --plugin or --all is refused)
#   scripts/check.sh --release  ... plus the release script's preflight, in report mode
# Flags combine; section 1 always runs first.
#
# Sanitizers: UBSan, ASan and TSan each get their own build directory. On Apple clang 17 (Xcode 16) ASan
# deadlocked before main() and TSan hung, which is why every sanitizer step has a hard timeout; since
# Apple clang 21 (Xcode 27.1, 2026-10-24) all three run the full suite cleanly. `--all` includes them.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
SELF="$(cd "$(dirname "$0")" && pwd -P)/$(basename "$0")"   # for --help after the cd
cd "$ROOT" || exit 2

DEFAULT_TIMEOUT=300
UBSAN_TIMEOUT=600
PLUGIN_TIMEOUT=600   # a cold xcodebuild of the plug-in exceeds the default

want_ubsan=0 want_asan=0 want_tsan=0 want_swift=0 want_bridge=0 want_plugin=0 want_shell=0 want_release=0
for arg in "$@"; do
    case "$arg" in
        --ubsan) want_ubsan=1 ;;
        --asan) want_asan=1 ;;
        --tsan) want_tsan=1 ;;
        --swift) want_swift=1 ;;
        --bridge) want_bridge=1 ;;
        --plugin) want_plugin=1 ;;
        --shell) want_shell=1 ;;
        --release) want_release=1 ;;
        --all) want_ubsan=1 want_asan=1 want_tsan=1 want_swift=1 want_bridge=1 want_plugin=1 want_release=1 ;;
        -h|--help) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$SELF"; exit 0 ;;   # the header comment
        *) echo "check.sh: unknown option '$arg' (try --help)" >&2; exit 2 ;;
    esac
done
# --plugin (and --all, which includes it) installs over the very binaries --shell must leave untouched, and its section
# runs first: --shell's before/after hashes would then compare the overwritten installation with itself.
if [ "$want_shell" -eq 1 ] && [ "$want_plugin" -eq 1 ]; then
    echo "check.sh: --shell cannot be combined with --plugin or --all: their installing build would run first, and --shell would compare the overwritten installation with itself" >&2
    exit 2
fi

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

# One Xcode scheme, built unsigned: plugin_scheme <scheme> <log> [extra xcodebuild arguments...]. Fails when
# "BUILD SUCCEEDED" is missing from the log, or when the log holds any error or warning that points into ChordLab/
# or engine/ (third-party and SDK noise is ignored). The Swift driver's "Could not read priors" warning names a
# .priors file under the build directory; it only means the incremental-build cache was stale (first build after an
# Xcode update), so it is ignored too.
plugin_scheme() {
    local scheme="$1" log="$2"
    shift 2
    echo "+ xcodebuild -scheme $scheme${*:+ $*} (log: $log)"
    "$TIMEOUT_CMD" "$PLUGIN_TIMEOUT" xcodebuild -project ChordLab/projects/ChordLab-macOS.xcodeproj \
        -scheme "$scheme" -configuration Debug "$@" \
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
    plugin_scheme macOS-VST3 "$LOG_DIR/chordlab-xcodebuild-macOS-VST3.log" || return 1
    plugin_scheme macOS-APP "$LOG_DIR/chordlab-xcodebuild-macOS-APP.log" || return 1
}

# The installed builds --shell must leave exactly as they were: the VST3 Ableton Live loads, and the app.
INSTALLED_VST3_BIN="$HOME/Library/Audio/Plug-Ins/VST3/ChordLab.vst3/Contents/MacOS/ChordLab"
INSTALLED_APP_BIN="$HOME/Applications/ChordLab.app/Contents/MacOS/ChordLab"

# installed_sha <file>: prints the file's sha256, or "absent" when there is no such file. Fails (status 1, nothing
# printed) when the file exists but could not be hashed: an empty hash must never read as "unchanged".
installed_sha() {
    if [ ! -e "$1" ]; then
        echo absent
        return 0
    fi
    local sha
    sha="$(shasum -a 256 "$1" 2>/dev/null | cut -d' ' -f1)"
    case "$sha" in
        [0-9a-f][0-9a-f][0-9a-f][0-9a-f]*) echo "$sha" ;;
        *) echo "check.sh: could not hash $1" >&2; return 1 ;;
    esac
}

# physical_dir <path>: the absolute physical path of `path` (symlinks and `..` resolved, the case as stored) without
# creating anything: its deepest existing ancestor is resolved with `cd -P`, and the components that do not exist
# yet are appended as given. Fails for a component "." or ".." among those, or when the ancestor cannot be entered.
physical_dir() {
    local p="$1" rest="" base parent
    while [ ! -d "$p" ]; do
        base="$(basename "$p")"
        case "$base" in
            .|..) return 1 ;;
        esac
        rest="/$base$rest"
        parent="$(dirname "$p")"
        [ "$parent" != "$p" ] || return 1
        p="$parent"
    done
    p="$(cd -P "$p" 2>/dev/null && pwd -P)" || return 1
    [ -n "$p" ] || return 1
    p="${p%/}"
    echo "$p$rest"
}

# shell_out_allowed <physical path>: true when the path lies strictly inside one of the scratch roots --shell may
# write to: /private/tmp or /private/var/folders (resolved physically too). An allow-list, so a path in ~/Library or
# ~/Applications, however spelled (another case, a firmlink, `..`, a symlink), is refused. $TMPDIR is deliberately no
# root of its own: TMPDIR=~/Library/Audio/Plug-Ins/VST3 would otherwise allow the installed plug-in's folder; on macOS
# the system's $TMPDIR lies under /private/var/folders and so stays allowed.
shell_out_allowed() {
    local root
    for root in /private/tmp /private/var/folders; do
        [ -n "$root" ] && [ -d "$root" ] || continue
        root="$(cd -P "$root" 2>/dev/null && pwd -P)" || continue
        root="${root%/}"
        [ -n "$root" ] || continue
        case "$1" in
            "$root"/?*) return 0 ;;
        esac
    done
    return 1
}

# shell_out_dir: prints the --shell scratch directory, physical and created, or fails with a message on stderr. The
# allow-list is checked before anything is created and again on the created directory's physical path.
shell_out_dir() {
    local tmp="${TMPDIR:-/tmp}"
    local out="${CHORDLAB_SHELL_OUT:-${tmp%/}/chordlab-shell-gate}"
    case "$out" in
        /*) ;;
        *) out="$ROOT/$out" ;;
    esac
    local candidate
    if ! candidate="$(physical_dir "$out")"; then
        echo "check.sh: --shell cannot resolve the scratch directory '$out' (no '.' or '..' in the part that does not exist yet)" >&2
        return 1
    fi
    if ! shell_out_allowed "$candidate"; then
        echo "check.sh: --shell refuses the scratch directory '$out' (resolves to '$candidate'): it must lie inside /private/tmp or /private/var/folders" >&2
        return 1
    fi
    mkdir -p "$candidate" || return 1
    local resolved
    resolved="$(cd -P "$candidate" 2>/dev/null && pwd -P)" || return 1
    if ! shell_out_allowed "$resolved"; then
        echo "check.sh: --shell refuses the scratch directory '$out' (created as '$resolved'): it must lie inside /private/tmp or /private/var/folders" >&2
        return 1
    fi
    echo "$resolved"
}

# The run directory of this --shell invocation: set by section 5s (shell_run_dir), read by section 5t. Empty until then.
SHELL_RUN=""

# shell_run_dir <out>: prints a FRESH private directory for this invocation, $out/run.XXXXXX (mktemp -d: created new,
# mode 0700, never an existing path), after checking that it is a real directory whose physical path lies inside the
# physical scratch directory `out`; fails with a message otherwise. Everything --shell writes goes below it: DSTROOT,
# SYMROOT, OBJROOT, derived data, the host test's build directory and the logs. Nothing that existed before the run is
# written to or read, so nothing planted in the scratch directory (a symlink or a hard link at a path an earlier run
# used, or anywhere below such a directory) can redirect a write. Before creating it, the previous runs are removed:
# only entries directly in `out` named run.* that are real directories (a symlink named run.* is left alone, never
# followed), with rm -rf, which removes the symlinks inside a tree without following them. Nothing else in `out` is
# touched.
shell_run_dir() {
    local out="$1" entry run phys
    for entry in "$out"/run.*; do
        [ -d "$entry" ] && [ ! -L "$entry" ] || continue
        rm -rf "$entry" || { echo "check.sh: --shell could not remove the previous run '$entry'" >&2; return 1; }
    done
    run="$(mktemp -d "$out/run.XXXXXX")" || { echo "check.sh: --shell could not create a run directory in '$out'" >&2; return 1; }
    if [ -L "$run" ] || [ ! -d "$run" ]; then
        echo "check.sh: --shell refuses the run directory '$run': not a real directory" >&2
        return 1
    fi
    phys="$(cd -P "$run" 2>/dev/null && pwd -P)" || { echo "check.sh: --shell cannot resolve the run directory '$run'" >&2; return 1; }
    case "$phys" in
        "$out"/run.?*) ;;
        *) echo "check.sh: --shell refuses the run directory '$run': it resolves to '$phys', outside '$out'" >&2; return 1 ;;
    esac
    echo "$phys"
}

# Section 5s: both schemes as --plugin builds them (Debug, unsigned), but into a scratch directory. The project sets
# DEPLOYMENT_LOCATION=YES, INSTALL_PATH=/ and a per-target DSTROOT of ~/Library/Audio/Plug-Ins/VST3 (VST3) or
# ~/Applications (APP); a DSTROOT on the command line overrides it (scripts/release.sh does the same), SYMROOT and
# OBJROOT replace the project's ChordLab/build-mac, and -derivedDataPath keeps Xcode's own derived data (module
# caches, logs) out of ~/Library/Developer. The scratch directory must pass shell_out_dir's allow-list, and every one
# of those paths, and the logs, lies in a fresh run directory below it (shell_run_dir): a cold build every time. Fails
# if a build fails, if the VST3 binary is not under the run's DSTROOT, or if the installed VST3 or app binary cannot be
# hashed or its sha256 differs before and after.
shell_builds() {
    if [ ! -d ChordLab/projects/ChordLab-macOS.xcodeproj ]; then
        echo "check.sh: ChordLab/projects/ChordLab-macOS.xcodeproj not found" >&2
        return 1
    fi
    local out run
    out="$(shell_out_dir)" || return 1
    run="$(shell_run_dir "$out")" || return 1
    SHELL_RUN="$run"
    echo "scratch directory: $out"
    echo "run directory:     $run"
    local dst="$run/dst"
    local product="$dst/ChordLab.vst3/Contents/MacOS/ChordLab"
    local vst3Before vst3After appBefore appAfter
    vst3Before="$(installed_sha "$INSTALLED_VST3_BIN")" || return 1
    appBefore="$(installed_sha "$INSTALLED_APP_BIN")" || return 1
    echo "installed VST3 before: $vst3Before ($INSTALLED_VST3_BIN)"
    echo "installed app before:  $appBefore ($INSTALLED_APP_BIN)"
    local rc=0 scheme
    for scheme in macOS-VST3 macOS-APP; do
        plugin_scheme "$scheme" "$run/xcodebuild-$scheme.log" \
            -derivedDataPath "$run/DerivedData" \
            DSTROOT="$dst" SYMROOT="$run/build" OBJROOT="$run/build/obj" || { rc=1; break; }
    done
    vst3After="$(installed_sha "$INSTALLED_VST3_BIN")" || return 1
    appAfter="$(installed_sha "$INSTALLED_APP_BIN")" || return 1
    echo "installed VST3 after:  $vst3After"
    echo "installed app after:   $appAfter"
    if [ "$vst3Before" != "$vst3After" ]; then
        echo "check.sh: the installed VST3 binary changed during --shell ($vst3Before -> $vst3After)" >&2
        return 1
    fi
    if [ "$appBefore" != "$appAfter" ]; then
        echo "check.sh: the installed app binary changed during --shell ($appBefore -> $appAfter)" >&2
        return 1
    fi
    [ "$rc" -eq 0 ] || return 1
    if [ ! -f "$product" ]; then
        echo "check.sh: the built VST3 binary is not at $product" >&2
        return 1
    fi
    echo "built VST3: $product ($(shasum -a 256 "$product" | cut -d' ' -f1))"
    [ -d "$dst/ChordLab.app" ] && echo "built app:  $dst/ChordLab.app"
    return 0
}

# Section 5t: the shell's runtime test. Configures and builds ChordLab/Tests/vst3host (a command-line VST3 host made
# from iPlug2's vendored VST3 SDK and the engine) in the run directory section 5s created (SHELL_RUN), then runs it
# against the ChordLab.vst3 that section 5s just built there: the plug-in is loaded into the host's process and
# constructed, given states, processed and destroyed, with no DAW, window or device. Fails on a failed build, a
# non-zero exit (a failed check exits 1, a crash 70 or by its signal) or a timeout, or if the installed VST3 or app
# binary changes.
shell_host_test() {
    if [ -z "$SHELL_RUN" ]; then
        echo "check.sh: no run directory (section 5s creates it)" >&2
        return 1
    fi
    local bundle="$SHELL_RUN/dst/ChordLab.vst3"
    local build="$SHELL_RUN/vst3host-build"
    echo "run directory: $SHELL_RUN"
    if [ ! -f "$bundle/Contents/MacOS/ChordLab" ]; then
        echo "check.sh: no scratch ChordLab.vst3 at $bundle (section 5s builds it)" >&2
        return 1
    fi
    local vst3Before vst3After appBefore appAfter rc=0
    vst3Before="$(installed_sha "$INSTALLED_VST3_BIN")" || return 1
    appBefore="$(installed_sha "$INSTALLED_APP_BIN")" || return 1
    step "$DEFAULT_TIMEOUT" cmake -S ChordLab/Tests/vst3host -B "$build" -DCMAKE_BUILD_TYPE=Debug || rc=1
    [ "$rc" -eq 0 ] && { step "$PLUGIN_TIMEOUT" cmake --build "$build" -j8 || rc=1; }
    [ "$rc" -eq 0 ] && { step "$DEFAULT_TIMEOUT" "$build/vst3host" "$bundle" || rc=1; }
    vst3After="$(installed_sha "$INSTALLED_VST3_BIN")" || return 1
    appAfter="$(installed_sha "$INSTALLED_APP_BIN")" || return 1
    echo "installed VST3 before/after: $vst3Before / $vst3After"
    echo "installed app before/after:  $appBefore / $appAfter"
    if [ "$vst3Before" != "$vst3After" ] || [ "$appBefore" != "$appAfter" ]; then
        echo "check.sh: an installed binary changed during the shell host test" >&2
        return 1
    fi
    return "$rc"
}

release_preflight() {
    step "$DEFAULT_TIMEOUT" scripts/release.sh preflight --report || return 1
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

if [ "$FAILED" -eq 0 ] && [ "$want_shell" -eq 1 ]; then
    section "5s shell build (not installed)" shell_builds
fi
if [ "$FAILED" -eq 0 ] && [ "$want_shell" -eq 1 ]; then
    section "5t shell host test" shell_host_test
fi

if [ "$FAILED" -eq 0 ] && [ "$want_release" -eq 1 ]; then
    section "6 release preflight" release_preflight
fi

print_summary
exit "$FAILED"
