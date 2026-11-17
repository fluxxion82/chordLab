#!/usr/bin/env bash
# Regression probes for the guards of `scripts/check.sh --shell`, filesystem only: no xcodebuild, cmake, ctest or
# swift ever runs. Shims of those four (first on PATH) record their calls and do nothing else, except that the
# xcodebuild shim reports success and creates an empty ChordLab binary under the DSTROOT it is given, so that the
# sections after the build can be reached. Everything happens in a fresh directory under /private/tmp made by this
# run, plus (for one probe) a fresh build-check-probe.XXXXXX directory at the repository root (ignored by git:
# build-*), the stand-in for a place outside every allowed scratch root. Each is removed at the end, and only if this
# run created it.
#
#   scripts/tests/check_shell_probes.sh
#
# One line per check; exit 1 at the end if any failed. Each probe runs check.sh once, in a few seconds.

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
CHECK="$ROOT/scripts/check.sh"
T=""
OUTSIDE=""
cleanup() {
    [ -n "$T" ] && rm -rf "$T"
    [ -n "$OUTSIDE" ] && rm -rf "$OUTSIDE"
}
trap cleanup EXIT
T="$(mktemp -d /private/tmp/chordlab-check-probe.XXXXXX)" || exit 2
T="$(cd -P "$T" && pwd -P)" || exit 2

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
[ -n "\${PROBE_TOUCH:-}" ] && echo "changed by the build" >>"\$PROBE_TOUCH"
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

# run <env assignments...> -- <check.sh arguments...>: runs check.sh with a fresh call log; sets RC, OUT and RUN (the
# run directory check.sh printed, empty if none).
run() {
    local envs=()
    while [ "$1" != "--" ]; do envs+=("$1"); shift; done
    shift
    : >"$T/calls.log"
    OUT="$(env TMPDIR="$T/tmp" "${envs[@]}" "$CHECK" "$@" 2>&1)"
    RC=$?
    RUN="$(echo "$OUT" | sed -n 's/^run directory: *//p' | head -1)"
}
called() { grep -q -- "$1" "$T/calls.log"; }
empty_dir() { [ -z "$(ls -A "$1" 2>/dev/null)" ]; }

# A sentinel tree outside the scratch directory: what a planted link would expose. snapshot prints its names and the
# sha256 of every file, without following links.
make_sentinel() {
    rm -rf "$T/sentinel"
    mkdir -p "$T/sentinel/dir"
    printf 'sentinel file, must never change\n' >"$T/sentinel/file.bin"
    printf 'second sentinel file\n' >"$T/sentinel/file2.bin"
    printf 'inside the sentinel directory\n' >"$T/sentinel/dir/keep.txt"
}
snapshot() {
    (cd "$T/sentinel" && find . -print | LC_ALL=C sort && find . -type f -exec shasum -a 256 {} + | LC_ALL=C sort)
}
fresh_out() {
    rm -rf "$T/out"
    mkdir -p "$T/out"
}
# Every scratch path a tool was given lies below the run directory.
only_run_paths() {
    local paths bad_paths
    paths="$(grep -o "$T/out/[^ ]*" "$T/calls.log")"
    [ -n "$paths" ] || return 1
    bad_paths="$(echo "$paths" | grep -v "^$RUN/")"
    [ -z "$bad_paths" ]
}

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
OUTSIDE="$(mktemp -d "$ROOT/build-check-probe.XXXXXX")" || { bad "could not create the outside stand-in"; OUTSIDE=""; }
if [ -n "$OUTSIDE" ]; then
    run TMPDIR="$OUTSIDE" CHORDLAB_SHELL_OUT= -- --shell
    if [ "$RC" -ne 0 ] && echo "$OUT" | grep -q "refuses the scratch directory" && ! called xcodebuild && empty_dir "$OUTSIDE"; then
        ok "TMPDIR outside /private/tmp and /private/var/folders: the default scratch directory is refused, nothing written"
    else
        bad "TMPDIR=$OUTSIDE: status $RC, xcodebuild called: $(called xcodebuild && echo yes || echo no), written: $(ls -A "$OUTSIDE" | tr '\n' ' ')"
    fi
