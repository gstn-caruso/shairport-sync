import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import test from 'node:test';
import yaml from 'js-yaml';

const action = yaml.load(readFileSync(new URL('../actions/setup-toolchain/action.yml', import.meta.url), 'utf8'));
const selection = action.runs.steps.find(step => step.id === 'architecture')?.run;

for (const [machine, architecture, llvm] of [['x86_64', 'amd64', 'X64'], ['aarch64', 'arm64', 'ARM64']]) {
  test(`native ${machine} selects matching toolchain archives`, () => {
    const directory = mkdtempSync(join(tmpdir(), 'toolchain-'));
    try {
      assert.ok(selection, 'native architecture selection must exist');
      const output = join(directory, 'output');
      execFileSync('bash', ['-eu', '-c', 'uname() { printf "%s\\n" "$TEST_MACHINE"; }\n' + selection], {
        env: {...process.env, TEST_MACHINE: machine, GITHUB_OUTPUT: output}
      });
      const values = Object.fromEntries(readFileSync(output, 'utf8').trim().split('\n').map(line => line.split('=')));
      assert.equal(values.asdf_arch, architecture);
      assert.equal(values.llvm_arch, llvm);
      assert.match(values.llvm_sha256, /^[a-f0-9]{64}$/);
    } finally {
      rmSync(directory, {recursive: true, force: true});
    }
  });
}

test('an unsupported native architecture fails before installing toolchains', () => {
  assert.ok(selection);
  assert.throws(() => execFileSync('bash', ['-eu', '-c', 'uname() { printf "riscv64\\n"; }\n' + selection], {
    env: {...process.env, GITHUB_OUTPUT: '/dev/null'}, stdio: 'pipe'
  }), /Unsupported native toolchain architecture/);
});
