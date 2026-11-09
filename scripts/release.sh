#!/usr/bin/env bash
# ChordLab release: from a clean, tagged checkout to a notarized pkg (design: docs/plans/2026-11-06-release-design.md).
#
#   scripts/release.sh preflight [--report] [--rehearsal] [--skip-notarize]
#       Checks only: tools, Developer ID identities, notary profile, iPlug2 checkout, versions, the six tracked
#       Info.plists at that version, no ChordLab AU in /Library (auval would validate it instead), clean trees, tag.
#       --report prints every check and exits 0 unless the repository itself is wrong (a contributor's gate).
#   scripts/release.sh build [--rehearsal] [--skip-notarize] [--skip-auval]
#       Release build of every format into dist/stage, signed with the Developer ID given on the xcodebuild
#       command line; signature checks; auval on the AU; three component pkgs; one signed product pkg,
#       notarized (notarytool --wait --timeout 2h) and stapled; .sha256 and .provenance.txt beside it.
#       --rehearsal allows an untagged HEAD and dirty trees and names the pkg with the commit (never publishable);
#       --skip-notarize leaves it unstapled and named -unnotarized; --skip-auval records the AU as unvalidated.
#   scripts/release.sh publish
#       gh release create v<version> --draft with the pkg, its hash and the provenance, then --draft=false once
#       all three assets are listed, after checking that they belong to HEAD, which must carry the annotated
#       tag, already pushed.
#
# The Xcode project stays ad-hoc signed; nothing here changes it. The script writes dist/ (stage, pkgs, logs and
# xcodebuild's intermediates: SYMROOT and OBJROOT point into it, so every build is cold) and the AU
# copy that auval validates (~/Library/Audio/Plug-Ins/Components/ChordLab.component, see au_validate); nothing
# else (Xcode's own shared module cache and logs under ~/Library/Developer aside; and the AU target's
# clear_audiounit_caches.command build phase, which deletes the AU caches under ~/Library/Caches/AudioUnitCache
# and AU Lab's component cache and kills AudioComponentRegistrar).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
SELF="$(cd "$(dirname "$0")" && pwd -P)/$(basename "$0")"   # for usage after the cd
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
FORMATS=(vst3 au app)
# auval names no component path, so a /Library copy (an earlier pkg install) could be validated instead of this build.
LIBRARY_AU=/Library/Audio/Plug-Ins/Components/ChordLab.component
LIBRARY_AU_FIX="move it aside (sudo mv it to ~/chordlab-dev-aside/) so auval validates this build, then rerun"
# IPLUG2_ROOT in ChordLab-mac.xcconfig is relative to ChordLab/projects (../../../iPlug2).
IPLUG2_ROOT="$( (cd ChordLab/projects && rel="$(sed -n 's/^IPLUG2_ROOT *= *//p' ../config/ChordLab-mac.xcconfig)" \
    && [ -n "$rel" ] && cd "$rel" && pwd -P) 2>/dev/null || true)"

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
head_tag()    { git describe --exact-match --tags HEAD 2>/dev/null || true; }   # informational rows only
annotated_tag() {  # HEAD carries v$1: 0 an annotated tag, 1 not on HEAD, 2 a lightweight tag
    local tags; tags=$(git tag --points-at HEAD 2>/dev/null || true)   # captured: an early grep exit cannot fail a pipe
    grep -qxF "v$1" <<<"$tags" || return 1                              # -F: the dots in a version are not wildcards
    [ "$(git cat-file -t "refs/tags/v$1" 2>/dev/null || true)" = tag ] || return 2   # refs/tags/: never a branch v$1
}
work_tree()   { git -C "${1:-.}" rev-parse --is-inside-work-tree >/dev/null 2>&1; }
tree_dirty()  {  # 0 dirty, 1 clean, 2 not a git work tree (never "clean": callers test ! tree_clean)
    work_tree "${1:-.}" || return 2
    [ -n "$(git -C "${1:-.}" status --porcelain 2>/dev/null)" ]
}
tree_clean()  { local rc=0; tree_dirty "$1" || rc=$?; [ "$rc" -eq 1 ]; }
tree_excerpt() {  # the first changed paths of both trees, for a die message
    local s i
    s=$(git status --porcelain 2>/dev/null || true)
    i=$(git -C "$IPLUG2_ROOT" status --porcelain 2>/dev/null) || i="not a git work tree"
    printf '\n  ChordLab: %s\n  iPlug2:   %s' "$(head -n 10 <<<"${s:-(clean)}" | tr '\n' ' ')" "$(head -n 10 <<<"${i:-(clean)}" | tr '\n' ' ')"
}

