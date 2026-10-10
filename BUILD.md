# Build and install

## CMake / C++26 build

Use CMake 4.2 or newer, Ninja, Clang 23.1.3 (pinned in `.tool-versions`)
and libstdc++ 15. CMake checks C++26 without GNU extensions by compiling and
running `cmake/cpp26_probe.cpp`. All receiver production sources, including the
entrypoint and generated plist, compile as C++26. The bundled pairing dependency
remains C behind an explicit linkage boundary; C tests exercise the receiver APIs.
CMake rejects any own production source configured to compile as C.

Install the asdf toolchain from the repository root:

```sh
sudo apt-get install cmake ninja-build g++-15 g++ python3
asdf plugin add clang https://github.com/higebu/asdf-llvm.git
git -C "${ASDF_DATA_DIR:-$HOME/.asdf}/plugins/clang" checkout b7b8dd389c790237145e7436f1cd85b49611e37e
asdf install
asdf reshim clang
```

The asdf plugin builds LLVM from source. CI uses a checksum-verified official
binary archive; see the workflow in `.github/workflows`.
The toolchain resolves compiler paths through `asdf which` from the repository.
Install the receiver dependencies on Debian/Ubuntu, then build:

```sh
sudo apt-get install pkg-config libgtest-dev libpopt-dev libconfig-dev libpulse-dev libavahi-client-dev libssl-dev libplist-dev libplist-utils libsodium-dev libgcrypt20-dev uuid-dev libavutil-dev libavcodec-dev libavformat-dev libswresample-dev xxd
cmake -S . -B build/cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_SYSCONFDIR=/etc
cmake --build build/cmake --parallel 2
ctest --test-dir build/cmake --output-on-failure
DESTDIR="$PWD/build/cmake/stage" cmake --install build/cmake
```

Only Linux with AirPlay 2, PulseAudio, Avahi, FFmpeg and OpenSSL is supported.
Provider switches are rejected. `INSTALL_CONFIG_FILES=OFF` disables installing
the sample configuration. CMake installs the binary under the chosen prefix,
the manual under its `share/man/man1` directory, and the sample under
`CMAKE_INSTALL_SYSCONFDIR`.
Generated configuration, plist and Git version files live in the CMake build
directory. Use an out-of-tree build; remove any stale `config.h` from the source
directory before configuring. To install on the host after checking the staged
files, run `sudo cmake --install build/cmake`.

## Build checks

Test builds require GoogleTest 1.17 or newer (`libgtest-dev`). Production-only
builds configured with `-DBUILD_TESTING=OFF` do not require GoogleTest.
All C++ suites register named scenarios with CTest. For a short feedback cycle,
build the relevant executable and select one case:

```sh
cmake --build build/cmake --target volume-policy-test --parallel 2
ctest --test-dir build/cmake -R '^VolumePolicy.MuteLevelRequestsMute$' --no-tests=error --output-on-failure
```

Use `ctest --test-dir build/cmake -N` to list cases, or a suite prefix such as
`-R '^RtpClock\.'` to select a group. Keep `--no-tests=error` so a mistyped name
fails. Full builds and CTest runs still cover the C adapters, linkage and shell
contracts. Policy, clock, audio-format/mapping, PCM/samples and playback-timing
targets use separate libraries; unrelated receiver changes do not relink their
test executables. This improves targeted feedback rather than clean-build time.

CI runs native amd64 and arm64 CMake Release, Debug, ASan+UBSan and TSan builds,
all with the same pinned compiler and library. Every build runs its contracts
and stages the binary, manual and sample configuration without starting a service.
The sanitizer jobs check instrumentation in receiver C++ object files. TSan
detects data races between threads; ASan detects memory misuse and UBSan detects
undefined behavior. All eight builds must pass before a release can publish.
Failed tests upload CTest diagnostics, and an empty test selection fails CI.

The compiler installation must include compiler-rt runtimes for sanitizer builds;
the official binary archive used by CI includes them. Some source-built asdf
installations contain only Clang and headers and must install compiler-rt first.
Reproduce ASan+UBSan separately from TSan:

```sh
cmake -S . -B build/asan -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
cmake --build build/asan --parallel 2
ctest --test-dir build/asan --output-on-failure
cmake -S . -B build/tsan -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=thread -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=thread -fno-omit-frame-pointer'
cmake --build build/tsan --parallel 2
ctest --test-dir build/tsan --output-on-failure
```

