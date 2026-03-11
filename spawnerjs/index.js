import { spawn } from 'node:child_process';

/**
 * Runs a shell command (or executable) and captures its stdout/stderr while streaming output live to the console.
 *
 * @param {string} cmd - The command to execute (e.g. "ls", "git", "npm run build", "echo 'hi' | tr a-z A-Z").
 *                       When `shell: true` (default), this can include shell syntax like pipes, redirects, &&, etc.
 * @param {string[]} [args=[]] - Array of arguments to pass to the command.
 *                                 Ignored / not recommended when using full shell commands (e.g. "echo hello | grep h").
 * @param {object} [options={}] - Additional options passed directly to `child_process.spawn`.
 * @param {boolean} [options.shell=true] - Whether to run the command inside a shell (default: true).
 *                                          Set to `false` for direct binary execution (more secure, no shell features).
 * @param {string} [options.cwd] - Current working directory for the child process.
 * @param {object} [options.env] - Environment variables for the child process.
 * @param {number} [options.timeout] - **Not natively supported here** — use external timeout logic if needed.
 * @param {string|Array} [options.stdio] - stdio configuration (overridable; default is ['inherit', 'pipe', 'pipe']).
 *
 * @returns {Promise<{
 *   code: number,
 *   signal: string|undefined,
 *   stdout: string,
 *   stderr: string
 * }>}
 *   Resolves with the result object on success (code === 0),
 *   rejects with an Error (containing .code, .signal, .stdout, .stderr) on failure.
 *
 * @example
 * ```js
 * await processCommand('ls', ['-la']);
 * await processCommand('echo "Hello" | tr "[:lower:]" "[:upper:]"');
 * await processCommand('node', ['--version'], { shell: false });
 * ```
 *
 * @throws {Error} When the command fails to spawn or exits with non-zero code
 */
export function processCommand(cmd, args = [], options = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, {
      shell: true,
      stdio: ['inherit', 'pipe', 'pipe'],
      ...options,
    });

    let stdoutData = '';
    let stderrData = '';

    child.stdout.on('data', (chunk) => {
      const str = chunk.toString();
      stdoutData += str;
      console.log(str);
    });

    child.stderr.on('data', (chunk) => {
      const str = chunk.toString();
      stderrData += str;
      console.error(str);
    });

    child.on('error', (err) => {
      reject(err);
    });

    child.on('close', (code, signal) => {
      const result = {
        code: code ?? 1,
        signal,
        stdout: stdoutData,
        stderr: stderrData,
      };

      if (code === 0) {
        resolve(result);
      } else {
        const err = new Error(`Command failed with code ${code}${signal ? ` (signal: ${signal})` : ''}`);
        Object.assign(err, result);
        reject(err);
      }
    });
  });
}