scheme_of() { case "$1" in vst3) echo macOS-VST3 ;; au) echo macOS-AUv2 ;; app) echo macOS-APP ;; *) die "unknown format $1" ;; esac; }
bundle_of() { case "$1" in vst3) echo ChordLab.vst3 ;; au) echo ChordLab.component ;; app) echo ChordLab.app ;; esac; }
pkgid_of()  { case "$1" in vst3) echo com.SterlingAlbury.chordlab.vst3 ;; au) echo com.SterlingAlbury.chordlab.au ;; app) echo com.SterlingAlbury.chordlab.app ;; esac; }
dest_of()   { case "$1" in vst3) echo /Library/Audio/Plug-Ins/VST3 ;; au) echo /Library/Audio/Plug-Ins/Components ;; app) echo /Applications ;; esac; }
title_of()  { case "$1" in vst3) echo "VST3 plug-in (Ableton Live and other VST3 hosts)" ;; au) echo "Audio Unit MIDI FX (Logic Pro)" ;; app) echo "ChordLab app (standalone)" ;; esac; }

REPORT=0 REHEARSAL=0 SKIP_NOTARIZE=0 SKIP_AUVAL=0
PRE_FAILED=0
VER="" PKG="" PROV="" AUVAL_RESULT="n/a" BUILD_HEAD="" BUILD_IPLUG2_HEAD=""

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

has_identity()      { local out; out=$(security find-identity -v -p "$1" 2>/dev/null) || return 1; grep -qF "\"$2\"" <<<"$out"; }
versions_agree()    { local hex; hex=$(version_hex); [ -n "$hex" ] && [ "$(hex_to_str "$hex")" = "$(version_str)" ]; }
notary_profile_ok() { "$TIMEOUT_CMD" 30 xcrun notarytool history --keychain-profile "$NOTARY_PROFILE"; }
plists_current() {  # prints what is stale; 0 when the tracked plists carry PLUG_VERSION_STR and PLUG_VERSION_HEX
    # prepare_resources-mac.py rewrites these six on every build, so a version bump left uncommitted in any of
    # them would dirty the tree mid-build (and only the final guard, after notarization, would notice).
    python3 -I - "$(version_str)" "$(version_hex)" ChordLab/resources/ChordLab-{VST3,VST2,AU,macOS-AUv3,AAX,macOS}-Info.plist <<'PY'
import plistlib, sys
ver, hexv, paths = sys.argv[1], sys.argv[2], sys.argv[3:]
try:
    want_int = int(hexv, 16)
except ValueError:
    want_int = None
want_hex = '0x' + hexv  # prepare_resources writes PLUG_VERSION_HEX as config.h spells it
stale = []
for path in paths:
    name = path.rsplit('/', 1)[-1]
    try:
        with open(path, 'rb') as fp:
            plist = plistlib.load(fp)
        for key in ('CFBundleVersion', 'CFBundleShortVersionString'):
            if plist.get(key) != ver:
                stale.append('%s %s %s' % (name, key, plist.get(key)))
        if name.endswith('-AU-Info.plist'):
            if plist.get('AudioUnit Version') != want_hex:
                stale.append('%s AudioUnit Version %s, expected %s' % (name, plist.get('AudioUnit Version'), want_hex))
            got = plist['AudioComponents'][0]['version']
            if want_int is None or got != want_int:
                stale.append('%s AudioComponents[0].version %s, expected %s' % (name, got, want_int))
        if name.endswith('-macOS-AUv3-Info.plist'):
            got = plist['NSExtension']['NSExtensionAttributes']['AudioComponents'][0]['version']
            if want_int is None or got != want_int:
                stale.append('%s NSExtension AudioComponents[0].version %s, expected %s' % (name, got, want_int))
    except Exception as e:
        stale.append('%s: %s' % (name, ' '.join(('%s: %s' % (type(e).__name__, e)).split())))
print('; '.join(stale))
sys.exit(1 if stale else 0)
PY
}

