#!/usr/bin/env bash
# Puts the pinned iPlug2 (scripts/iplug2.pin) where the build expects it, or checks the checkout that is there.
#
#   scripts/fetch-iplug2.sh [<dir>]
#       <dir> defaults to IPLUG2_ROOT from ChordLab/config/ChordLab-mac.xcconfig (../../../iPlug2 from ChordLab/projects:
#       an iPlug2 folder next to the repository). When <dir> does not exist (or is empty): a blobless clone of
#       IPLUG2_URL checked out at IPLUG2_COMMIT (detached), then iPlug2's own Dependencies/IPlug/download-vst3-sdk.sh
#       VST3SDK_TAG. When it exists (a developer's checkout, a CI cache): it must be a git work tree at IPLUG2_COMMIT
#       with no changed tracked file; a missing VST3 SDK is fetched as above, one whose tree hash is not
#       VST3SDK_TREE_SHA256 is refused. An existing checkout is never moved to another commit and its SDK never
#       replaced (other projects may share it): the message says what to run instead.
#   scripts/fetch-iplug2.sh --check [<dir>]
#       Checks only: never clones or downloads. Exit 0 when <dir> matches the pin, 1 when not.
#   scripts/fetch-iplug2.sh --print-sdk-hash [<dir>]
#       Prints the VST3 SDK tree hash of <dir> (to record a new pin).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
SELF="$(cd "$(dirname "$0")" && pwd -P)/$(basename "$0")"
PIN="$ROOT/scripts/iplug2.pin"

die()  { echo "fetch-iplug2.sh: $*" >&2; exit 1; }
note() { echo "fetch-iplug2.sh: $*"; }
usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$SELF"; exit 2; }

pin_value() {  # KEY: its value in scripts/iplug2.pin; dies when missing
    local v
    v=$(sed -n "/^$1=/{s///p;q;}" "$PIN" 2>/dev/null || true)
    [ -n "$v" ] || die "$PIN has no $1"
    echo "$v"
}

# The same hash as scripts/release.sh's vst3sdk_tree_sha256 (what the provenance file records): base, pluginterfaces
# and public.sdk without samples, .git and .DS_Store, as download-vst3-sdk.sh vendors them.
sdk_tree_sha256() {
    local sdk="$1/Dependencies/IPlug/VST3_SDK" d dirs=()
    for d in base pluginterfaces public.sdk; do [ -d "$sdk/$d" ] && dirs+=("$d"); done
    [ "${#dirs[@]}" -eq 3 ] || { echo missing; return 0; }
    (cd "$sdk" && find "${dirs[@]}" \( -name .git -o -path public.sdk/samples \) -prune -o -type f ! -name .DS_Store -print0 \
        | LC_ALL=C sort -z | xargs -0 shasum -a 256 | shasum -a 256 | cut -d' ' -f1)
}

default_dir() {  # IPLUG2_ROOT of the xcconfig, relative to ChordLab/projects, made absolute (it need not exist yet)
    local rel parent
    rel=$(sed -n 's/^IPLUG2_ROOT *= *//p' "$ROOT/ChordLab/config/ChordLab-mac.xcconfig" | head -n 1)
    [ -n "$rel" ] || die "no IPLUG2_ROOT in ChordLab/config/ChordLab-mac.xcconfig"
    parent=$(cd "$ROOT/ChordLab/projects/$(dirname "$rel")" 2>/dev/null && pwd -P) \
        || die "the folder that should hold iPlug2 ($ROOT/ChordLab/projects/$(dirname "$rel")) does not exist"
    echo "$parent/$(basename "$rel")"
}

MODE=fetch DIR=""
for arg in "$@"; do
    case "$arg" in
        --check) MODE=check ;;
        --print-sdk-hash) MODE=sdk_hash ;;
        -h|--help) usage ;;
        -*) echo "fetch-iplug2.sh: unknown option '$arg'" >&2; usage ;;
        *) [ -z "$DIR" ] || usage; DIR="$arg" ;;
    esac
