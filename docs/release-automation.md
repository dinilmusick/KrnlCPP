# Native source release automation

The existing `C++ lifecycle portability` workflow must report success on current
`main` HEAD. The release workflow verifies CMake build and install,
then Release Please manages `version.txt`, release PRs, changelog,
tags and immutable GitHub source releases.

No local credentials are needed. Conventional Commits provide SemVer
bump types. For release PR checks and fully automated merge, replace
GITHUB_TOKEN PR-creation authentication with GitHub App credentials and
require independent CI checks. Default bot-created PRs do not trigger CI.

This implements tagged GitHub source archives, **not** vcpkg registry
or Conan remote publication, which still require registered recipes,
cross-platform consumer checks and a registry write integration.
