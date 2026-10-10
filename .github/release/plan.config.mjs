import release from './release.config.mjs';

export default {
  ...release,
  plugins: release.plugins.map(([name, options]) => [name, name === '@semantic-release/exec' ? {
    verifyReleaseCmd: `printf 'version=%s\\n' '<%= nextRelease.version %>' >> "$GITHUB_OUTPUT"`
  } : options])
};