preflight() {
    local ver tool tag tag_rc stale plists_rc
    ver=$(version_str)
    echo "release.sh preflight (ChordLab $ver)"
    for tool in xcodebuild codesign pkgbuild productbuild pkgutil auval lipo shasum python3 gh ditto; do
        check "tool $tool" "$(probe command -v "$tool")" "$(command -v "$tool" 2>/dev/null || echo "not on PATH (a release machine needs it)")"
    done
    check "tool notarytool" "$(probe xcrun --find notarytool)" "$(xcrun --find notarytool 2>/dev/null || true)"
    check "tool stapler" "$(probe xcrun --find stapler)" "$(xcrun --find stapler 2>/dev/null || true)"
    check "versions agree" "$(probe versions_agree)" "PLUG_VERSION_STR $ver, PLUG_VERSION_HEX 0x$(version_hex)" 1
    if ! command -v python3 >/dev/null 2>&1; then  # the tool row above says so; a release (strict) needs it anyway
        if [ "$REPORT" -eq 1 ]; then row skip "Info.plists current" "skipped: python3 missing"
        else check "Info.plists current" 1 "skipped: python3 missing (it reads the plists)"; fi
    else
        stale=$(plists_current 2>&1) && plists_rc=0 || plists_rc=1
        if [ "$plists_rc" -eq 0 ]; then
            check "Info.plists current" 0 "VST3, VST2, AU, macOS-AUv3, AAX, macOS plists: CFBundleVersion and CFBundleShortVersionString $ver; AU 'AudioUnit Version' 0x$(version_hex), AU and AUv3 component version $((16#$(version_hex)))" 1
        else
            check "Info.plists current" 1 "${stale:-stale}: build once (scripts/check.sh --plugin) and commit the regenerated plists with the bump" 1
        fi
    fi
    check "iPlug2 checkout" "$(probe test -f "$IPLUG2_ROOT/common-mac.xcconfig")" "${IPLUG2_ROOT:-unresolved: ../../../iPlug2 from ChordLab/projects}" 1
    check "VST3 SDK" "$(probe test -d "$IPLUG2_ROOT/Dependencies/IPlug/VST3_SDK/pluginterfaces")" "$IPLUG2_ROOT/Dependencies/IPlug/VST3_SDK" 1
    check "Developer ID Application" "$(probe has_identity codesigning "$APP_IDENTITY")" "$APP_IDENTITY"
    check "Developer ID Installer" "$(probe has_identity basic "$PKG_IDENTITY")" "$PKG_IDENTITY"
    if [ "$SKIP_AUVAL" -eq 1 ]; then
        row skip "no /Library AU" "--skip-auval"
    elif [ -e "$LIBRARY_AU" ]; then
        check "no /Library AU" 1 "$LIBRARY_AU exists: $LIBRARY_AU_FIX"
    else
        check "no /Library AU" 0 "$LIBRARY_AU absent (auval sees only the staged build)"
    fi
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
        check "iPlug2 tree clean" "$(probe tree_clean "$IPLUG2_ROOT")" "the provenance file must name a commit that is the code ($IPLUG2_ROOT, a git work tree)"
        tag_rc=0; annotated_tag "$ver" || tag_rc=$?
        case "$tag_rc" in
            0) check "HEAD tagged v$ver" 0 "annotated tag v$ver" ;;
            2) check "HEAD tagged v$ver" 1 "v$ver is a lightweight tag; the release notes come from an annotated one (git tag -a)" ;;
            *) tag=$(git tag --points-at HEAD 2>/dev/null | tr '\n' ' ' | sed 's/ $//' || true)
               check "HEAD tagged v$ver" 1 "${tag:-no tag on HEAD} (--rehearsal for a rehearsal)" ;;
        esac
    fi
    check "gh logged in" "$(probe "$TIMEOUT_CMD" 30 gh auth status)" "$("$TIMEOUT_CMD" 30 gh auth status 2>&1 | sed -n 's/.*Logged in to \(.*\)/\1/p' | head -1)"
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
    # SYMROOT/OBJROOT: the project pins SYMROOT to ChordLab/build-mac (the developer's intermediates); building into
    # dist/build instead makes every release build cold (build() wipes dist/), so the pkg is this commit's code.
    if ! xcodebuild -project "$PROJECT" -scheme "$scheme" -configuration Release \
            DSTROOT="$out" SYMROOT="$DIST/build" OBJROOT="$DIST/build/obj" \
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
    grep -Eq 'flags=0x[0-9a-f]+\([^)]*runtime' <<<"$info" || die "$b: hardened runtime is off"
    ! grep -q 'adhoc' <<<"$info"                  || die "$b: the signature is ad-hoc"
    grep -qF "Authority=$APP_IDENTITY" <<<"$info" || die "$b: not signed by $APP_IDENTITY"
    archs=$(lipo -archs "$exe")
    case "$archs" in *arm64*x86_64*|*x86_64*arm64*) ;; *) die "$b: not universal ($archs)" ;; esac
    [ -f "$b/Contents/Resources/LICENSE" ] && [ -f "$b/Contents/Resources/THIRD_PARTY_NOTICES.txt" ] || die "$b: LICENSE or THIRD_PARTY_NOTICES.txt missing from Contents/Resources"
    # Notarization refuses get-task-allow; captured first so an early grep exit cannot fail the pipeline.
    ents=$(codesign -d --entitlements - "$b" 2>/dev/null || true)
    if grep -q 'get-task-allow' <<<"$ents"; then die "$b: get-task-allow entitlement present (notarization would refuse it)"; fi
    note "verified $(basename "$b"): Developer ID, hardened runtime, timestamp, $archs, notices, no get-task-allow"
}

