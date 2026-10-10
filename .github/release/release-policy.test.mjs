import assert from 'node:assert/strict';
import test from 'node:test';
import {analyzeCommits} from '@semantic-release/commit-analyzer';
import config from './release.config.mjs';

const policy = config.plugins.find(([name]) => name === '@semantic-release/commit-analyzer')[1];
const analyze = messages => analyzeCommits(policy, {
  commits: messages.map(message => ({message})),
  logger: {log() {}}
});

for (const [message, expected] of [
  ['feat(audio): add a receiver', 'minor'],
  ['fix(audio): prevent a race', 'patch'],
  ['perf(audio): avoid copying', 'patch'],
  ...['docs', 'test', 'refactor', 'style', 'chore', 'build', 'ci'].map(type => [`${type}: maintain project`, null]),
  ['fix!: remove an option', 'major'],
  ['docs: change contract\n\nBREAKING CHANGE: old configuration is rejected', 'major'],
  ['Update README', null],
  ['Revert "fix: correct timing"\n\nThis reverts commit abcdef1234567890.', null]
]) {
  test(`${message.split('\n')[0]} selects ${expected ?? 'no release'}`, async () => {
    assert.equal(await analyze([message]), expected);
  });
}

test('an empty range does not release', async () => {
  assert.equal(await analyze([]), null);
});

test('a fully reverted range does not release', async () => {
  assert.equal(await analyzeCommits(policy, {
    commits: [
      {hash: '1234567890abcdef', message: 'Revert "fix: correct timing"\n\nThis reverts commit abcdef1234567890.'},
      {hash: 'abcdef1234567890', message: 'fix: correct timing'}
    ],
    logger: {log() {}}
  }), null);
});

test('the greatest required bump wins regardless of commit order', async () => {
  const messages = ['feat: add output', 'refactor: delegate decisions', 'fix: correct timing'];
  assert.equal(await analyze(messages), 'minor');
  assert.equal(await analyze(messages.toReversed()), 'minor');
  messages.push('chore!: remove legacy support');
  assert.equal(await analyze(messages), 'major');
  assert.equal(await analyze(messages.toReversed()), 'major');
});