fi

# --- links planted at the paths earlier versions used are neither followed nor written ----------------------------------
make_sentinel
fresh_out
ln -s "$T/sentinel/dir" "$T/out/build"
ln -s "$T/sentinel/dir" "$T/out/dst"
mkdir -p "$T/out/DerivedData" "$T/out/vst3host-build"
ln -s "$T/sentinel/dir" "$T/out/DerivedData/ModuleCache.noindex"
ln -s "$T/sentinel/dir" "$T/out/vst3host-build/CMakeFiles"
ln "$T/sentinel/file.bin" "$T/out/xcodebuild-macOS-VST3.log"          # a hard link: same file, same volume
ln -s "$T/sentinel/file2.bin" "$T/out/xcodebuild-macOS-APP.log"
before="$(snapshot)"
run CHORDLAB_SHELL_OUT="$T/out" -- --shell
after="$(snapshot)"
case "$RUN" in
    "$T/out"/run.??????) run_ok=1 ;;
    *) run_ok=0 ;;
esac
if [ "$run_ok" -eq 1 ] && [ "$before" = "$after" ] && echo "$OUT" | grep -q "5s shell build (not installed) PASS" &&
    [ -f "$RUN/xcodebuild-macOS-VST3.log" ] && [ -f "$RUN/dst/ChordLab.vst3/Contents/MacOS/ChordLab" ] &&
    called "cmake -S ChordLab/Tests/vst3host -B $RUN/vst3host-build" && only_run_paths; then
    ok "links at the old fixed paths (build, dst, DerivedData/ModuleCache.noindex, vst3host-build/CMakeFiles, a hard-linked and a symlinked log): the sentinels are byte-identical, every tool was given paths below $(basename "$RUN") only"
else
    bad "links at the old fixed paths: run '$RUN', sentinels unchanged: $([ "$before" = "$after" ] && echo yes || echo no); paths given: $(grep -o "$T/out/[^ ]*" "$T/calls.log" | sort -u | tr '\n' ' ')"
fi
if [ -L "$T/out/build" ] && [ -L "$T/out/dst" ] && [ -L "$T/out/DerivedData/ModuleCache.noindex" ] &&
    [ -L "$T/out/vst3host-build/CMakeFiles" ] && [ -f "$T/out/xcodebuild-macOS-VST3.log" ] && [ -L "$T/out/xcodebuild-macOS-APP.log" ]; then
    ok "the planted entries themselves are left as they were (nothing but run.* is touched)"
else
    bad "a planted entry was removed or replaced"
fi

# --- an old run directory is removed without following the links inside it -----------------------------------------
make_sentinel
fresh_out
mkdir -p "$T/out/run.OLD1/build"
ln -s "$T/sentinel" "$T/out/run.OLD1/link-to-sentinel"
ln -s "$T/sentinel/dir" "$T/out/run.OLD1/build/obj"
printf 'old\n' >"$T/out/run.OLD1/old.log"
before="$(snapshot)"
run CHORDLAB_SHELL_OUT="$T/out" -- --shell
after="$(snapshot)"
if [ ! -e "$T/out/run.OLD1" ] && [ ! -L "$T/out/run.OLD1" ] && [ "$before" = "$after" ] && [ -n "$RUN" ] && [ "$RUN" != "$T/out/run.OLD1" ]; then
    ok "an old run.* directory holding links out of the scratch directory is removed; the sentinel's content survives"
else
    bad "old run directory: still there: $([ -e "$T/out/run.OLD1" ] && echo yes || echo no), sentinels unchanged: $([ "$before" = "$after" ] && echo yes || echo no)"
fi

