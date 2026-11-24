# KrnlCPP: end-to-end native package release

**Canonical version:** `version.txt` is read by CMake and updated by Release Please's `simple` release strategy; `.release-please-manifest.json` must agree. Only immutable `vX.Y.Z` GitHub tags are eligible.

## Fully automatic flow after one-time authorization

1. Normal pull requests and main pushes run the existing native CI plus `Native package recipe lint`, which validates Conan 2 metadata and version identity.
2. Successful exact-main CI triggers `.github/workflows/release-native.yml`. A short-lived GitHub App installation token creates a Release Please version/changelog PR (with live PR CI) or publishes an immutable GitHub source release after its version PR is merged.
3. `.github/workflows/native-release-pr-automerge.yml` accepts only bot-authored release-please branches with changed files restricted to `version.txt`, `CHANGELOG.md`, and the release manifest. Every named PR-triggered CI run must pass at the exact head SHA before an immutable, rechecked squash merge.
4. A published GitHub Release triggers `.github/workflows/publish-native-packages.yml`, independently verifies tag/version/main ancestry and the Conan recipe, then conditionally distributes **Conan 2 package binaries** to the configured Conan remote and proposes the **vcpkg Git registry port** to `dinilmusick/forge-registry`. The vcpkg port is pinned to the source archive's SHA512, and the registry records the exact Git tree of the versioned port.
5. The shared registry independently installs changed ports in a clean vcpkg consumer, verifies its immutable baseline/version records, and merges only bot-authored registry PRs with exact-head successful CI.

## One-time GitHub and registry setup (never on developer machines)

- Install one least-privilege GitHub App across each native source repository and `forge-registry`: **Actions read, Contents write, Pull requests write**. Configure `RELEASE_PLZ_APP_ID` (GitHub variable), `RELEASE_PLZ_APP_PRIVATE_KEY` (GitHub Actions secret) and `RELEASE_PLZ_APP_READY=true` in each repository, including the shared registry. This avoids the blocked `GITHUB_TOKEN` PR creation path.
- For vcpkg, set `NATIVE_VCPKG_PUBLISH_READY=true` only when the App is installed on `forge-registry` and the registry consumer workflow is green. The registry is currently private: make it public when public downloads are intended, or provide clients read permissions.
- For Conan, set `CONAN_REMOTE_URL` (GitHub variable), `CONAN_REMOTE_USER` and `CONAN_REMOTE_PASSWORD` (protected `native-packages` environment secrets). Restrict the environment to trusted release tags. After verifying the remote and permission scope, set `NATIVE_CONAN_PUBLISH_READY=true`.

Release tags automatically launch publication. If credentials were installed after a tag already existed, dispatch `Native package distribution` with the exact existing `vX.Y.Z` tag and `publish=true` to reconcile; it will not rewrite source code.

**Certification requires external evidence:** GitHub Release tag, Conan remote recipe/binary resolution, vcpkg registry version+tree, and a clean downstream consumer. A skipped publish job is not a release.