component_pkg() {
    local fmt=$1 root plist out
    root="$STAGE/$fmt"; plist="$PKGS/$fmt-component.plist"; out="$PKGS/ChordLab-$fmt.pkg"
    pkgbuild --analyze --root "$root" "$plist" >>"$LOGS/pkgbuild.log" 2>&1 || die "pkgbuild --analyze $fmt failed (see $LOGS/pkgbuild.log)"
    # Never "update" a stray copy elsewhere, never skip an install over a newer build (design D16).
    python3 - "$plist" <<'PY' || die "could not edit $plist"
import plistlib, sys
path = sys.argv[1]
with open(path, 'rb') as fp:
    items = plistlib.load(fp)
for item in items:
    item['BundleIsRelocatable'] = False
    item['BundleIsVersionChecked'] = False
with open(path, 'wb') as fp:
    plistlib.dump(items, fp)
PY
    pkgbuild --root "$root" --component-plist "$plist" --identifier "$(pkgid_of "$fmt")" --version "$VER" \
        --install-location "$(dest_of "$fmt")" "$out" >>"$LOGS/pkgbuild.log" 2>&1 || die "pkgbuild $fmt failed (see $LOGS/pkgbuild.log)"
    note "component package $(basename "$out") for $(dest_of "$fmt")"
}

write_distribution() {
    local xml=$1 fmt
    {
        echo '<?xml version="1.0" encoding="utf-8"?>'
        echo '<installer-gui-script minSpecVersion="2">'
        echo "    <title>ChordLab $VER</title>"
        echo '    <welcome file="welcome.txt" mime-type="text/plain"/>'
        echo '    <readme file="THIRD_PARTY_NOTICES.txt" mime-type="text/plain"/>'
        echo '    <license file="LICENSE" mime-type="text/plain"/>'
        echo '    <conclusion file="conclusion.txt" mime-type="text/plain"/>'
        echo '    <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>'
        echo '    <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>'
        echo '    <volume-check>'
        echo '        <allowed-os-versions>'
        echo '            <os-version min="15.0"/>'
        echo '        </allowed-os-versions>'
        echo '    </volume-check>'
        echo '    <choices-outline>'
        for fmt in "${FORMATS[@]}"; do echo "        <line choice=\"$fmt\"/>"; done
        echo '    </choices-outline>'
        for fmt in "${FORMATS[@]}"; do
            echo "    <choice id=\"$fmt\" title=\"$(title_of "$fmt")\" description=\"Installs to $(dest_of "$fmt").\">"
            echo "        <pkg-ref id=\"$(pkgid_of "$fmt")\"/>"
            echo '    </choice>'
        done
        for fmt in "${FORMATS[@]}"; do
            echo "    <pkg-ref id=\"$(pkgid_of "$fmt")\" version=\"$VER\">ChordLab-$fmt.pkg</pkg-ref>"
        done
        echo '</installer-gui-script>'
    } >"$xml"
}

package() {
    local fmt resources suffix="" sig
    for fmt in "${FORMATS[@]}"; do component_pkg "$fmt"; done
    resources="$DIST/resources"; rm -rf "$resources"; mkdir -p "$resources"
    cp scripts/release/resources/welcome.txt scripts/release/resources/conclusion.txt LICENSE THIRD_PARTY_NOTICES.txt "$resources/"
    write_distribution "$DIST/Distribution.xml"
    if [ "$REHEARSAL" -eq 1 ]; then
        suffix="-$(git rev-parse --short HEAD)"
        if ! tree_clean . || ! tree_clean "$IPLUG2_ROOT"; then suffix="$suffix-dirty"; fi
    fi
    [ "$SKIP_NOTARIZE" -eq 0 ] || suffix="$suffix-unnotarized"
    PKG="$DIST/ChordLab-$VER$suffix.pkg"
    productbuild --distribution "$DIST/Distribution.xml" --package-path "$PKGS" --resources "$resources" \
        --sign "$PKG_IDENTITY" --timestamp "$PKG" >>"$LOGS/productbuild.log" 2>&1 || { tail -n 20 "$LOGS/productbuild.log" >&2; die "productbuild failed"; }
    # Captured first so an early grep exit cannot fail the pipeline.
    sig=$(pkgutil --check-signature "$PKG" 2>&1) || die "$PKG: pkgutil --check-signature failed"
    grep -qF "Status: signed by a developer certificate issued by Apple for distribution" <<<"$sig" \
        || die "$PKG: pkgutil does not report a Developer ID distribution signature (see pkgutil --check-signature)"
    grep -qF "Signed with a trusted timestamp" <<<"$sig" || die "$PKG: the installer signature has no trusted timestamp"
    grep -qF "$PKG_IDENTITY" <<<"$sig" || die "$PKG: not signed with $PKG_IDENTITY"
    note "product package $(basename "$PKG")"
}

