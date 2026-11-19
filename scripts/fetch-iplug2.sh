#!/usr/bin/env bash
# Puts the pinned iPlug2 (scripts/iplug2.pin) where the build expects it, or checks the checkout that is there.
#
#   scripts/fetch-iplug2.sh [<dir>]
#       <dir> defaults to IPLUG2_ROOT from ChordLab/config/ChordLab-mac.xcconfig (../../../iPlug2 from ChordLab/projects:
#       an iPlug2 folder next to the repository). When <dir> does not exist (or is empty): a blobless clone of
#       IPLUG2_URL checked out at IPLUG2_COMMIT (detached) with the VST3 SDK VST3SDK_TAG, all assembled in a temporary
#       sibling folder and moved to <dir> only once complete and verified, so an interrupted run leaves no half
#       checkout behind. When <dir> exists (a developer's checkout, a CI cache): it must be a git work tree at
#       IPLUG2_COMMIT with no changed tracked file and the SDK whose tree hash is VST3SDK_TREE_SHA256; an SDK folder
#       holding nothing but iPlug2's own README.md gets the SDK fetched as below. Anything else is refused, never
#       repaired: an existing checkout is never moved to another commit and its SDK never replaced or completed (other
#       projects may share it); the message says what to run instead.
#       The SDK comes from iPlug2's own Dependencies/IPlug/download-vst3-sdk.sh VST3SDK_TAG, run in a scratch folder
#       outside every git work tree; only a result with the pinned tree hash is moved into the checkout.
#   scripts/fetch-iplug2.sh --check [<dir>]
#       Checks only: never clones or downloads. Exit 0 when <dir> matches the pin, 1 when not.
#   scripts/fetch-iplug2.sh --print-sdk-hash [<dir>]
#       Prints the VST3 SDK tree hash of <dir> (to record a new pin), or "absent" or "partial".
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
SELF="$(cd "$(dirname "$0")" && pwd -P)/$(basename "$0")"
PIN="$ROOT/scripts/iplug2.pin"
SDK_REL=Dependencies/IPlug/VST3_SDK

die()  { echo "fetch-iplug2.sh: $*" >&2; exit 1; }
note() { echo "fetch-iplug2.sh: $*"; }
usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$SELF"; exit 2; }

pin_value() {  # KEY: its value in scripts/iplug2.pin; dies when missing
    local v
    v=$(sed -n "/^$1=/{s///p;q;}" "$PIN" 2>/dev/null || true)
    [ -n "$v" ] || die "$PIN has no $1"
    echo "$v"
}

# sdk_state <SDK folder>: "absent" when none of base, pluginterfaces and public.sdk is there, "partial" when some are,
# else their tree hash: the same hash as scripts/release.sh's vst3sdk_tree_sha256 (what the provenance file records),
# over the three without samples, .git and .DS_Store, as download-vst3-sdk.sh vendors them.
sdk_state() {
    local sdk="$1" d dirs=()
    for d in base pluginterfaces public.sdk; do if [ -d "$sdk/$d" ]; then dirs+=("$d"); fi; done
    if [ "${#dirs[@]}" -eq 0 ]; then echo absent; return 0; fi
    if [ "${#dirs[@]}" -ne 3 ]; then echo partial; return 0; fi
    (cd "$sdk" && find "${dirs[@]}" \( -name .git -o -path public.sdk/samples \) -prune -o -type f ! -name .DS_Store -print0 \
        | LC_ALL=C sort -z | xargs -0 shasum -a 256 | shasum -a 256 | cut -d' ' -f1)
}

