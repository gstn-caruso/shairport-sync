import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {Writable} from 'node:stream';
import {pathToFileURL} from 'node:url';
import test from 'node:test';
import {prepare, verifyRelease} from '@semantic-release/exec';
import config from './release.config.mjs';

const version = '5.7.0';
const fixture = () => {
  const cwd = mkdtempSync(join(tmpdir(), 'release-packages-'));
  mkdirSync(join(cwd, 'build/release/packages'), {recursive: true});
  return cwd;
};
const packageFor = (cwd, architecture, packageVersion = version, filenameArchitecture = architecture) => {
  const root = join(cwd, `fixture-${filenameArchitecture}`);
  mkdirSync(join(root, 'DEBIAN'), {recursive: true});
  writeFileSync(join(root, 'DEBIAN/control'), `Package: shairport-sync\nVersion: ${packageVersion}\nArchitecture: ${architecture}\nMaintainer: Test <test@example.com>\nDescription: Release fixture\n`);
  execFileSync('dpkg-deb', ['--build', root, join(cwd, 'build/release/packages', `shairport-sync_${packageVersion}_${filenameArchitecture}.deb`)], {stdio: 'pipe'});
};
const silentOutput = () => new Writable({write(chunk, encoding, callback) { callback(); }});
const context = cwd => ({cwd, env: {...process.env, RELEASE_VERSION: version}, nextRelease: {version}, logger: {log() {}}, stdout: silentOutput(), stderr: silentOutput()});
const execution = config.plugins.find(([name]) => name === '@semantic-release/exec')[1];

test('publication prepares the same version from exactly two native packages', async () => {
  const cwd = fixture();
  try {
    packageFor(cwd, 'amd64');
    packageFor(cwd, 'arm64');
    await prepare(execution, context(cwd));
    assert.equal(readFileSync(join(cwd, 'VERSION'), 'utf8'), `${version}\n`);
    const checksums = readFileSync(join(cwd, 'build/release/packages/SHA256SUMS'), 'utf8');
    assert.equal(checksums.trim().split('\n').length, 2);
    execFileSync('sha256sum', ['--check', 'SHA256SUMS'], {cwd: join(cwd, 'build/release/packages'), stdio: 'pipe'});
  } finally {
    rmSync(cwd, {recursive: true, force: true});
  }
});

for (const failure of ['missing ARM64', 'wrong version', 'wrong architecture', 'unexpected extra package', 'changed plan']) {
  test(`publication rejects ${failure} before updating VERSION`, async () => {
    const cwd = fixture();
    try {
      packageFor(cwd, 'amd64');
      if (failure !== 'missing ARM64') packageFor(cwd, failure === 'wrong architecture' ? 'amd64' : 'arm64', failure === 'wrong version' ? '5.6.0' : version, 'arm64');
      if (failure === 'unexpected extra package') packageFor(cwd, 'all');
      const invocation = context(cwd);
      if (failure === 'changed plan') invocation.env.RELEASE_VERSION = '5.8.0';
      await assert.rejects(prepare(execution, invocation));
      assert.equal(existsSync(join(cwd, 'VERSION')), false);
    } finally {
      rmSync(cwd, {recursive: true, force: true});
    }
  });
}

test('the dry-run verification hook emits the semantic-release version without preparing a release', async () => {
  const {default: plan} = await import('./plan.config.mjs');
  const cwd = fixture();
  try {
    const output = join(cwd, 'output');
    const invocation = context(cwd);
    invocation.env.GITHUB_OUTPUT = output;
    invocation.options = {dryRun: true};
    const plugin = plan.plugins.find(([name]) => name === '@semantic-release/exec')[1];
    await verifyRelease(plugin, invocation);
    assert.equal(readFileSync(output, 'utf8'), `version=${version}\n`);
    assert.equal(existsSync(join(cwd, 'VERSION')), false);
  } finally {
    rmSync(cwd, {recursive: true, force: true});
  }
});

test('semantic-release dry-run invokes the planner hook with the calculated bump', async () => {
  const directory = fixture();
  try {
    const remote = join(directory, 'remote.git');
    const cwd = join(directory, 'repository');
    execFileSync('git', ['init', '--bare', '--initial-branch=master', remote], {stdio: 'pipe'});
    execFileSync('git', ['clone', remote, cwd], {stdio: 'pipe'});
    const git = (...args) => execFileSync('git', args, {cwd, stdio: 'pipe'});
    git('checkout', '-b', 'master');
    git('config', 'user.email', 'test@example.com');
    git('config', 'user.name', 'Release Test');
    git('commit', '--allow-empty', '-m', 'chore: baseline');
    git('tag', 'v5.6.0');
    git('commit', '--allow-empty', '-m', 'feat: provide ARM64 packages');
    git('push', 'origin', 'master', '--tags');
    const output = join(directory, 'output');
    const integration = `
      import semanticRelease from ${JSON.stringify(import.meta.resolve('semantic-release'))};
      import plan from ${JSON.stringify(new URL('./plan.config.mjs', import.meta.url).href)};
      const result = await semanticRelease({
        ...plan, repositoryUrl: ${JSON.stringify(pathToFileURL(remote).href)}, dryRun: true, ci: false,
        plugins: plan.plugins.filter(([name]) => ['@semantic-release/commit-analyzer', '@semantic-release/release-notes-generator', '@semantic-release/exec'].includes(name))
      });
      if (result.nextRelease.version !== '5.7.0') process.exit(1);
    `;
    execFileSync(process.execPath, ['--input-type=module', '-e', integration], {cwd, env: {...process.env, GITHUB_OUTPUT: output}, stdio: 'pipe'});
    assert.equal(readFileSync(output, 'utf8'), `version=${version}\n`);
    assert.equal(existsSync(join(cwd, 'VERSION')), false);
    assert.throws(() => git('rev-parse', '--verify', 'refs/tags/v5.7.0'));
  } finally {
    rmSync(directory, {recursive: true, force: true});
  }
});