notarize() {
    local submit rc=0 id status py_rc=0
    if [ "$SKIP_NOTARIZE" -eq 1 ]; then note "notarization skipped (--skip-notarize): $(basename "$PKG") is not stapled"; return; fi
    note "notarizing $(basename "$PKG") with profile $NOTARY_PROFILE (usually a few minutes; gives up waiting after 2 h)"
    # Never die before the submission id and the raw answer are saved: a lost id is a lost log.
    submit=$(xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait --timeout 2h \
             --output-format json 2>>"$LOGS/notarytool.err") || rc=$?
    printf '%s\n' "$submit" >"$LOGS/notarytool-submit.json"
    # notarytool may print the JSON on one line or pretty-printed, after progress lines: the whole answer first,
    # then the last line that parses, then everything from the first line that opens an object. Nothing printed
    # (and a diagnostic on stderr) when none parses, so the "no submission id" die below fires.
    read -r id status < <(python3 -I - "$LOGS/notarytool-submit.json" <<'PY'
import json, sys
raw = open(sys.argv[1], encoding="utf-8", errors="replace").read()
def obj(text):
    try:
        d = json.loads(text)
    except Exception:
        return None
    return d if isinstance(d, dict) else None
d = obj(raw)
if d is None:
    for line in raw.splitlines():
        x = obj(line)
        if x is not None:
            d = x
if d is None:
    lines = raw.splitlines()
    for i, line in enumerate(lines):
        if line.lstrip().startswith("{"):
            d = obj("\n".join(lines[i:]))
            break
if d is None:
    print("release.sh: no JSON object in the notarytool answer: %r" % raw[:200], file=sys.stderr)
    sys.exit(1)
print(d.get("id") or "-", str(d.get("status") or "-").replace(" ", "_"))
PY
) || true
    [ "${id:--}" != "-" ] || die "notarytool submit failed (rc $rc, no submission id; see $LOGS/notarytool.err and $LOGS/notarytool-submit.json; if it was submitted, find it with: xcrun notarytool history --keychain-profile $NOTARY_PROFILE)"
    note "notarization submission $id: status ${status:--} (notarytool rc $rc)"
    case "${status:--}" in
        Accepted|Invalid|Rejected) ;;
        *) die "notarization of submission $id did not finish (status ${status:--}, rc $rc); check later with: xcrun notarytool info $id --keychain-profile $NOTARY_PROFILE" ;;
    esac
    xcrun notarytool log "$id" --keychain-profile "$NOTARY_PROFILE" "$LOGS/notarytool-log.json" >>"$LOGS/notarytool.err" 2>&1 \
        || die "notarytool log $id failed (status $status; see $LOGS/notarytool.err)"
    # Every issue is printed; errors stop the run, warnings do not (design D18 as relaxed after review).
    python3 - "$LOGS/notarytool-log.json" <<'PY' || py_rc=$?
import json, sys
try:
    log = json.load(open(sys.argv[1]))
    issues = log.get("issues") or []
except Exception as e:
    print("  could not read the notarization log: %s" % e)
    sys.exit(2)
for issue in issues:
    print("  %s: %s (%s)" % (issue.get("severity"), issue.get("message"), issue.get("path")))
sys.exit(1 if any(str(issue.get("severity")).lower() == "error" for issue in issues) else 0)
PY
    [ "$py_rc" -ne 2 ] || die "the notarization log of submission $id is unreadable (status $status; see $LOGS/notarytool-log.json)"
    [ "$py_rc" -eq 0 ] || die "the notarization log of submission $id has errors (status $status; see $LOGS/notarytool-log.json)"
    [ "$status" = "Accepted" ] || die "notarization of submission $id: status $status (see $LOGS/notarytool-log.json)"
    # Accepted with no error-severity issue: a non-zero exit alone does not stop the run; the staple, stapler validate
    # and spctl below prove the outcome.
    [ "$rc" -eq 0 ] || note "WARNING: notarytool submit exited $rc although submission $id is Accepted with no error (see $LOGS/notarytool.err)"
    xcrun stapler staple "$PKG" >>"$LOGS/stapler.log" 2>&1 || die "stapler staple failed (see $LOGS/stapler.log)"
    xcrun stapler validate "$PKG" >>"$LOGS/stapler.log" 2>&1 || die "stapler validate failed (see $LOGS/stapler.log)"
    spctl --assess --type install -vv "$PKG" >>"$LOGS/spctl.log" 2>&1 || die "spctl refuses the pkg (see $LOGS/spctl.log)"
    note "notarized and stapled (submission $id)"
}

