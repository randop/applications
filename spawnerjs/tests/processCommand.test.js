import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { processCommand } from '../index.js';

// For cleaner test output – suppress live console output during tests
const originalLog = console.log;
const originalError = console.error;

describe('processCommand', () => {
  before(() => {
    console.log = () => { };
    console.error = () => { };
  });

  after(() => {
    console.log = originalLog;
    console.error = originalError;
  });

  it('should successfully run a simple command and capture output', async () => {
    const result = await processCommand('echo', ['test123']);

    assert.equal(result.code, 0);
    assert(result.stdout.includes('test123'));
    assert.equal(result.stderr, '');
  });

  it('should handle piped commands via shell', async () => {
    const result = await processCommand('echo "hello world" | tr "[:lower:]" "[:upper:]"');

    assert.equal(result.code, 0);
    assert(result.stdout.includes('HELLO WORLD'));
  });

  it('should reject on non-zero exit code', async () => {
    await assert.rejects(
      processCommand('false'),
      (err) => {
        assert.equal(err.code, 1);
        assert(err.message.includes('Command failed'));
        return true;
      }
    );
  });

  it('should reject when command does not exist', async () => {
    await assert.rejects(
      processCommand('this-command-does-not-exist-xyz'),
      (err) => {
        assert(err.code === 'ENOENT' || err.code === 127 || err.code === 1);
        assert(err.message.includes('Command failed') || err.message.includes('ENOENT'));
        return true;
      }
    );
  });

  it('should respect shell: false when explicitly set', async () => {
    const result = await processCommand('node', ['--version'], { shell: false });

    assert.equal(result.code, 0);
    assert(result.stdout.includes('v'));
  });
});