The NQPTP fixtures preload an uninstrumented test-only shared library built by
the system `cc`; receiver code remains instrumented with Clang's static sanitizer
runtime. Expected startup failures still reject sanitizer diagnostics, including
leaks. Leak detection remains enabled. A TSan runtime startup failure caused by
host address-space or sandbox restrictions is failed infrastructure validation;
it does not establish the absence of data races.

Install and run NQPTP compatible with this receiver's shared-memory interface (currently SMI version 10) and Avahi as system services. Start a PulseAudio-compatible user session. PipeWire users should run `pipewire-pulse`; the native PipeWire backend is not provided.

Copy `shairport-sync.conf.sample` to `shairport-sync.conf` and edit it before
starting the receiver. When migrating an existing configuration, see
[CONFIGURATION.md](CONFIGURATION.md) for unsupported settings.

From the repository root, `./user-service-install.sh --dry-run` previews user service installation. `./user-service-install.sh` installs and starts the unit for your current user. Its default executable path is `/usr/local/bin/shairport-sync`; edit the unit if you chose another prefix. Do not run the installer as root. NQPTP and Avahi must already be running.

`systemctl --user status shairport-sync` and `journalctl --user -u shairport-sync` show receiver status and logs. The unit does not create a system account or install a system-wide receiver service.

## Debian packages and releases

Every CI Release build uploads a `.deb` and checksum as workflow artifacts,
including changes that do not increase the version. Packages built by CI target
Ubuntu 26.04 amd64 and arm64 and depend on that distribution's runtime libraries; they are
not universal packages for older Ubuntu or Debian releases. CPack derives the
library dependencies with `dpkg-shlibdeps`. NQPTP must be installed separately;
the package does not install or start NQPTP, Avahi or the receiver.
The arm64 package supports Raspberry Pi hardware running Ubuntu 26.04 arm64.
It requires a 64-bit OS; Raspberry Pi OS and older Ubuntu releases are separate
distribution targets. CI builds and executes the arm64 tests and packaged binary
on a native GitHub ARM runner inside Ubuntu 26.04, without emulation.

Build a package locally with the installed distribution's dependencies:

```sh
sudo apt-get install dpkg-dev file
cmake -S . -B build/package -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_SYSCONFDIR=/etc -DUSE_GIT_VERSION=OFF
cmake --build build/package --parallel 2
ctest --test-dir build/package --no-tests=error --output-on-failure
cpack --config build/package/CPackConfig.cmake -B build/package/packages
sudo apt install ./build/package/packages/shairport-sync_*.deb
```

The package contains `/usr/bin/shairport-sync`, the manual, license notices,
configuration documentation and `/etc/shairport-sync.conf.sample`. Copy the
sample to `/etc/shairport-sync.conf` and configure it before starting. The
packaged user unit uses `/usr/bin/shairport-sync`; enable it with
`systemctl --user enable --now shairport-sync` from the PulseAudio user's session
after starting NQPTP and Avahi. Remove any older user-unit override that still
points to `/usr/local/bin/shairport-sync`.

Pushes to `master` or `main`, including merged PRs, run the same validation.
Before native builds, semantic-release performs a dry run to select the version
from commits since the last release. After all checks pass, it publishes that
same version using the tested packages:

| Commit type | Version change |
| --- | --- |
| Any type with `!` or a `BREAKING CHANGE:` footer | Major |
| `feat` | Minor |
| `fix`, `perf` | Patch |
| `docs`, `test`, `refactor`, `style`, `chore`, `build`, `ci` | None |

The largest required bump wins. Preserve that meaning in squash-merge titles
and bodies. `VERSION` is the CMake version source. For a release, automation
updates it with `CHANGELOG.md`, commits the
release metadata, creates `v<version>` and publishes a GitHub Release with the
amd64 and arm64 `.deb` files and a shared `SHA256SUMS`. Both architectures build
and test the planned version before publication. The publisher requires exactly
one package for each architecture with matching package name, version and
architecture metadata. Release binaries use the package version instead of a
Git description. Release jobs are serialized; superseded runs skip publication
so a later validated push covers their commits.

For the initial rollout, tag the last pre-automation `master` commit
`3740f383` as `v5.5.1` before merging this workflow. This establishes the existing
version baseline; a feature then releases 5.6.0 rather than starting at 1.0.0.
Do not increment `VERSION` manually for subsequent changes. Release tooling is
pinned by `.github/release/package-lock.json`; reproduce its policy checks with
`npm ci --prefix .github/release` and `npm test --prefix .github/release`.