dep_version() {  # file regex-with-one-group; empty (the provenance says "unknown") if the file or line is missing
    sed -n "s/$2/\1/p" "$1" 2>/dev/null | head -1 || true
}

provenance() {
    PROV="${PKG%.pkg}.provenance.txt"
    if [ "$REHEARSAL" -eq 0 ]; then  # the provenance must name the code that was built
        [ "$(git rev-parse HEAD)" = "$BUILD_HEAD" ] \
            && [ "$(git -C "$IPLUG2_ROOT" rev-parse HEAD 2>/dev/null || echo -)" = "$BUILD_IPLUG2_HEAD" ] \
            && tree_clean . && tree_clean "$IPLUG2_ROOT" \
            || die "the tree changed during the build; rebuild$(tree_excerpt)"
    fi
    (cd "$DIST" && shasum -a 256 "$(basename "$PKG")" >"$(basename "$PKG").sha256")
    local vst3 rta rtm nl json=engine/third_party/nlohmann/json.hpp
    vst3=$(dep_version "$IPLUG2_ROOT/Dependencies/IPlug/VST3_SDK/pluginterfaces/vst/vsttypes.h" '^#define kVstVersionString[[:space:]]*"VST \([^"]*\)".*')
    rta=$(dep_version "$IPLUG2_ROOT/Dependencies/IPlug/RTAudio/RtAudio.h" '^#define RTAUDIO_VERSION[[:space:]]*"\([^"]*\)".*')
    rtm=$(dep_version "$IPLUG2_ROOT/Dependencies/IPlug/RTMidi/RtMidi.h" '^#define RTMIDI_VERSION[[:space:]]*"\([^"]*\)".*')
    nl="$(dep_version "$json" '^#define NLOHMANN_JSON_VERSION_MAJOR[[:space:]][[:space:]]*\([0-9][0-9]*\).*').$(dep_version "$json" '^#define NLOHMANN_JSON_VERSION_MINOR[[:space:]][[:space:]]*\([0-9][0-9]*\).*').$(dep_version "$json" '^#define NLOHMANN_JSON_VERSION_PATCH[[:space:]][[:space:]]*\([0-9][0-9]*\).*')"
    [ "$nl" != ".." ] || nl=""
    {
        echo "ChordLab $VER"
        echo "pkg:            $(basename "$PKG")"
        echo "sha256:         $(cut -d' ' -f1 "$PKG.sha256")"
        echo "chordlab:       $(git rev-parse HEAD) ($(git describe --tags --always --dirty)) https://github.com/fluxxion82/chordLab"
        if ! tree_clean .; then echo "WARNING:        the ChordLab working tree was dirty when this was built (rehearsal only)"; fi
        if work_tree "$IPLUG2_ROOT"; then
            echo "iplug2:         $(git -C "$IPLUG2_ROOT" rev-parse HEAD) ($(git -C "$IPLUG2_ROOT" describe --tags --always --dirty)) $(git -C "$IPLUG2_ROOT" remote get-url origin 2>/dev/null || echo "$IPLUG2_ROOT")"
        else
            echo "iplug2:         not a git work tree ($IPLUG2_ROOT)"
        fi
        if ! tree_clean "$IPLUG2_ROOT"; then echo "WARNING:        the iPlug2 tree was not a clean git work tree when this was built (rehearsal only)"; fi
        echo "vst3sdk:        ${vst3:-unknown} https://github.com/steinbergmedia/vst3sdk (GPLv3 branch)"
        echo "rtaudio:        ${rta:-unknown} https://github.com/thestk/rtaudio"
        echo "rtmidi:         ${rtm:-unknown} https://github.com/thestk/rtmidi"
        echo "nlohmann-json:  ${nl:-unknown} https://github.com/nlohmann/json"
        echo "xcode:          $(xcodebuild -version | tr '\n' ' ')"
        echo "macos:          $(sw_vers -productVersion) ($(uname -m))"
        echo "formats:        ${FORMATS[*]}"
        echo "auval:          $AUVAL_RESULT"
        if [ "$AUVAL_RESULT" = "$AUVAL_ARM64_ONLY" ]; then echo "WARNING:        auval ran on arm64 only (no Rosetta on the build Mac); the AU's x86_64 slice was not validated"; fi
        if [ "$AUVAL_RESULT" = "$AUVAL_X86_64_ONLY" ]; then echo "WARNING:        auval ran on x86_64 only (an Intel build Mac); the AU's arm64 slice was not validated"; fi
        echo "notarized:      $([ "$SKIP_NOTARIZE" -eq 1 ] && echo no || echo yes)"
        echo "rehearsal:      $([ "$REHEARSAL" -eq 1 ] && echo yes || echo no)"
        echo "built:          $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } >"$PROV"
    note "wrote $(basename "$PROV") and $(basename "$PKG").sha256"
}

prov_field() { sed -n "s/^$1: *//p" "$PROV"; }

publish() {
    VER=$(version_str)
    PKG="$DIST/ChordLab-$VER.pkg"; PROV="${PKG%.pkg}.provenance.txt"
    local remote_sha local_sha tag_rc=0 view_rc=0 assets f
    annotated_tag "$VER" || tag_rc=$?
    [ "$tag_rc" -ne 1 ] || die "HEAD is not tagged v$VER"
    [ "$tag_rc" -ne 2 ] || die "v$VER is a lightweight tag; the release notes come from an annotated one (git tag -a)"
    [ -f "$PKG" ] && [ -f "$PKG.sha256" ] && [ -f "$PROV" ] || die "run 'scripts/release.sh build' on the tag first ($PKG missing)"
    (cd "$DIST" && shasum -a 256 -c "$(basename "$PKG").sha256" >/dev/null 2>&1) || die "$PKG does not match its .sha256"
    [ "$(prov_field chordlab | cut -d' ' -f1)" = "$(git rev-parse HEAD)" ] || die "the provenance names another commit than HEAD; rebuild on the tag"
    [ "$(prov_field rehearsal)" = "no" ] || die "a rehearsal build is not publishable"
    [ "$(prov_field notarized)" = "yes" ] || die "an unnotarized build is not publishable"
    case "$(prov_field auval)" in
        yes) ;;
        "$AUVAL_ARM64_ONLY") note "WARNING: the AU's x86_64 slice was not validated (auval: $AUVAL_ARM64_ONLY)" ;;
        "$AUVAL_X86_64_ONLY") note "WARNING: the AU's arm64 slice was not validated (auval: $AUVAL_X86_64_ONLY)" ;;
        *) die "the AU was not validated (auval: $(prov_field auval)); rebuild without --skip-auval" ;;
    esac
    [ "$(prov_field formats)" = "${FORMATS[*]}" ] || die "the pkg has formats '$(prov_field formats)', expected '${FORMATS[*]}'"
    xcrun stapler validate "$PKG" >/dev/null 2>&1 || die "$PKG is not stapled"
    remote_sha=$(git ls-remote origin "refs/tags/v$VER" | awk -v r="refs/tags/v$VER" '$2 == r {print $1}') || die "git ls-remote origin failed"
    local_sha=$(git rev-parse "refs/tags/v$VER")
    [ -n "$remote_sha" ] || die "tag v$VER is not on origin yet: git push origin main v$VER"
    [ "$remote_sha" = "$local_sha" ] || die "origin's v$VER ($remote_sha) is not the local tag ($local_sha)"
    "$TIMEOUT_CMD" 30 gh release view "v$VER" >/dev/null 2>&1 || view_rc=$?
    [ "$view_rc" -ne 0 ] || die "a release or draft v$VER already exists: finish it (gh release upload/edit) or delete it (gh release delete v$VER)"
    [ "$view_rc" -ne 124 ] || die "gh release view v$VER timed out after 30 s (network? gh auth status)"
    # A draft first, published only once every asset is there: a half-uploaded release is never public.
    gh release create "v$VER" "$PKG" "$PKG.sha256" "$PROV" --draft --title "ChordLab $VER" --notes-from-tag --verify-tag \
        || die "gh release create v$VER --draft failed (a draft may be left: gh release view v$VER)"
    assets=$(gh release view "v$VER" --json assets --jq '.assets[].name') || die "gh release view v$VER failed; the release is still a draft"
    for f in "$PKG" "$PKG.sha256" "$PROV"; do
        grep -qxF "$(basename "$f")" <<<"$assets" \
            || die "the draft release v$VER lacks $(basename "$f"): gh release upload v$VER $f --clobber, then gh release edit v$VER --draft=false"
    done
    gh release edit "v$VER" --draft=false >/dev/null || die "gh release edit v$VER --draft=false failed; the draft has all three assets, run it by hand"
    note "published https://github.com/fluxxion82/chordLab/releases/tag/v$VER"
}

