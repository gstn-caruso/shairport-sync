export default {
  branches: ['master', 'main'],
  tagFormat: 'v${version}',
  plugins: [
    ['@semantic-release/commit-analyzer', {preset: 'conventionalcommits'}],
    ['@semantic-release/release-notes-generator', {preset: 'conventionalcommits'}],
    ['@semantic-release/changelog', {changelogFile: 'CHANGELOG.md'}],
    ['@semantic-release/exec', {
      prepareCmd: `set -eu
printf '%s\\n' '<%= nextRelease.version %>' > VERSION
cmake -S . -B build/release -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_SYSCONFDIR=/etc -DUSE_GIT_VERSION=OFF
cmake --build build/release --parallel 2
ctest --test-dir build/release --no-tests=error --output-on-failure
cpack --config build/release/CPackConfig.cmake -B build/release/packages
sha256sum build/release/packages/*.deb > build/release/packages/SHA256SUMS`
    }],
    ['@semantic-release/git', {
      assets: ['VERSION', 'CHANGELOG.md'],
      message: 'chore(release): ${nextRelease.version} [skip ci]\\n\\n${nextRelease.notes}'
    }],
    ['@semantic-release/github', {
      assets: ['build/release/packages/*.deb', 'build/release/packages/SHA256SUMS'],
      successComment: false,
      failComment: false,
      failTitle: false,
      releasedLabels: false
    }]
  ]
};
