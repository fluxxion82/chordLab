#!/usr/bin/env bash
# ChordLab release: from a clean, tagged checkout to a notarized pkg (design: docs/plans/2026-11-06-release-design.md).
#
#   scripts/release.sh preflight [--report] [--rehearsal] [--skip-notarize]
#       Checks only: tools, Developer ID identities, notary profile, iPlug2 checkout, versions, clean trees, tag.
#       --report prints every check and exits 0 unless the repository itself is wrong (a contributor's gate).
#   scripts/release.sh build [--rehearsal] [--skip-notarize] [--skip-auval]
#       Release build of every format into dist/stage, signed with the Developer ID given on the xcodebuild
#       command line; signature checks; auval on the AU; three component pkgs; one signed product pkg,
#       notarized and stapled; .sha256 and .provenance.txt beside it.
#       --rehearsal allows an untagged HEAD and dirty trees and names the pkg with the commit (never publishable);
#       --skip-notarize leaves it unstapled and named -unnotarized; --skip-auval records the AU as unvalidated.
#   scripts/release.sh publish
#       gh release create v<version> with the pkg, its hash and the provenance, after checking that they belong
#       to HEAD, which must be the tag, already pushed.
#
# The Xcode project stays ad-hoc signed; nothing here changes it. The only write outside dist/ is the AU copy
# that auval validates (~/Library/Audio/Plug-Ins/Components/ChordLab.component, see au_validate).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$ROOT"

PROJECT=ChordLab/projects/ChordLab-macOS.xcodeproj
CONFIG_H=ChordLab/config.h
TEAM=GPX68LJ584
APP_IDENTITY="Developer ID Application: Sterling Albury ($TEAM)"
PKG_IDENTITY="Developer ID Installer: Sterling Albury ($TEAM)"
NOTARY_PROFILE=chordlab
DIST="$ROOT/dist"
STAGE="$DIST/stage"
PKGS="$DIST/pkgs"
LOGS="$DIST/log"
FORMATS=(vst3 app)                       # Task 10 adds au
# IPLUG2_ROOT in ChordLab-mac.xcconfig is relative to ChordLab/projects (../../../iPlug2).
IPLUG2_ROOT="$( (cd ChordLab/projects && cd "$(sed -n 's/^IPLUG2_ROOT *= *//p' ../config/ChordLab-mac.xcconfig)" && pwd -P) 2>/dev/null || true)"

if command -v timeout >/dev/null 2>&1; then TIMEOUT_CMD=timeout
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT_CMD=gtimeout
else echo "release.sh: neither timeout nor gtimeout found; install coreutils" >&2; exit 1; fi

die()  { echo "release.sh: $*" >&2; exit 1; }
note() { echo "release.sh: $*"; }

version_str() { sed -n 's/^#define PLUG_VERSION_STR "\(.*\)"/\1/p' "$CONFIG_H"; }
version_hex() { sed -n 's/^#define PLUG_VERSION_HEX 0x\([0-9A-Fa-f]*\).*/\1/p' "$CONFIG_H"; }
hex_to_str()  { local h=$((16#$1)); echo "$(( (h >> 16) & 0xFFFF )).$(( (h >> 8) & 0xFF )).$(( h & 0xFF ))"; }
au_subtype()  { sed -n "s/^#define PLUG_UNIQUE_ID '\(.*\)'.*/\1/p" "$CONFIG_H"; }
au_mfr()      { sed -n "s/^#define PLUG_MFR_ID '\(.*\)'.*/\1/p" "$CONFIG_H"; }
head_tag()    { git describe --exact-match --tags HEAD 2>/dev/null || true; }
tree_dirty()  { [ -n "$(git -C "${1:-.}" status --porcelain 2>/dev/null)" ]; }

scheme_of() { case "$1" in vst3) echo macOS-VST3 ;; au) echo macOS-AUv2 ;; app) echo macOS-APP ;; *) die "unknown format $1" ;; esac; }
bundle_of() { case "$1" in vst3) echo ChordLab.vst3 ;; au) echo ChordLab.component ;; app) echo ChordLab.app ;; esac; }
pkgid_of()  { case "$1" in vst3) echo com.SterlingAlbury.chordlab.vst3 ;; au) echo com.SterlingAlbury.chordlab.au ;; app) echo com.SterlingAlbury.chordlab.app ;; esac; }
dest_of()   { case "$1" in vst3) echo /Library/Audio/Plug-Ins/VST3 ;; au) echo /Library/Audio/Plug-Ins/Components ;; app) echo /Applications ;; esac; }
title_of()  { case "$1" in vst3) echo "VST3 plug-in (Ableton Live and other VST3 hosts)" ;; au) echo "Audio Unit MIDI FX (Logic Pro)" ;; app) echo "ChordLab app (standalone)" ;; esac; }

