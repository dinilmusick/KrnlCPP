# Automated native package release

## Flow

\`main\` → exact-head native CI → automated Release Please version PR → independent PR CI → bot merge → immutable \`vX.Y.Z\` GitHub Release → package distribution.

### Conan 2

Each repository contains its own Conan recipe. After a GitHub Release, \`.github/workflows/publish-native-packages.yml\` independently verifies the tag, canonical version, clean CMake install and Conan recipe. When \`NATIVE_CONAN_PUBLISH_READY=true\`, it creates the package and uploads it to the configured Conan 2 remote using the protected \`native-packages\` environment.

### vcpkg

There is **no shared vcpkg registry repository**. Each GitHub Release contains a self-contained \`<package>-<version>-vcpkg-port.tar.gz\` and SHA512 file containing the port metadata. This makes the package release independently distributable and reproducible without a central repository. The port's \`vcpkg.json\` pins the immutable source release.

### Authorization

Normal release automation uses a short-lived GitHub App installation token for release PR creation. Do not use a developer personal access token in repository workflows. Configure:

- \`RELEASE_PLZ_APP_ID\` repository variable
- \`RELEASE_PLZ_APP_PRIVATE_KEY\` protected secret
- \`RELEASE_PLZ_APP_READY=true\`

Conan activation additionally requires:

- \`CONAN_REMOTE_URL\` repository variable
- \`CONAN_REMOTE_USER\` protected \`native-packages\` secret
- \`CONAN_REMOTE_PASSWORD\` protected \`native-packages\` secret
- \`NATIVE_CONAN_PUBLISH_READY=true\`

Nothing is published until the corresponding readiness gate is enabled.

## Verification contract

A release is considered certified only after exact-head CI succeeds, the immutable GitHub tag matches \`version.txt\`, the GitHub Release exists, the Conan upload (when enabled) is independently resolved from the remote, and the vcpkg port archive checksum matches the published release asset.

No shared registry or developer-machine publishing credential is required.
