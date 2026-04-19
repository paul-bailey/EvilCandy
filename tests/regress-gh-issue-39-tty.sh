#!/bin/sh

# A regression test for gh issue 39.
# A line like the following
#
#   if (1) print('hi');}
#
# was not throwing a syntax error over the invalid right brace while in
# TTY mode, due to how it tore down the state machine after executing
# the first statement.  This test acts like a TTY, since the problem
# never existed in script/string mode.


set -eu

if ! command -v python3 >/dev/null 2>&1; then
    echo "$0: python3 not found, skipping PTY regression" >&2
    exit 77
fi

python3 <<'PY'
import os
import pty
import select
import subprocess
import sys
import time

cmd = ["./evilcandy"]

master, slave = pty.openpty()
proc = subprocess.Popen(
    cmd,
    stdin=slave,
    stdout=slave,
    stderr=slave,
    close_fds=True,
)
os.close(slave)

def read_until_idle(timeout=3.0, idle=0.2):
    out = bytearray()
    deadline = time.time() + timeout
    last_read = time.time()

    while time.time() < deadline:
        r, _, _ = select.select([master], [], [], 0.05)
        if r:
            try:
                chunk = os.read(master, 4096)
            except OSError:
                break
            if not chunk:
                break
            out.extend(chunk)
            last_read = time.time()
        elif time.time() - last_read >= idle:
            break

    return out.decode("utf-8", "replace")

# Drain initial prompt.
read_until_idle()

os.write(master, b'1; 2;\n')
out1 = read_until_idle()

os.write(master, b'if (true) { print("ran"); } }\n')
out2 = read_until_idle()

os.write(master, b'\x04')
read_until_idle()
proc.wait(timeout=3)
os.close(master)

combined = out1 + out2

checks = [
    ("first statement result", "1" in out1),
    ("saved same-line statement result", "2" in out1),
    ("valid leading if statement executed", "ran" in out2),
    ("bad saved closing brace reported", "SyntaxError" in out2 and "Unexpected token }" in out2),
]

failed = [name for name, ok in checks if not ok]
if failed:
    sys.stderr.write("PTY regression failed: " + ", ".join(failed) + "\n")
    sys.stderr.write("----- output -----\n")
    sys.stderr.write(combined)
    sys.stderr.write("\n------------------\n")
    sys.exit(1)
PY

