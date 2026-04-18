#!/bin/sh

set -eu

tmp_script="${TMPDIR:-/tmp}/evc-rethrow-trace-$$.evc"
tmp_err="${TMPDIR:-/tmp}/evc-rethrow-trace-$$.err"

trap 'rm -f "$tmp_script" "$tmp_err"' EXIT

cat > "$tmp_script" <<'EOF'
function f() {
    throw RuntimeError("original failure");
}

try {
    f();
} catch (e) {
    throw e;
}
EOF

./evilcandy "$tmp_script" 2>"$tmp_err" >/dev/null || true

grep -q 'RuntimeError original failure' "$tmp_err"
grep -q 'throw RuntimeError("original failure")' "$tmp_err"

if grep -q 'line:     throw e;' "$tmp_err"; then
    echo "rethrow traceback points at catch block" >&2
    cat "$tmp_err" >&2
    exit 1
fi
