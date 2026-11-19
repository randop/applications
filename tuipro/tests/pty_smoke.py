#!/usr/bin/env python3
"""Pseudo-terminal smoke test for tuipro.

Checks that the real terminal path:
  * enters the alternate screen and enables mouse/paste reporting,
  * puts the terminal in raw mode,
  * accepts key input,
  * reacts to a window-size change (SIGWINCH), and
  * exits cleanly on "q".
"""
import fcntl
import os
import pty
import re
import select
import signal
import struct
import sys
import termios
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get("TUIPRO_BIN", os.path.join(ROOT, "build", "tuipro"))
SCRIPT = os.path.join(ROOT, "tests", "pty_app.lua")
TIMEOUT = 8.0


def set_size(fd, rows, cols):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def strip_ansi(data):
    return re.sub(rb"\x1b\[[0-9;?]*[ -/]*[@-~]", b"", data)


def main():
    master, slave = pty.openpty()
    set_size(slave, 24, 80)

    pid = os.fork()
    if pid == 0:
        try:
            os.close(master)
            os.setsid()
            fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
            for fd in (0, 1, 2):
                os.dup2(slave, fd)
            if slave > 2:
                os.close(slave)
            env = dict(os.environ)
            env.setdefault("TERM", "xterm-256color")
            env["TUIPRO_RUNTIME"] = ROOT
            os.chdir(ROOT)
            os.execvpe(BIN, [BIN, "--fps", "60", SCRIPT], env)
        except Exception as exc:  # pragma: no cover - child failure path
            os.write(2, ("pty child: %r\n" % (exc,)).encode())
            os._exit(127)

    output = bytearray()
    deadline = time.time() + TIMEOUT

    def read_until(needle, timeout=TIMEOUT):
        nonlocal deadline
        end = time.time() + timeout
        while time.time() < end:
            ready, _, _ = select.select([master], [], [], 0.1)
            if not ready:
                continue
            try:
                chunk = os.read(master, 65536)
            except OSError:
                return False
            if not chunk:
                return False
            output.extend(chunk)
            if needle in output or needle in strip_ansi(output):
                return True
        return False

    failures = []

    def check(cond, msg):
        if not cond:
            failures.append(msg)
            print("FAIL " + msg)
        else:
            print("ok   " + msg)

    try:
        check(read_until(b"PTY-SMOKE"), "renders the application")
        check(b"\x1b[?1049h" in output, "enters the alternate screen")
        check(b"\x1b[?1000h" in output or b"\x1b[?1006h" in output,
              "enables mouse reporting")
        check(b"\x1b[?2004h" in output, "enables bracketed paste")

        # The child should have put the slave terminal into raw mode.
        attrs = termios.tcgetattr(slave)
        lflag = attrs[3]
        check(not (lflag & termios.ICANON), "disables canonical input")
        check(not (lflag & termios.ECHO), "disables terminal echo")
        check(not (lflag & termios.ISIG), "disables signal-generating keys")

        os.write(master, b"x")
        check(read_until(b"KEY-OK 1"), "receives key input")

        set_size(slave, 20, 60)
        os.kill(pid, signal.SIGWINCH)
        check(read_until(b"size 60x20"), "handles SIGWINCH resize")

        os.write(master, b"q")
        end = time.time() + TIMEOUT
        status = None
        while time.time() < end:
            done, st = os.waitpid(pid, os.WNOHANG)
            if done == pid:
                status = st
                break
            time.sleep(0.05)
        if status is None:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            check(False, "exits after q")
        else:
            code = os.waitstatus_to_exitcode(status) if hasattr(
                os, "waitstatus_to_exitcode") else (
                    os.WEXITSTATUS(status) if os.WIFEXITED(status) else -1)
            check(code == 0, "exits after q (status %s)" % code)
    finally:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.close(slave)
        except OSError:
            pass
        try:
            os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            pass

    if failures:
        print("\nPTY smoke failed: %d check(s)" % len(failures))
        print("--- terminal output (stripped) ---")
        sys.stdout.buffer.write(strip_ansi(output)[-4000:])
        print()
        return 1
    print("PTY smoke passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
