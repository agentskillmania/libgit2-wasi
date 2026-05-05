#!/bin/bash
# test/helper.sh — git2.wasm TAP verification framework
#
# Usage:
#   #!/bin/bash
#   source "$(dirname "$0")/../helper.sh"
#   plan 5
#   # ... tests ...
#   done_testing
#
# Key design:
#   git2 CLI has no -C flag. We use --dir=HOST_PATH::/ to map the temp
#   directory as WASI root "/". All paths passed to git2 are under "/".
#   For commands needing a repo context (config, hash-object, etc), we
#   map the specific repo subdirectory as WASI root.
#
# Environment variables:
#   WASMTIME   — wasmtime path (default $HOME/bin/wasmtime)
#   GIT2_WASM  — git2.wasm path (default $PROJ_ROOT/build-wasi/git2.wasm)
#   KEEP_TMP   — set to y to keep temp dir on failure
#   VERBOSE    — set to y for verbose output

set -u

# ========================= Configuration =========================

_PROJ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WASMTIME="${WASMTIME:-$HOME/bin/wasmtime}"
GIT2_WASM="${GIT2_WASM:-$_PROJ_ROOT/build-wasi/git2.wasm}"

# wasmtime flags
_WASM_FLAGS="-W exceptions=y"
_WASM_NET_FLAGS="-S inherit-network=yes -S allow-ip-name-lookup=yes"

# TAP state
_TEST_COUNT=0
_PLANNED_TESTS=0
_PLAN_SET=0
_FAILED=0

# Temp directory
_TEST_TMPDIR=""

# ========================= Temp directory =========================

setup() {
    _TEST_TMPDIR="$(mktemp -d "/tmp/git2test.XXXXXX")"
}

teardown() {
    if [[ -n "$_TEST_TMPDIR" && -d "$_TEST_TMPDIR" ]]; then
        if [[ "${KEEP_TMP:-}" == "y" && _FAILED -gt 0 ]]; then
            echo "# temp dir kept: $_TEST_TMPDIR" >&2
        else
            rm -rf "$_TEST_TMPDIR"
        fi
    fi
}

# ========================= TAP functions =========================

plan() {
    _PLANNED_TESTS="$1"
    _PLAN_SET=1
    echo "1..$_PLANNED_TESTS"
}

done_testing() {
    if [[ $_PLAN_SET -eq 1 && _TEST_COUNT -ne $_PLANNED_TESTS ]]; then
        echo "# WARNING: planned $_PLANNED_TESTS but ran $_TEST_COUNT" >&2
    elif [[ $_PLAN_SET -eq 0 ]]; then
        echo "1..$_TEST_COUNT"
    fi
    teardown
    exit $_FAILED
}

pass() {
    local desc="$1"
    _TEST_COUNT=$(( _TEST_COUNT + 1 ))
    echo "ok $_TEST_COUNT - $desc"
}

fail() {
    local desc="$1"
    _TEST_COUNT=$(( _TEST_COUNT + 1 ))
    _FAILED=$(( _FAILED + 1 ))
    echo "not ok $_TEST_COUNT - $desc"
}

skip() {
    local reason="$1"
    _TEST_COUNT=$(( _TEST_COUNT + 1 ))
    echo "ok $_TEST_COUNT # SKIP $reason"
}

skip_if() {
    local cond="$1"
    local reason="$2"
    if [[ "$cond" -ne 0 ]]; then
        skip "$reason"
        return 0
    else
        return 1
    fi
}

# ========================= Assertions =========================

is() {
    local got="$1" expected="$2" desc="$3"
    if [[ "$got" == "$expected" ]]; then
        pass "$desc"
    else
        fail "$desc"
        echo "#   got:      '$got'" >&2
        echo "#   expected: '$expected'" >&2
    fi
}

isnt() {
    local got="$1" expected="$2" desc="$3"
    if [[ "$got" != "$expected" ]]; then
        pass "$desc"
    else
        fail "$desc"
        echo "#   got:      '$got'" >&2
        echo "#   expected: anything else" >&2
    fi
}

like() {
    local got="$1" pattern="$2" desc="$3"
    if [[ "$got" =~ $pattern ]]; then
        pass "$desc"
    else
        fail "$desc"
        echo "#   got:      '$got'" >&2
        echo "#   expected: match /$pattern/" >&2
    fi
}

unlike() {
    local got="$1" pattern="$2" desc="$3"
    if [[ ! "$got" =~ $pattern ]]; then
        pass "$desc"
    else
        fail "$desc"
        echo "#   got:      '$got'" >&2
        echo "#   expected: no match /$pattern/" >&2
    fi
}

cmp_ok() {
    local left="$1" op="$2" right="$3" desc="$4"
    if eval "[[ \"$left\" $op \"$right\" ]]"; then
        pass "$desc"
    else
        fail "$desc"
        echo "#   comparison: '$left' $op '$right' failed" >&2
    fi
}

# ========================= git2 run helpers =========================

# _capture — internal: run wasmtime, capture stdout/stderr
_capture() {
    local stdout_file="$_TEST_TMPDIR/_stdout"
    local stderr_file="$_TEST_TMPDIR/_stderr"

    "$@" >"$stdout_file" 2>"$stderr_file"
    _EXIT=$?
    _STDOUT="$(cat "$stdout_file")"
    _STDERR="$(cat "$stderr_file")"

    if [[ "${VERBOSE:-}" == "y" ]]; then
        echo "# cmd: $*" >&2
        echo "# exit=$_EXIT" >&2
        [[ -n "$_STDOUT" ]] && echo "# stdout: $_STDOUT" >&2
        [[ -n "$_STDERR" ]] && echo "# stderr: $_STDERR" >&2
    fi
}

# Run git2.wasm with tmpdir mapped as WASI root.
# Use "/" prefixed paths for all arguments (e.g. init /myrepo).
# Usage: git2_run <args...>
# Sets: _EXIT, _STDOUT, _STDERR
git2_run() {
    _capture "$WASMTIME" $_WASM_FLAGS --dir="$_TEST_TMPDIR::/" "$GIT2_WASM" "$@"
}

# Run git2.wasm with network + tmpdir mapped as WASI root.
# Usage: git2_run_net <args...>
# Sets: _EXIT, _STDOUT, _STDERR
git2_run_net() {
    _capture "$WASMTIME" $_WASM_FLAGS $_WASM_NET_FLAGS --dir="$_TEST_TMPDIR::/" "$GIT2_WASM" "$@"
}

# Run git2.wasm with a repo subdirectory mapped as WASI root.
# The git2 CWD "/" resolves to <tmpdir>/<subdir>.
# Usage: git2_run_in <subdir> <args...>
# Sets: _EXIT, _STDOUT, _STDERR
git2_run_in() {
    local subdir="$1"
    shift
    _capture "$WASMTIME" $_WASM_FLAGS --dir="$_TEST_TMPDIR/$subdir::/" "$GIT2_WASM" "$@"
}

# Run git2.wasm in a repo subdir with network access.
# Usage: git2_run_in_net <subdir> <args...>
# Sets: _EXIT, _STDOUT, _STDERR
git2_run_in_net() {
    local subdir="$1"
    shift
    _capture "$WASMTIME" $_WASM_FLAGS $_WASM_NET_FLAGS --dir="$_TEST_TMPDIR/$subdir::/" "$GIT2_WASM" "$@"
}

# Create a file in tmpdir
# Usage: mkfile <path> <content>
#   path is relative to _TEST_TMPDIR
mkfile() {
    local path="$_TEST_TMPDIR/$1"
    mkdir -p "$(dirname "$path")"
    printf '%s' "$2" > "$path"
}
