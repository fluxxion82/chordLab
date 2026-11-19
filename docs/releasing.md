# Releasing

How a ChordLab release is built, signed, notarized and published. You only need this if you make releases (of
ChordLab or of a fork). Building and testing from source is in [development.md](development.md).

Releases are built by GitHub Actions ([`.github/workflows/release.yml`](../.github/workflows/release.yml)) when a
`v*` tag is pushed. The workflow runs `scripts/release.sh` on GitHub's hosted `xcode-27` runner, and the same script
also runs by hand on a Mac (see [Releasing from your own Mac](#releasing-from-your-own-mac)). Run
`scripts/release.sh` with no arguments for its full usage.

## One-time set-up: secrets

The workflow signs with a Developer ID Application and a Developer ID Installer certificate, and notarizes with an
App Store Connect API key. They are stored as seven secrets in a GitHub environment named `release`:

| Secret | What it holds |
|---|---|
| `MACOS_DEVELOPER_ID_APPLICATION_P12` | the Developer ID Application identity, exported as `.p12`, base64 |
| `MACOS_DEVELOPER_ID_APPLICATION_P12_PASSWORD` | that export's password |
| `MACOS_DEVELOPER_ID_INSTALLER_P12` | the Developer ID Installer identity, exported as `.p12`, base64 |
| `MACOS_DEVELOPER_ID_INSTALLER_P12_PASSWORD` | that export's password |
| `APPLE_NOTARY_API_KEY_P8` | the App Store Connect API key file `AuthKey_<KEY_ID>.p8`, base64 |
| `APPLE_NOTARY_API_KEY_ID` | the key's Key ID |
| `APPLE_NOTARY_API_ISSUER_ID` | the key's Issuer ID |

To set them up, run these commands in the repository with the GitHub CLI.

1. Create the environment:

   ```bash
   gh api -X PUT "repos/{owner}/{repo}/environments/release"
   ```

2. In Keychain Access → My Certificates, export each Developer ID identity as a `.p12` with a new password.
3. Store the two exports and their passwords. Each password command prompts for the value, so it stays out of your
   shell history:

   ```bash
   base64 -i ~/Desktop/devid-app.p12 | gh secret set MACOS_DEVELOPER_ID_APPLICATION_P12 --env release
   ```

   ```bash
   gh secret set MACOS_DEVELOPER_ID_APPLICATION_P12_PASSWORD --env release
   ```

   ```bash
   base64 -i ~/Desktop/devid-installer.p12 | gh secret set MACOS_DEVELOPER_ID_INSTALLER_P12 --env release
   ```

   ```bash
   gh secret set MACOS_DEVELOPER_ID_INSTALLER_P12_PASSWORD --env release
   ```

4. In App Store Connect → Users and Access → Integrations → App Store Connect API → Team Keys, generate a key with
   Developer access. Download `AuthKey_<KEY_ID>.p8` (it can be downloaded only once) and note the Key ID and the
   Issuer ID. Then:

   ```bash
   base64 -i ~/Downloads/AuthKey_<KEY_ID>.p8 | gh secret set APPLE_NOTARY_API_KEY_P8 --env release
   ```

   ```bash
   gh secret set APPLE_NOTARY_API_KEY_ID --env release
   ```

   ```bash
   gh secret set APPLE_NOTARY_API_ISSUER_ID --env release
   ```

5. Delete the exported `.p12` files. Optionally, once a rehearsal has passed, restrict the `release` environment
   to `main` and `v*` tags in the repository's settings.

When the Developer ID certificates are renewed, export and store the two `.p12` secrets again.

## Per release

1. Bump `PLUG_VERSION_STR` and `PLUG_VERSION_HEX` in `ChordLab/config.h`. Build once (`scripts/check.sh --plugin`) so
   the six tracked Info.plists in `ChordLab/resources` carry the new version. Commit the bump with all six; CI fails
   if they are missing.
2. Merge to `main` and wait for CI to pass.
3. Optionally, rehearse: this builds, signs and notarizes the current `main` as a pkg named after the commit and
   uploads it as a workflow artifact only. Never publishable.

   ```bash
   gh workflow run release.yml --ref main -f notarize=true
   ```

4. Tag the release commit and push the tag. The tag's message becomes the release notes.

   ```bash
   git tag -a v<version> -m "ChordLab <version>: ..."
   ```

   ```bash
   git push origin v<version>
   ```

5. The workflow builds the VST3, Audio Unit and app, signs them, validates the Audio Unit with `auval` (both slices,
   under Rosetta), packages, notarizes and staples the pkg. A separate publish job, the only one allowed to write,
   then creates a **draft** release with four assets: the pkg, its `.sha256`, the `.provenance.txt` and the VST3
   SDK source tarball.
6. Download the draft, check it and smoke-install it on a Mac with any development copies moved aside:

   ```bash
   gh release download v<version>
   ```

   ```bash
   shasum -a 256 -c --ignore-missing ChordLab-<version>.pkg.sha256
   ```

   ```bash
   sudo installer -pkg ChordLab-<version>.pkg -target /
   ```

7. Publish the draft:

   ```bash
   gh release edit v<version> --draft=false
   ```

## Releasing from your own Mac

The hand-run path does the same as the workflow, with the identities in your login keychain.

One-time set-up:

- Install the Developer ID Application and Installer certificates in the login keychain.
- Store notary credentials as a keychain profile named `chordlab`:

  ```bash
  xcrun notarytool store-credentials chordlab --apple-id <apple-id> --team-id <team-id> --password <app-specific-password>
  ```

  Alternatively, set `CHORDLAB_NOTARY_KEY_FILE` (the `.p8` file), `CHORDLAB_NOTARY_KEY_ID` and
  `CHORDLAB_NOTARY_ISSUER` to notarize with an App Store Connect API key instead, as the workflow does.
- Install Rosetta (`softwareupdate --install-rosetta --agree-to-license`) so `auval` also validates the Intel slice.
  Without it the provenance records `auval: yes (arm64 only)`, which `publish` accepts with a warning.

After step 2 of [Per release](#per-release):

```bash
scripts/release.sh preflight --rehearsal   # tools, identities, notary settings, the iPlug2 pin, versions, plists
git tag -a v<version> -m "ChordLab <version>: ..."   # the message becomes the release notes
scripts/release.sh preflight               # now strict: clean trees, iPlug2 public on GitHub, HEAD is the tag
scripts/release.sh build                   # Release build, signing, auval, pkgs, notarization, stapling, SDK source tarball
sudo installer -pkg dist/ChordLab-<version>.pkg -target /    # smoke test with development copies moved aside, then uninstall
git push origin main v<version>            # publish needs the tag on GitHub
scripts/release.sh publish --draft         # checks the pkg belongs to HEAD and the tag, then creates a draft release
```

Pushing the tag also starts the release workflow. Cancel that run (in the Actions tab, or with `gh run cancel`) so
that only one of the two creates the release. `publish` without `--draft` publishes the release as soon as all four
assets are uploaded.

## Notes

- **The iPlug2 pin.** Preflight refuses an iPlug2 checkout or VST3 SDK that doesn't match `scripts/iplug2.pin`, and
  an iPlug2 commit GitHub cannot show publicly. The strict preflight also wants the iPlug2 work tree clean, with
  no untracked files.
- **Rehearsal.** `build --rehearsal --skip-notarize` rehearses on a dirty or untagged tree. Its pkg is never
  publishable.
- **Signing.** The Xcode project stays ad-hoc signed. The script injects the Developer ID on the `xcodebuild`
  command line, so building from source needs no certificate.
- **Build time.** `build` starts cold (it wipes `dist/`) and takes several minutes per format.
- **`auval` and the installed Audio Unit.** `build` installs the staged Audio Unit into
  `~/Library/Audio/Plug-Ins/Components` for `auval`, replacing whatever copy was there. Preflight refuses to build
  while `/Library/Audio/Plug-Ins/Components/ChordLab.component` exists, because `auval` names no path and could
  validate that copy instead. Move it aside first, and uninstall again after the smoke test.
- **Keychain prompt.** On your own Mac, the first `productbuild --sign` may raise a keychain prompt for the
  Developer ID Installer key. The build waits until it is answered: click "Always Allow".
- **Corresponding source.** The `.provenance.txt` names the ChordLab commit and tag, the iPlug2 commit, the VST3 SDK
  version and its tag, a hash of the vendored SDK tree, the tarball's digest, and the RtAudio, RtMidi and
  nlohmann/json versions. Together with the SDK source tarball and [development.md](development.md), they make up
  the complete corresponding source of the binaries under the GPL.