# --- a symlink named run.* is no run directory ------------------------------------------------------------------------
make_sentinel
fresh_out
ln -s "$T/sentinel" "$T/out/run.Y"
before="$(snapshot)"
run CHORDLAB_SHELL_OUT="$T/out" -- --shell
after="$(snapshot)"
if [ -L "$T/out/run.Y" ] && [ "$before" = "$after" ] && [ -n "$RUN" ] && [ "$RUN" != "$T/out/run.Y" ] &&
    [ "$(cd -P "$RUN" && pwd -P)" = "$RUN" ] && ! grep -q "$T/out/run.Y" "$T/calls.log"; then
    ok "a symlink named run.Y is neither followed, removed nor used; the run directory is a new real one"
else
    bad "symlink run.Y: still a link: $([ -L "$T/out/run.Y" ] && echo yes || echo no), sentinels unchanged: $([ "$before" = "$after" ] && echo yes || echo no), run '$RUN'"
fi

# --- an installed plug-in that lives in the scratch directory ---------------------------------------------------------
# The installed set is extended through the test seams with paths inside this probe's own directory ($T/inst); the
# real ~/Library and ~/Applications are only hashed (read), as by every --shell run.
fresh_out
rm -rf "$T/inst"
mkdir -p "$T/out/run.OLD/dst/ChordLab.vst3/Contents/MacOS" "$T/inst/VST3"
printf 'a scratch build someone linked into the host\n' >"$T/out/run.OLD/dst/ChordLab.vst3/Contents/MacOS/ChordLab"
ln -s "$T/out/run.OLD/dst/ChordLab.vst3" "$T/inst/VST3/ChordLab.vst3"   # the bundle is a symlink into an old run
sum_before="$(shasum -a 256 "$T/out/run.OLD/dst/ChordLab.vst3/Contents/MacOS/ChordLab")"
run CHORDLAB_SHELL_OUT="$T/out" CHORDLAB_INSTALLED_VST3_BIN="$T/inst/VST3/ChordLab.vst3/Contents/MacOS/ChordLab" -- --shell
if [ "$RC" -ne 0 ] && echo "$OUT" | grep -q "the installed plug-in points into the gate's scratch directory" &&
    [ -f "$T/out/run.OLD/dst/ChordLab.vst3/Contents/MacOS/ChordLab" ] &&
    [ "$(shasum -a 256 "$T/out/run.OLD/dst/ChordLab.vst3/Contents/MacOS/ChordLab")" = "$sum_before" ] &&
    ! called xcodebuild && ! called "ChordLab/Tests/vst3host" && [ -z "$RUN" ]; then
    ok "an installed bundle symlinked into run.OLD: refused before anything is removed or built; the binary is intact, no build shim ran"
else
    bad "installed bundle in run.OLD: status $RC, binary intact: $([ -f "$T/out/run.OLD/dst/ChordLab.vst3/Contents/MacOS/ChordLab" ] && echo yes || echo no); $(echo "$OUT" | grep "check.sh:" | head -2 | tr '\n' ' ')"
fi

# A dangling installed link whose target string points into the scratch directory counts too.
fresh_out
rm -rf "$T/inst"
mkdir -p "$T/inst/App/ChordLab.app/Contents/MacOS"
ln -s "$T/out/run.GONE/ChordLab" "$T/inst/App/ChordLab.app/Contents/MacOS/ChordLab"
run CHORDLAB_SHELL_OUT="$T/out" CHORDLAB_INSTALLED_APP_BIN="$T/inst/App/ChordLab.app/Contents/MacOS/ChordLab" -- --shell
if [ "$RC" -ne 0 ] && echo "$OUT" | grep -q "points into the gate's scratch directory" && ! called xcodebuild; then
    ok "a dangling installed link whose target lies in the scratch directory: refused"
else
    bad "dangling installed link into the scratch directory: status $RC"
fi