REPORT=0 REHEARSAL=0 SKIP_NOTARIZE=0 SKIP_AUVAL=0
PRE_FAILED=0
VER="" PKG="" PROV="" AUVAL_RESULT="n/a"

# ---------------------------------------------------------------------------------------------------------
# preflight

probe() { "$@" >/dev/null 2>&1 && echo 0 || echo 1; }   # a command's success as 0/1, safe under set -e
row()   { printf '  %-8s %-26s %s\n' "$1" "$2" "$3"; }
check() {  # name ok(0|1) detail [hard]: under --report only a hard check fails (the repository itself is wrong)
    local name=$1 ok=$2 detail=$3 hard=${4:-0}
    if [ "$ok" -eq 0 ]; then row ok "$name" "$detail"; return; fi
    row MISSING "$name" "$detail"
    if [ "$REPORT" -eq 0 ] || [ "$hard" -eq 1 ]; then PRE_FAILED=1; fi
}

has_identity()      { security find-identity -v -p "$1" 2>/dev/null | grep -qF "\"$2\""; }
versions_agree()    { local hex; hex=$(version_hex); [ -n "$hex" ] && [ "$(hex_to_str "$hex")" = "$(version_str)" ]; }
notary_profile_ok() { "$TIMEOUT_CMD" 30 xcrun notarytool history --keychain-profile "$NOTARY_PROFILE"; }

preflight() {
    local ver tool tag
    ver=$(version_str)
    echo "release.sh preflight (ChordLab $ver)"
    for tool in xcodebuild codesign pkgbuild productbuild pkgutil auval lipo shasum python3 gh ditto; do
        check "tool $tool" "$(probe command -v "$tool")" "$(command -v "$tool" 2>/dev/null || echo "not on PATH (a release machine needs it)")"
    done
    check "tool notarytool" "$(probe xcrun --find notarytool)" "$(xcrun --find notarytool 2>/dev/null || true)"
    check "tool stapler" "$(probe xcrun --find stapler)" "$(xcrun --find stapler 2>/dev/null || true)"
    check "versions agree" "$(probe versions_agree)" "PLUG_VERSION_STR $ver, PLUG_VERSION_HEX 0x$(version_hex)" 1
    check "iPlug2 checkout" "$(probe test -f "$IPLUG2_ROOT/common-mac.xcconfig")" "${IPLUG2_ROOT:-unresolved: ../../../iPlug2 from ChordLab/projects}" 1
    check "VST3 SDK" "$(probe test -d "$IPLUG2_ROOT/Dependencies/IPlug/VST3_SDK/pluginterfaces")" "$IPLUG2_ROOT/Dependencies/IPlug/VST3_SDK" 1
    check "Developer ID Application" "$(probe has_identity codesigning "$APP_IDENTITY")" "$APP_IDENTITY"
    check "Developer ID Installer" "$(probe has_identity basic "$PKG_IDENTITY")" "$PKG_IDENTITY"
    if [ "$SKIP_NOTARIZE" -eq 1 ]; then
        row skip "notary profile" "--skip-notarize"
    else
        check "notary profile" "$(probe notary_profile_ok)" "keychain profile '$NOTARY_PROFILE' (once: xcrun notarytool store-credentials $NOTARY_PROFILE --apple-id <id> --team-id $TEAM --password <app-specific password>)"
    fi
    if [ "$REHEARSAL" -eq 1 ]; then
        row skip "clean tree" "--rehearsal ($(git status --porcelain | wc -l | tr -d ' ') changed paths)"
        row skip "iPlug2 tree clean" "--rehearsal"
        tag=$(head_tag)
        row skip "HEAD tagged v$ver" "--rehearsal: ${tag:-no tag}; the pkg name carries $(git rev-parse --short HEAD)"
    else
        check "clean tree" "$(probe test -z "$(git status --porcelain)")" "$(git status --porcelain | wc -l | tr -d ' ') changed paths"
        check "iPlug2 tree clean" "$(probe test -z "$(git -C "$IPLUG2_ROOT" status --porcelain 2>/dev/null)")" "the provenance file must name a commit that is the code ($IPLUG2_ROOT)"
        tag=$(head_tag)
        check "HEAD tagged v$ver" "$(probe test "$tag" = "v$ver")" "${tag:-no tag on HEAD} (--rehearsal for a rehearsal)"
    fi
    check "gh logged in" "$(probe gh auth status)" "$(gh auth status 2>&1 | sed -n 's/.*Logged in to \(.*\)/\1/p' | head -1)"
    if [ "$PRE_FAILED" -ne 0 ]; then
        [ "$REPORT" -eq 1 ] && die "preflight: the repository itself is wrong (see MISSING above)"
        die "preflight failed (see MISSING above)"
    fi
    note "preflight ok"
}