# auval: AUVAL_RESULT is "yes" (both slices), "yes (arm64 only)" (Apple silicon without Rosetta), "yes (x86_64 only)"
# (an Intel Mac) or "skipped"; the provenance says which slice went unvalidated; publish accepts all but "skipped".
AUVAL_ARM64_ONLY="yes (arm64 only)"
AUVAL_X86_64_ONLY="yes (x86_64 only)"
au_validate() {
    local dst="$HOME/Library/Audio/Plug-Ins/Components/ChordLab.component" subtype mfr built_type host
    if [ "$SKIP_AUVAL" -eq 1 ]; then AUVAL_RESULT=skipped; note "auval skipped (--skip-auval): not publishable"; return; fi
    [ ! -e "$LIBRARY_AU" ] || die "$LIBRARY_AU exists: $LIBRARY_AU_FIX"   # again: the build may run long after preflight
    subtype=$(au_subtype); mfr=$(au_mfr)
    [ -n "$subtype" ] && [ -n "$mfr" ] || die "could not read PLUG_UNIQUE_ID / PLUG_MFR_ID from $CONFIG_H"
    # Xcode copies the Info.plist in parallel with the script that regenerates it, so the first build after a plist
    # change can ship the stale one (Task 7): refuse anything but a MIDI processor before validating.
    built_type=$(plutil -extract AudioComponents.0.type raw -o - "$STAGE/au/ChordLab.component/Contents/Info.plist" 2>/dev/null || true)
    [ "$built_type" = aumi ] || die "the staged AU's Info.plist says type '${built_type:-?}', expected aumi (rebuild; the plist copy raced the resource script)"
    note "installing the staged AU into $dst for auval (this Mac only; replaces the dev copy)"
    rm -rf "$dst"; ditto "$STAGE/au/ChordLab.component" "$dst"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    auval -strict -v aumi "$subtype" "$mfr" >"$LOGS/auval.log" 2>&1 || { tail -n 40 "$LOGS/auval.log" >&2; die "auval failed (see $LOGS/auval.log)"; }
    grep -q "AU VALIDATION SUCCEEDED" "$LOGS/auval.log" || die "auval: no success line in $LOGS/auval.log"
    host=$(uname -m)
    if [ "$host" = arm64 ] && arch -x86_64 /usr/bin/true 2>/dev/null; then  # on Intel, arch -x86_64 is native
        arch -x86_64 auval -strict -v aumi "$subtype" "$mfr" >"$LOGS/auval-x86_64.log" 2>&1 || { tail -n 40 "$LOGS/auval-x86_64.log" >&2; die "auval (x86_64 slice) failed (see $LOGS/auval-x86_64.log)"; }
        grep -q "AU VALIDATION SUCCEEDED" "$LOGS/auval-x86_64.log" || die "auval (x86_64 slice): no success line in $LOGS/auval-x86_64.log"
        AUVAL_RESULT=yes
        note "auval passed for arm64 and x86_64 (aumi $subtype $mfr)"
    elif [ "$host" = arm64 ]; then
        AUVAL_RESULT=$AUVAL_ARM64_ONLY
        note "auval passed for arm64 (aumi $subtype $mfr)"
        note "WARNING: Rosetta is not installed; the x86_64 slice was not validated (softwareupdate --install-rosetta)"
    else
        AUVAL_RESULT=$AUVAL_X86_64_ONLY
        note "auval passed for $host (aumi $subtype $mfr)"
        note "WARNING: this Mac is $host; the arm64 slice was not validated (only an Apple silicon Mac can run it)"
    fi
}

