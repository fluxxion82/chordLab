# Releasing

How a ChordLab release is built, signed, notarized and published. You only need this if you make releases (of
ChordLab or of a fork); building and testing from source is in [development.md](development.md).

`scripts/release.sh` turns a clean, tagged checkout into the pkg on the Releases page. Run
`scripts/release.sh` with no arguments for the full usage.

## One-time set-up on the signing Mac

- A Developer ID Application and a Developer ID Installer certificate for your Apple team, in the login keychain.
- Notary credentials stored as a keychain profile named `chordlab`:

  ```bash
  xcrun notarytool store-credentials chordlab --apple-id <apple-id> --team-id <team-id> --password <app-specific-password>
  ```

- Rosetta (`softwareupdate --install-rosetta --agree-to-license`), so `auval` also validates the Intel slice. Without
  it the provenance records `auval: yes (arm64 only)`, which `publish` accepts with a warning.

## Per release

1. Bump `PLUG_VERSION_STR` and `PLUG_VERSION_HEX` in `ChordLab/config.h`.
2. Build once (`scripts/check.sh --plugin`) so the six tracked Info.plists in `ChordLab/resources` carry the new
   version.
3. Commit the bump with all six regenerated plists, and merge to `main`.
4. Run the release steps:

```bash
scripts/release.sh preflight --rehearsal   # tools, identities, profile, versions, plists current (before the tag exists)
git tag -a v<version> -m "ChordLab <version>: ..."   # the message becomes the release notes
scripts/release.sh preflight               # now strict: clean trees (ChordLab and iPlug2), iPlug2 commit public on GitHub, HEAD is the tag
scripts/release.sh build                   # Release build of VST3, AU and app into dist/stage, signed, auval, pkgs, notarized, stapled, SDK source tarball
sudo installer -pkg dist/ChordLab-<version>.pkg -target /    # smoke test with any development copies moved aside, then uninstall
git push origin main v<version>            # publish needs the tag on GitHub
scripts/release.sh publish                 # checks the pkg belongs to HEAD and the tag, then creates the GitHub release
```

`publish` uploads four assets: the pkg, its `.sha256`, the `.provenance.txt` and the VST3 SDK source tarball.

## Notes

- **Rehearsal.** `build --rehearsal --skip-notarize` rehearses on a dirty or untagged tree. Its pkg is never
  publishable.
- **Signing.** The Xcode project stays ad-hoc signed. The script injects the Developer ID on the `xcodebuild`
  command line, so building from source needs no certificate.
- **Build time.** `build` starts cold (it wipes `dist/`) and takes several minutes per format.
- **`auval` and the installed Audio Unit.** `build` installs the staged Audio Unit into
  `~/Library/Audio/Plug-Ins/Components` for `auval`, replacing whatever copy was there. Preflight refuses to build while
  `/Library/Audio/Plug-Ins/Components/ChordLab.component` exists, because `auval` names no path and could validate
  that copy instead of the staged one. Move it aside first, and uninstall again after the smoke test.
- **Keychain prompt.** The first `productbuild --sign` may raise a keychain prompt for the Developer ID Installer
  key. The build waits until it is answered: click "Always Allow".
- **Corresponding source.** The `.provenance.txt` names the ChordLab commit and tag, the iPlug2 commit on its
  public GitHub (preflight refuses a commit GitHub cannot show), the VST3 SDK version and its tag, a hash of the
  vendored SDK tree, the tarball's digest, and the RtAudio, RtMidi and nlohmann/json versions. Together with the
  SDK source tarball and [development.md](development.md), they make up the complete corresponding source of the
  binaries under the GPL.