# sdk_extras <SDK folder>: what it holds besides iPlug2's tracked README.md (and .DS_Store): empty for a checkout whose
# SDK was never fetched.
sdk_extras() {
    local e out=""
    for e in "$1"/* "$1"/.[!.]* "$1"/..?*; do
        if [ ! -e "$e" ] && [ ! -L "$e" ]; then continue; fi
        case "$(basename "$e")" in README.md|.DS_Store) ;; *) out="$out $(basename "$e")" ;; esac
    done
    echo "${out# }"
}

default_dir() {  # IPLUG2_ROOT of the xcconfig, relative to ChordLab/projects, made absolute (it need not exist yet)
    local rel parent
    rel=$(sed -n '/^IPLUG2_ROOT *= */{s///p;q;}' "$ROOT/ChordLab/config/ChordLab-mac.xcconfig" || true)
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
case "$DIR" in /*) ;; *) DIR="$PWD/$DIR" ;; esac
DIR="${DIR%/}"
# A symlink to a folder (the worktrees' iPlug2 link): work on the folder it points to, leaving the link as it is.
if [ -L "$DIR" ] && [ -d "$DIR" ]; then DIR=$(cd -P "$DIR" && pwd -P); fi

URL=$(pin_value IPLUG2_URL)
COMMIT=$(pin_value IPLUG2_COMMIT)
TAG=$(pin_value VST3SDK_TAG)
SDK_SHA=$(pin_value VST3SDK_TREE_SHA256)

if [ "$MODE" = sdk_hash ]; then sdk_state "$DIR/$SDK_REL"; exit 0; fi

if command -v timeout >/dev/null 2>&1; then TO=(timeout 1200)
elif command -v gtimeout >/dev/null 2>&1; then TO=(gtimeout 1200)
else TO=(); fi

WORK=()   # temporary folders of this run (the download, a fresh clone before it is moved), removed on exit
cleanup() { local w; for w in ${WORK[@]+"${WORK[@]}"}; do rm -rf "$w"; done; }
trap cleanup EXIT

# fetch_sdk <iPlug2 folder>: runs iPlug2's download-vst3-sdk.sh in a scratch folder outside every git work tree
# (GIT_CEILING_DIRECTORIES) and checks the result's tree hash there. The download script is not fail-fast (after a failed
# clone it carries on with rm -rf in its current folder, here the scratch one) and its last step, git checkout README.md,
# only works inside iPlug2: its exit status says nothing, the hash decides. The verified SDK is then moved into
# <iPlug2 folder>/VST3_SDK entry by entry, never over anything: iPlug2's tracked README.md stays, a name that exists is
# refused, mv -n never replaces a file, and nothing in that folder (which other projects may share) is ever renamed or
# deleted. If something appears there meanwhile, the caller's hash check fails and says so; nothing is lost.
fetch_sdk() {
    local ip="$1" target sb e name extras got
    target="$ip/$SDK_REL"
    extras=$(sdk_extras "$target")
    [ -z "$extras" ] || die "$target holds an incomplete SDK ($extras); left as it is: remove it, or run download-vst3-sdk.sh $TAG there yourself"
    sb=$(mktemp -d "${TMPDIR:-/tmp}/vst3sdk-fetch.XXXXXX")
    sb=$(cd -P "$sb" && pwd -P)
    WORK+=("$sb")
    note "fetching the VST3 SDK $TAG with iPlug2's Dependencies/IPlug/download-vst3-sdk.sh (in $sb)"
    (cd "$sb" && GIT_CEILING_DIRECTORIES="$sb" ${TO[@]+"${TO[@]}"} "$ip/Dependencies/IPlug/download-vst3-sdk.sh" "$TAG") \
        >"$sb/download.log" 2>&1 || true
    [ -d "$sb/VST3_SDK" ] || { tail -n 15 "$sb/download.log" >&2; die "download-vst3-sdk.sh $TAG fetched nothing"; }
    got=$(sdk_state "$sb/VST3_SDK")
    [ "$got" = "$SDK_SHA" ] || { tail -n 15 "$sb/download.log" >&2; die "the VST3 SDK $TAG as downloaded hashes to $got, the pin says $SDK_SHA"; }
    mkdir -p "$target"
    for e in "$sb/VST3_SDK"/* "$sb/VST3_SDK"/.[!.]* "$sb/VST3_SDK"/..?*; do
        if [ ! -e "$e" ] && [ ! -L "$e" ]; then continue; fi
        name=$(basename "$e")
        [ "$name" != README.md ] || continue
        if [ -e "$target/$name" ] || [ -L "$target/$name" ]; then
            die "$target/$name appeared while the SDK was downloading; nothing there was changed, but the SDK is incomplete: check $target"
        fi
        mv -n "$e" "$target/$name"
    done
}

check_tree() {  # <iPlug2 folder>: a git work tree of its own at the pinned commit with no changed tracked file
    local d="$1" head changed
    git -C "$d" rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "$d is not a git work tree"
    [ "$(git -C "$d" rev-parse --show-toplevel)" = "$(cd "$d" && pwd -P)" ] || die "$d is inside another git work tree, not an iPlug2 checkout"
    head=$(git -C "$d" rev-parse HEAD)
    [ "$head" = "$COMMIT" ] || die "$d is at ${head:0:9}, the pin says ${COMMIT:0:9}: check it out (git -C '$d' checkout --detach $COMMIT) or move the pin (scripts/iplug2.pin)"
    changed=$(git -C "$d" status --porcelain --untracked-files=no)
    [ -z "$changed" ] || die "$d has changed tracked files: $(head -n 5 <<<"$changed" | tr '\n' ' ')"
}

empty_dir() { [ -d "$1" ] && [ -z "$(ls -A "$1")" ]; }

if [ ! -e "$DIR" ] || empty_dir "$DIR"; then
    [ "$MODE" = fetch ] || die "no iPlug2 at $DIR (run scripts/fetch-iplug2.sh $DIR to fetch the pinned one)"
    mkdir -p "$(dirname "$DIR")"
    tmp=$(mktemp -d "$(dirname "$DIR")/.iPlug2-fetch.XXXXXX")
    WORK+=("$tmp")
    note "cloning $URL (blobless) at ${COMMIT:0:9} into $tmp"
    ${TO[@]+"${TO[@]}"} git clone --quiet --filter=blob:none --no-checkout "$URL" "$tmp/iPlug2"
    # The pin may name a commit no branch carries any more: fetch it by id when the clone lacks it.
    git -C "$tmp/iPlug2" cat-file -e "$COMMIT^{commit}" 2>/dev/null || ${TO[@]+"${TO[@]}"} git -C "$tmp/iPlug2" fetch --quiet origin "$COMMIT"
    # A blobless checkout downloads the commit's files now: under the timeout too.
    ${TO[@]+"${TO[@]}"} git -C "$tmp/iPlug2" -c advice.detachedHead=false checkout --quiet --detach "$COMMIT"
    check_tree "$tmp/iPlug2"
    fetch_sdk "$tmp/iPlug2"
    if [ -d "$DIR" ]; then rmdir "$DIR"; fi
    mv "$tmp/iPlug2" "$DIR"
    note "fetched into $DIR"
fi

[ -d "$DIR" ] || die "$DIR is not a directory"
check_tree "$DIR"
state=$(sdk_state "$DIR/$SDK_REL")
case "$state" in
    absent)
        [ "$MODE" = fetch ] || die "no VST3 SDK in $DIR/$SDK_REL (run scripts/fetch-iplug2.sh to fetch it)"
        fetch_sdk "$DIR"
        check_tree "$DIR"
        state=$(sdk_state "$DIR/$SDK_REL") ;;
    partial)
        die "the VST3 SDK in $DIR/$SDK_REL is incomplete (base, pluginterfaces or public.sdk missing); left as it is: remove it and rerun, or run download-vst3-sdk.sh $TAG there yourself" ;;
esac
[ "$state" = "$SDK_SHA" ] || die "the VST3 SDK in $DIR hashes to $state, the pin says $SDK_SHA ($TAG): refetch it ((cd '$DIR/Dependencies/IPlug' && ./download-vst3-sdk.sh $TAG)) or move the pin"

note "iPlug2 $DIR at ${COMMIT:0:9} with VST3 SDK $TAG (tree ${SDK_SHA:0:12}): matches scripts/iplug2.pin"
