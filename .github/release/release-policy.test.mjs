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
  ['Update README', null]
]) {
  test(`${message.split('\n')[0]} selects ${expected ?? 'no release'}`, async () => {
    assert.equal(await analyze([message]), expected);
  });
}

test('an empty range does not release', async () => {
  assert.equal(await analyze([]), null);
});

test('the greatest required bump wins regardless of commit order', async () => {
  const messages = ['feat: add output', 'fix: correct timing', 'chore!: remove legacy support'];
  assert.equal(await analyze(messages), 'major');
  assert.equal(await analyze(messages.toReversed()), 'major');
});
