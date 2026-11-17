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

echo
if [ "$FAILURES" -eq 0 ]; then
    echo "check_shell_probes: ALL PASS"
    exit 0
fi
echo "check_shell_probes: $FAILURES FAILED"
exit 1