### Recovering a partially published release

A failure can occur after the release metadata commit is pushed but before the
tag or GitHub Release is published. Rerunning that workflow can skip the release
because the branch now points to the metadata commit. Recover the intended
version from that commit; do not bump `VERSION` again or run semantic-release
against a different source commit. Failed release jobs retain prepared packages
as `release-packages-<source-sha>` when those files exist. Native build failures
retain CTest diagnostics under `ctest-<architecture>-<build>-<source-sha>`.

Record the failed run ID, its source SHA and the exact automated
`chore(release): <version>` commit from the run logs and branch history. In a
fresh recovery clone, fetch the branch and tags, then inspect that commit:

```sh
git clone https://github.com/gstn-caruso/shairport-sync.git recovery
cd recovery
git fetch origin --tags
release_commit='REPLACE_WITH_EXACT_METADATA_COMMIT_SHA'
source_sha='REPLACE_WITH_FAILED_RUN_SOURCE_SHA'
failed_run='REPLACE_WITH_FAILED_RUN_ID'
release_version=$(git show "$release_commit:VERSION")
release_tag="v$release_version"
test "$(git rev-parse "$release_commit^")" = "$source_sha"
git diff "$source_sha" "$release_commit" --stat
git show "$release_commit:CHANGELOG.md" > recovery-notes.md
```

The metadata commit must change only `VERSION` and `CHANGELOG.md`, and its
changelog must describe the intended version. Stop if the source or version
does not match. If the tag exists, verify that it points to this exact commit:
`test "$(git rev-parse "$release_tag^{commit}")" = "$release_commit"`.
If the failure happened before tag creation, create and push only that missing
tag: `git tag "$release_tag" "$release_commit"`, then
`git push origin "refs/tags/$release_tag"`. Do not move an existing tag.

Download the prepared packages from the failed run and verify them before upload:

```sh
gh run download "$failed_run" --name "release-packages-$source_sha" --dir recovery-assets
(cd recovery-assets && sha256sum --check SHA256SUMS)
for architecture in amd64 arm64; do
  package="recovery-assets/shairport-sync_${release_version}_${architecture}.deb"
  test "$(dpkg-deb -f "$package" Version)" = "$release_version"
  test "$(dpkg-deb -f "$package" Architecture)" = "$architecture"
done
```

If packaging or checksum generation failed, or the artifact has expired,
check out the exact release commit with `git checkout --detach "$release_commit"`
in this recovery clone and rebuild using the package commands above. Keep
`/usr`, `/etc` and `USE_GIT_VERSION=OFF`, run the tests, and verify the extracted
binary reports the intended version. Rebuild on both Ubuntu 26.04 amd64 and
Ubuntu 26.04 arm64 to reproduce the native CI distribution targets. Copy both
resulting `.deb` files to `recovery-assets` and create their checksums with
`(cd recovery-assets && sha256sum *.deb > SHA256SUMS)`, then repeat the checksum
and package-version checks. This rebuild retains the recorded source and version
without creating another metadata commit or bump.

Inspect `gh release view "$release_tag" --json isDraft,assets,targetCommitish`.
If a draft exists, download its current assets to a separate directory, compare
each existing file against the verified recovery files, and upload only missing
assets with `gh release upload "$release_tag" recovery-assets/FILE` (replace
`FILE` with the missing `.deb` or `SHA256SUMS`). Finish that draft with
`gh release edit "$release_tag" --draft=false --notes-file recovery-notes.md`.
If the release does not exist, create it for the verified existing tag and exact
metadata commit:

```sh
gh release create "$release_tag" recovery-assets/*.deb recovery-assets/SHA256SUMS \
  --verify-tag --target "$release_commit" --title "$release_tag" \
  --notes-file recovery-notes.md
```

Do not treat an authentication or network error as proof that a release is
missing. For an already published release, download its assets and verify their
hashes against `SHA256SUMS`; identical assets require no replacement. Stop on
any hash mismatch. If that release is incomplete, upload only its missing
assets after comparing all existing assets with the verified recovery files.
Do not use `--clobber`, delete a published release, or replace
published binaries with a differing rebuild. Verify the final tag, release,
version and downloaded checksums before declaring recovery complete.

Release tooling currently has 15 upstream dependency audit advisories, including
the commit-analyzer's `micromatch`/`braces` chain and bundled npm dependencies.
Check with `npm audit --prefix .github/release`; the pinned latest tooling and
safe automatic fixes did not resolve these advisories at initial rollout.
