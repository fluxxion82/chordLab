#!/usr/bin/env bash
# Regression probes for the guards of `scripts/check.sh --shell`, filesystem only: no xcodebuild, cmake, ctest or
# swift ever runs. Shims of those four (first on PATH) record their calls and do nothing else, except that the
# xcodebuild shim reports success and creates an empty ChordLab binary under the DSTROOT it is given, so that the
# sections after the build can be reached. Everything happens in a fresh directory under /private/tmp, removed at the
# end, plus (for one probe) a directory build-check-probe/ at the repo root (ignored by git: build-*), removed too.
#
#   scripts/tests/check_shell_probes.sh
#
# One line per check; exit 1 at the end if any failed. Each probe runs check.sh once, in a few seconds.

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
CHECK="$ROOT/scripts/check.sh"
T="$(mktemp -d /private/tmp/chordlab-check-probe.XXXXXX)" || exit 2
T="$(cd -P "$T" && pwd -P)"
OUTSIDE="$ROOT/build-check-probe"   # an existing directory outside every allowed scratch root
trap 'rm -rf "$T" "$OUTSIDE"' EXIT

mkdir -p "$T/bin" "$T/tmp"
for tool in cmake ctest swift; do
    cat >"$T/bin/$tool" <<EOF
#!/bin/sh
echo "$tool \$*" >>"$T/calls.log"
exit 0
EOF
done
cat >"$T/bin/xcodebuild" <<EOF
#!/bin/sh
echo "xcodebuild \$*" >>"$T/calls.log"
for a in "\$@"; do
    case "\$a" in
        DSTROOT=*) d="\${a#DSTROOT=}"; mkdir -p "\$d/ChordLab.vst3/Contents/MacOS" && : >"\$d/ChordLab.vst3/Contents/MacOS/ChordLab" ;;
    esac
done
echo "** BUILD SUCCEEDED **"
exit 0
EOF
chmod +x "$T/bin/"*
export PATH="$T/bin:$PATH"
for tool in cmake ctest swift xcodebuild; do
    if [ "$(command -v "$tool")" != "$T/bin/$tool" ]; then
        echo "check_shell_probes: the $tool shim is not first on PATH; refusing to run" >&2
        exit 2
    fi
done

FAILURES=0
ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; FAILURES=$((FAILURES + 1)); }

# run <env assignments...> -- <check.sh arguments...>: runs check.sh with a fresh call log; sets RC and OUT.
run() {
    local envs=()
    while [ "$1" != "--" ]; do envs+=("$1"); shift; done
    shift
    : >"$T/calls.log"
    OUT="$(env TMPDIR="$T/tmp" "${envs[@]}" "$CHECK" "$@" 2>&1)"
    RC=$?
}
called() { grep -q "$1" "$T/calls.log"; }
empty_dir() { [ -z "$(ls -A "$1" 2>/dev/null)" ]; }

# --- --shell is never combined with an installing section ---------------------------------------------------------
for args in "--shell --plugin" "--plugin --shell" "--shell --all" "--all --shell"; do
    # shellcheck disable=SC2086
    run -- $args
    if [ "$RC" -eq 2 ] && echo "$OUT" | grep -q -- "--shell cannot be combined" && [ ! -s "$T/calls.log" ]; then
        ok "check.sh $args: refused while parsing, before any section ran"
    else
        bad "check.sh $args: status $RC, $(wc -l <"$T/calls.log" | tr -d ' ') tool calls; output tail: $(echo "$OUT" | tail -3 | tr '\n' ' ')"
    fi
done

# --- $TMPDIR is no allowed root ---------------------------------------------------------------------------------------
mkdir -p "$OUTSIDE"
run TMPDIR="$OUTSIDE" CHORDLAB_SHELL_OUT= -- --shell
if [ "$RC" -ne 0 ] && echo "$OUT" | grep -q "refuses the scratch directory" && ! called xcodebuild && empty_dir "$OUTSIDE"; then
    ok "TMPDIR outside /private/tmp and /private/var/folders: the default scratch directory is refused, nothing written"
