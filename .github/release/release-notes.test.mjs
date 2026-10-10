import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import test from 'node:test';
import {fileURLToPath} from 'node:url';
import {analyzeCommits} from '@semantic-release/commit-analyzer';
import config from './release.config.mjs';

test('the configured release plugins generate feature, fix and breaking notes from the repository root', async () => {
  const cwd = fileURLToPath(new URL('../../', import.meta.url));
  const git = (...args) => execFileSync('git', args, {cwd, encoding: 'utf8'}).trim();
  const gitHead = git('rev-parse', 'HEAD');
  const commits = [
    'fix(audio): correct playback timing',
    'feat(audio): add an output format',
    'feat(audio)!: remove legacy output\n\nBREAKING CHANGE: legacy output is no longer supported'
  ].map(message => ({message, hash: gitHead}));
  const context = {
    cwd,
    commits,
    lastRelease: {version: '5.5.1', gitTag: 'v5.5.1'},
    nextRelease: {version: '6.0.0', gitTag: 'v6.0.0', gitHead},
    options: {repositoryUrl: git('remote', 'get-url', 'origin')},
    logger: {log() {}}
  };
  const analyzer = config.plugins.find(([name]) => name === '@semantic-release/commit-analyzer')[1];
  const [notesPlugin, notesOptions] = config.plugins.find(([name]) => name === '@semantic-release/release-notes-generator');
  const {generateNotes} = await import(notesPlugin);

  assert.equal(await analyzeCommits(analyzer, context), 'major');
  const notes = await generateNotes(notesOptions, context);

  assert.match(notes, /6\.0\.0/);
  assert.match(notes, /compare\/v5\.5\.1\.\.\.v6\.0\.0/);
  assert.match(notes, /### Bug Fixes/);
  assert.match(notes, /correct playback timing/);
  assert.match(notes, /### Features/);
  assert.match(notes, /add an output format/);
  assert.match(notes, /### ⚠ BREAKING CHANGES/);
  assert.match(notes, /legacy output is no longer supported/);
});