build() {
    VER=$(version_str)
    REPORT=0; preflight
    BUILD_HEAD=$(git rev-parse HEAD)
    BUILD_IPLUG2_HEAD=$(git -C "$IPLUG2_ROOT" rev-parse HEAD 2>/dev/null || echo -)
    rm -rf "$DIST"; mkdir -p "$STAGE" "$PKGS" "$LOGS"
    local fmt
    for fmt in "${FORMATS[@]}"; do build_format "$fmt"; done
    # A build that rewrites a tracked file (a version bump's plists left uncommitted) would only be caught after
    # notarization by the provenance guard: stop here instead.
    if [ "$REHEARSAL" -eq 0 ] && { ! tree_clean . || ! tree_clean "$IPLUG2_ROOT"; }; then
        die "the build changed the tree (commit the regenerated Info.plists with a version bump), then rebuild$(tree_excerpt)"
    fi
    for fmt in "${FORMATS[@]}"; do verify_bundle "$STAGE/$fmt/$(bundle_of "$fmt")"; done
    au_validate; package; notarize; provenance
    note "build done: $(basename "$PKG") (formats: ${FORMATS[*]}; auval: $AUVAL_RESULT; notarized: $([ "$SKIP_NOTARIZE" -eq 1 ] && echo no || echo yes))"
}

# ---------------------------------------------------------------------------------------------------------

usage() { sed -n '2,/^set -euo/p' "$SELF" | sed '$d' | sed 's/^# \{0,1\}//'; exit 2; }

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
    publish) publish ;;
    *) usage ;;
esac