# ---------------------------------------------------------------------------------------------------------
# build

build_format() {
    local fmt=$1 scheme bundle out log
    scheme=$(scheme_of "$fmt"); bundle=$(bundle_of "$fmt"); out="$STAGE/$fmt"; log="$LOGS/xcodebuild-$fmt.log"
    rm -rf "$out"; mkdir -p "$out"
    note "building $scheme (Release, universal, Developer ID) into $out (log: $log)"
    # The project already has DEPLOYMENT_LOCATION=YES, INSTALL_PATH=/ and SKIP_INSTALL=NO: DSTROOT alone moves
    # the install from ~/Library ($(VST3_PATH), $(AU_PATH), $(APP_PATH)) to the stage.
    # CODE_SIGN_INJECT_BASE_ENTITLEMENTS=NO: Xcode otherwise adds get-task-allow to the app, which notarization refuses.
    if ! xcodebuild -project "$PROJECT" -scheme "$scheme" -configuration Release \
            DSTROOT="$out" \
            CODE_SIGN_STYLE=Manual CODE_SIGN_IDENTITY="$APP_IDENTITY" DEVELOPMENT_TEAM="$TEAM" \
            PROVISIONING_PROFILE_SPECIFIER= OTHER_CODE_SIGN_FLAGS=--timestamp ENABLE_HARDENED_RUNTIME=YES \
            CODE_SIGN_INJECT_BASE_ENTITLEMENTS=NO \
            build >"$log" 2>&1; then
        tail -n 30 "$log" >&2
        die "xcodebuild $scheme failed (see $log)"
    fi
    grep -q "BUILD SUCCEEDED" "$log" || die "xcodebuild $scheme: no BUILD SUCCEEDED in $log"
    [ -d "$out/$bundle" ] || die "$bundle is not in $out (DSTROOT ignored? see $log)"
}

verify_bundle() {
    local b=$1 exe info archs ents
    exe="$b/Contents/MacOS/ChordLab"
    [ -x "$exe" ] || die "$b: no executable at Contents/MacOS/ChordLab"
    codesign --verify --deep --strict --verbose=2 "$b" >>"$LOGS/codesign.log" 2>&1 || die "$b: codesign --verify failed (see $LOGS/codesign.log)"
    info=$(codesign -dvv "$b" 2>&1)
    grep -q '^Timestamp=' <<<"$info"              || die "$b: no secure timestamp"
    grep -q 'flags=0x10000(runtime)' <<<"$info"   || die "$b: hardened runtime is off, or the signature is ad-hoc"
    grep -qF "Authority=$APP_IDENTITY" <<<"$info" || die "$b: not signed by $APP_IDENTITY"
    archs=$(lipo -archs "$exe")
    case "$archs" in *arm64*x86_64*|*x86_64*arm64*) ;; *) die "$b: not universal ($archs)" ;; esac
    [ -f "$b/Contents/Resources/LICENSE" ] && [ -f "$b/Contents/Resources/THIRD_PARTY_NOTICES.txt" ] || die "$b: LICENSE or THIRD_PARTY_NOTICES.txt missing from Contents/Resources"
    # Notarization refuses get-task-allow; captured first so an early grep exit cannot fail the pipeline.
    ents=$(codesign -d --entitlements - "$b" 2>/dev/null || true)
    if grep -q 'get-task-allow' <<<"$ents"; then die "$b: get-task-allow entitlement present (notarization would refuse it)"; fi
    note "verified $(basename "$b"): Developer ID, hardened runtime, timestamp, $archs, notices, no get-task-allow"
}

build() {
    VER=$(version_str)
    REPORT=0; preflight
    rm -rf "$DIST"; mkdir -p "$STAGE" "$PKGS" "$LOGS"
    local fmt
    for fmt in "${FORMATS[@]}"; do build_format "$fmt"; done
    for fmt in "${FORMATS[@]}"; do verify_bundle "$STAGE/$fmt/$(bundle_of "$fmt")"; done
    # Task 10: au_validate
    # Task 4: package; notarize; provenance
    note "build done: $STAGE"
}

# ---------------------------------------------------------------------------------------------------------

usage() { sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 2; }

[ $# -ge 1 ] || usage
cmd=$1; shift
for arg in "$@"; do
    case "$arg" in
        --report) REPORT=1 ;;
        --rehearsal) REHEARSAL=1 ;;
        --skip-notarize) SKIP_NOTARIZE=1 ;;
        --skip-auval) SKIP_AUVAL=1 ;;
        *) echo "release.sh: unknown option '$arg'" >&2; usage ;;
    esac
done
case "$cmd" in
    preflight) preflight ;;
    build) build ;;
    publish) die "publish: not implemented yet (Task 4)" ;;
    *) usage ;;
esac