# An installed real file outside the scratch directory, old runs present: they are removed, the hashes compare.
fresh_out
rm -rf "$T/inst"
mkdir -p "$T/out/run.OLD2" "$T/inst/App/ChordLab.app/Contents/MacOS"
printf 'the real installed app\n' >"$T/inst/App/ChordLab.app/Contents/MacOS/ChordLab"
app="$T/inst/App/ChordLab.app/Contents/MacOS/ChordLab"
app_sum="$(shasum -a 256 "$app" | cut -d' ' -f1)"
run CHORDLAB_SHELL_OUT="$T/out" CHORDLAB_INSTALLED_APP_BIN="$app" -- --shell
if echo "$OUT" | grep -q "5s shell build (not installed) PASS" && [ ! -e "$T/out/run.OLD2" ] &&
    [ "$(echo "$OUT" | grep -c "$app_sum  $app")" -ge 2 ]; then
    ok "an installed file outside the scratch directory: old runs removed, its hash read before and after, the section passes"
else
    bad "installed file outside: status $RC, run.OLD2 left: $([ -e "$T/out/run.OLD2" ] && echo yes || echo no)"
fi
# ... and a build that changes it is caught.
run CHORDLAB_SHELL_OUT="$T/out" CHORDLAB_INSTALLED_APP_BIN="$app" PROBE_TOUCH="$app" -- --shell
if [ "$RC" -ne 0 ] && echo "$OUT" | grep -q "5s shell build (not installed) FAIL" &&
    echo "$OUT" | grep -q "an installed binary changed during --shell"; then
    ok "a build that changes the installed file: the section fails with \"an installed binary changed\""
else
    bad "a changed installed file was not caught: status $RC"
fi

# No installed file at all: absent before and after, passes.
fresh_out
rm -rf "$T/inst"
run CHORDLAB_SHELL_OUT="$T/out" CHORDLAB_INSTALLED_VST3_BIN="$T/inst/none.vst3/Contents/MacOS/ChordLab" \
    CHORDLAB_INSTALLED_APP_BIN="$T/inst/none.app/Contents/MacOS/ChordLab" -- --shell
if echo "$OUT" | grep -q "5s shell build (not installed) PASS" &&
    [ "$(echo "$OUT" | grep -c "absent  $T/inst/none")" -ge 4 ]; then
    ok "no installed file: absent before and after, the section passes"
else
    bad "no installed file: status $RC; $(echo "$OUT" | grep absent | tr '\n' ' ')"
fi

# --- the control: nothing planted ------------------------------------------------------------------------------------
fresh_out
run CHORDLAB_SHELL_OUT="$T/out" -- --shell
if echo "$OUT" | grep -q "5s shell build (not installed) PASS" && [ -f "$RUN/xcodebuild-macOS-VST3.log" ] &&
    [ -f "$RUN/xcodebuild-macOS-APP.log" ] && called "cmake -S ChordLab/Tests/vst3host -B $RUN/vst3host-build" &&
    only_run_paths && [ "$(ls -A "$T/out")" = "$(basename "$RUN")" ]; then
    ok "nothing planted: the builds pass, the logs and every tool path are in the run directory, the host test is reached, nothing else is created"
else
    bad "nothing planted: status $RC, run '$RUN', scratch holds: $(ls -A "$T/out" | tr '\n' ' ')"
fi

# A second invocation replaces the first run directory with a new one.
first="$RUN"
run CHORDLAB_SHELL_OUT="$T/out" -- --shell
if [ -n "$RUN" ] && [ "$RUN" != "$first" ] && [ ! -e "$first" ] && [ "$(ls -A "$T/out")" = "$(basename "$RUN")" ]; then
    ok "a second invocation removes the first run directory and works in a new one"
else
    bad "second invocation: first '$first', second '$RUN', scratch holds: $(ls -A "$T/out" | tr '\n' ' ')"
fi

echo
if [ "$FAILURES" -eq 0 ]; then
    echo "check_shell_probes: ALL PASS"
    exit 0
fi
echo "check_shell_probes: $FAILURES FAILED"
exit 1