done
[ -n "$DIR" ] || DIR=$(default_dir)

URL=$(pin_value IPLUG2_URL)
COMMIT=$(pin_value IPLUG2_COMMIT)
TAG=$(pin_value VST3SDK_TAG)
SDK_SHA=$(pin_value VST3SDK_TREE_SHA256)

if [ "$MODE" = sdk_hash ]; then sdk_tree_sha256 "$DIR"; exit 0; fi

if command -v timeout >/dev/null 2>&1; then TO=(timeout 1200)
elif command -v gtimeout >/dev/null 2>&1; then TO=(gtimeout 1200)
else TO=(); fi

empty_dir() { [ -d "$1" ] && [ -z "$(ls -A "$1")" ]; }

if [ ! -e "$DIR" ] || empty_dir "$DIR"; then
    [ "$MODE" = fetch ] || die "no iPlug2 at $DIR (run scripts/fetch-iplug2.sh${DIR:+ $DIR} to fetch the pinned one)"
    note "cloning $URL into $DIR (blobless) at ${COMMIT:0:9}"
    ${TO[@]+"${TO[@]}"} git clone --quiet --filter=blob:none --no-checkout "$URL" "$DIR"
    # The pin may name a commit no branch carries any more: fetch it by id when the clone lacks it.
    git -C "$DIR" cat-file -e "$COMMIT^{commit}" 2>/dev/null || ${TO[@]+"${TO[@]}"} git -C "$DIR" fetch --quiet origin "$COMMIT"
    git -C "$DIR" -c advice.detachedHead=false checkout --quiet --detach "$COMMIT"
fi

[ -d "$DIR" ] || die "$DIR is not a directory"
git -C "$DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "$DIR is not a git work tree"
[ "$(git -C "$DIR" rev-parse --show-toplevel)" = "$(cd "$DIR" && pwd -P)" ] || die "$DIR is inside another git work tree, not an iPlug2 checkout"
head=$(git -C "$DIR" rev-parse HEAD)
[ "$head" = "$COMMIT" ] || die "$DIR is at ${head:0:9}, the pin says ${COMMIT:0:9}: check it out (git -C '$DIR' checkout --detach $COMMIT) or move the pin (scripts/iplug2.pin)"
changed=$(git -C "$DIR" status --porcelain --untracked-files=no)
[ -z "$changed" ] || die "$DIR has changed tracked files: $(head -n 5 <<<"$changed" | tr '\n' ' ')"

sha=$(sdk_tree_sha256 "$DIR")
if [ "$sha" = missing ]; then
    [ "$MODE" = fetch ] || die "no VST3 SDK in $DIR/Dependencies/IPlug/VST3_SDK (run scripts/fetch-iplug2.sh to fetch it)"
    note "fetching the VST3 SDK $TAG with iPlug2's Dependencies/IPlug/download-vst3-sdk.sh"
    (cd "$DIR/Dependencies/IPlug" && ${TO[@]+"${TO[@]}"} ./download-vst3-sdk.sh "$TAG") >/dev/null 2>&1 \
        || die "download-vst3-sdk.sh $TAG failed (run it in $DIR/Dependencies/IPlug to see why)"
    sha=$(sdk_tree_sha256 "$DIR")
    changed=$(git -C "$DIR" status --porcelain --untracked-files=no)
    [ -z "$changed" ] || die "download-vst3-sdk.sh left changed tracked files in $DIR: $(head -n 5 <<<"$changed" | tr '\n' ' ')"
fi
[ "$sha" = "$SDK_SHA" ] || die "the VST3 SDK in $DIR hashes to $sha, the pin says $SDK_SHA ($TAG): refetch it ((cd '$DIR/Dependencies/IPlug' && ./download-vst3-sdk.sh $TAG)) or move the pin"

note "iPlug2 $DIR at ${COMMIT:0:9} with VST3 SDK $TAG (tree ${SDK_SHA:0:12}): matches scripts/iplug2.pin"