else
    bad "TMPDIR=$OUTSIDE: status $RC, xcodebuild called: $(called xcodebuild && echo yes || echo no), written: $(ls -A "$OUTSIDE" | tr '\n' ' ')"
fi
rm -rf "$OUTSIDE"

# --- every path the section writes is checked right before use ------------------------------------------------------
# fresh_out: a new scratch directory with an empty target elsewhere; prints nothing.
fresh_out() {
    rm -rf "$T/out" "$T/elsewhere"
    mkdir -p "$T/out" "$T/elsewhere"
}
# probe_link <relative path> <dir|file> <what>: that path inside the scratch directory is a symlink to elsewhere.
probe_link() {
    local rel="$1" kind="$2" what="$3"
    fresh_out
    mkdir -p "$(dirname "$T/out/$rel")"
    if [ "$kind" = dir ]; then
        ln -s "$T/elsewhere" "$T/out/$rel"
    else
        ln -s "$T/elsewhere/stolen.log" "$T/out/$rel"
    fi
    run CHORDLAB_SHELL_OUT="$T/out" -- --shell
    if [ "$RC" -ne 0 ] && echo "$OUT" | grep -q "refuses '$T/out/$rel'" && empty_dir "$T/elsewhere"; then
        ok "$what ($rel) as a symlink: refused, nothing written through it"
    else
        bad "$what ($rel) as a symlink: status $RC, written elsewhere: $(ls -A "$T/elsewhere" | tr '\n' ' '); output tail: $(echo "$OUT" | grep -E "check.sh:|refuses" | head -3 | tr '\n' ' ')"
    fi
}
probe_link dst dir "the scratch DSTROOT"
probe_link build dir "SYMROOT"
probe_link build/obj dir "OBJROOT"
probe_link DerivedData dir "-derivedDataPath"
probe_link vst3host-build dir "the host test's build directory"
probe_link xcodebuild-macOS-VST3.log file "the VST3 build log"
probe_link xcodebuild-macOS-APP.log file "the app build log"

# A real directory that resolves outside the scratch directory: build is real, but obj inside it is a symlink.
fresh_out
mkdir -p "$T/out/build"
ln -s "$T/elsewhere" "$T/out/build/obj"
run CHORDLAB_SHELL_OUT="$T/out" -- --shell
if [ "$RC" -ne 0 ] && echo "$OUT" | grep -q "refuses '$T/out/build/obj'" && ! called xcodebuild && empty_dir "$T/elsewhere"; then
    ok "OBJROOT behind a real SYMROOT, as a symlink: refused before any xcodebuild"
else
    bad "OBJROOT behind a real SYMROOT: status $RC"
fi

# The control: nothing planted. Both builds (shims) pass, the logs are inside the scratch directory, and the host
# test section reaches its build directory (its run then fails here: the cmake shim builds no vst3host).
fresh_out
run CHORDLAB_SHELL_OUT="$T/out" -- --shell
if echo "$OUT" | grep -q "5s shell build (not installed) PASS" && [ -f "$T/out/xcodebuild-macOS-VST3.log" ] &&
    [ -f "$T/out/xcodebuild-macOS-APP.log" ] && called "cmake -S ChordLab/Tests/vst3host -B $T/out/vst3host-build" &&
    ! echo "$OUT" | grep -q "refuses"; then
    ok "nothing planted: no refusal, the logs are written inside the scratch directory, the host test is reached"
else
    bad "nothing planted: status $RC; output tail: $(echo "$OUT" | tail -8 | tr '\n' ' ')"
fi

echo
if [ "$FAILURES" -eq 0 ]; then
    echo "check_shell_probes: ALL PASS"
    exit 0
fi
echo "check_shell_probes: $FAILURES FAILED"
exit 1
