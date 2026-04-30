#!/bin/bash
# test/verify_all.sh — comprehensive verification for git2.wasm
#
# Usage:
#   test/verify_all.sh                # run all tests
#   test/verify_all.sh --list         # list test categories
#   test/verify_all.sh --category init   # run specific category
#
# Environment variables:
#   WASMTIME   — wasmtime path
#   GIT2_WASM  — git2.wasm path
#   KEEP_TMP   — keep temp dir on failure
#   VERBOSE    — verbose output
#   SKIP_NET   — set to y to skip network tests

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/helper.sh"

CATEGORIES=(
    "version"
    "init"
    "config"
    "hash-object"
    "cat-file"
    "add"
    "status"
    "clone-local"
    "clone-https"
    "blame"
)

# ========================= Version =========================

test_version() {
    plan 3

    git2_run --version
    is "$_EXIT" "0" "git2 --version exits 0"
    like "$_STDOUT" "git2 version" "--version output contains version string"

    git2_run help
    like "$_STDOUT" "clone" "help lists available commands"
}

# ========================= Init =========================

test_init() {
    plan 7

    # Basic init
    git2_run init /repo1
    is "$_EXIT" "0" "git init succeeds"
    like "$_STDOUT" "Initialized empty" "init prints confirmation"
    [[ -d "$_TEST_TMPDIR/repo1/.git" ]] && pass ".git directory exists" || fail ".git directory exists"
    [[ -d "$_TEST_TMPDIR/repo1/.git/objects" ]] && pass ".git/objects exists" || fail ".git/objects exists"
    [[ -d "$_TEST_TMPDIR/repo1/.git/refs" ]] && pass ".git/refs exists" || fail ".git/refs exists"

    # Quiet init
    git2_run init -q /repo2
    is "$_EXIT" "0" "git init -q succeeds"
    unlike "$_STDOUT" "Initialized" "quiet init produces no output"

    # Init in existing dir
    mkdir -p "$_TEST_TMPDIR/repo3"
    git2_run init /repo3
    is "$_EXIT" "0" "git init in existing dir succeeds"
}

# ========================= Config =========================

test_config() {
    plan 5

    # Create repo and run config inside it
    git2_run init /repo
    git2_run_in repo config --list
    is "$_EXIT" "0" "config --list in repo succeeds"

    # Set config value (--add required by git2 CLI)
    git2_run_in repo config --add user.name "Alice"
    is "$_EXIT" "0" "config --add user.name succeeds"

    # Get it back
    git2_run_in repo config --get user.name
    like "$_STDOUT" "Alice" "config --get retrieves value"

    # Nonexistent key
    git2_run_in repo config --get nonexistent.key.xyz
    cmp_ok "$_EXIT" "!=" "0" "config --get nonexistent key returns non-zero"

    # Set email
    git2_run_in repo config --add user.email "alice@test.com"
    is "$_EXIT" "0" "config --add user.email succeeds"
}

# ========================= Hash-object =========================

test_hash_object() {
    plan 5

    git2_run init /repo
    mkfile repo/hello.txt "hello world"

    # Hash a file
    git2_run_in repo hash-object hello.txt
    is "$_EXIT" "0" "hash-object succeeds"
    like "$_STDOUT" "^[0-9a-f]{40}$" "hash-object outputs 40-char SHA1"

    # Hash with -w
    git2_run_in repo hash-object -w hello.txt
    is "$_EXIT" "0" "hash-object -w succeeds"
    local sha="$_STDOUT"

    # Hash from stdin
    echo "test content" | "$WASMTIME" $_WASM_FLAGS \
        --dir="$_TEST_TMPDIR/repo::/" \
        "$GIT2_WASM" hash-object --stdin \
        >"$_TEST_TMPDIR/_stdout" 2>"$_TEST_TMPDIR/_stderr"
    _EXIT=$?
    _STDOUT="$(cat "$_TEST_TMPDIR/_stdout")"
    like "$_STDOUT" "^[0-9a-f]{40}$" "hash-object --stdin outputs SHA1"
    is "$_EXIT" "0" "hash-object --stdin succeeds"
}

# ========================= Cat-file =========================

test_cat_file() {
    plan 6

    git2_run init /repo
    mkfile repo/test.txt "test content for cat-file"
    git2_run_in repo hash-object -w test.txt
    local sha="$_STDOUT"

    # Type
    git2_run_in repo cat-file -t "$sha"
    is "$_EXIT" "0" "cat-file -t succeeds"
    is "$_STDOUT" "blob" "cat-file -t reports blob type"

    # Size
    git2_run_in repo cat-file -s "$sha"
    is "$_EXIT" "0" "cat-file -s succeeds"
    cmp_ok "$_STDOUT" ">" "0" "cat-file -s reports positive size"

    # Pretty-print
    git2_run_in repo cat-file -p "$sha"
    is "$_EXIT" "0" "cat-file -p succeeds"
    is "$_STDOUT" "test content for cat-file" "cat-file -p shows blob content"
}

# ========================= Add =========================

test_add() {
    plan 5

    git2_run init /repo
    git2_run_in repo config --add user.name "Test"
    git2_run_in repo config --add user.email "test@test.com"
    mkfile repo/file1.txt "content1"
    mkfile repo/file2.txt "content2"

    # Add single file
    git2_run_in repo add file1.txt
    is "$_EXIT" "0" "git add single file succeeds"

    # Add second file
    git2_run_in repo add file2.txt
    is "$_EXIT" "0" "git add second file succeeds"

    # Verify staged via status
    git2_run_in repo status -s
    like "$_STDOUT" "A  file1.txt" "file1 shows as staged"
    like "$_STDOUT" "A  file2.txt" "file2 shows as staged"

    # Add nonexistent file
    git2_run_in repo add nonexistent.txt
    cmp_ok "$_EXIT" "!=" "0" "add nonexistent file fails"
}

# ========================= Status =========================

test_status() {
    plan 5

    # Empty repo
    git2_run init /repo
    git2_run_in repo status
    is "$_EXIT" "0" "status on empty repo succeeds"

    # With untracked file
    mkfile repo/hello.txt "hello"
    git2_run_in repo status
    like "$_STDOUT" "Untracked files" "status shows untracked header"
    like "$_STDOUT" "hello.txt" "status shows untracked filename"

    # Short format
    git2_run_in repo status -s
    like "$_STDOUT" "\\?\\?" "short status shows ??"
    like "$_STDOUT" "hello.txt" "short status shows filename"
}

# ========================= Clone (local) =========================

test_clone_local() {
    plan 5

    if ! command -v git &>/dev/null; then
        skip "native git not available" 5
        return
    fi

    # Create source repo with native git
    mkdir -p "$_TEST_TMPDIR/src"
    mkfile src/README.md "# Test Project"
    (
        cd "$_TEST_TMPDIR/src"
        git init --initial-branch=main 2>/dev/null
        git config user.name "test" 2>/dev/null
        git config user.email "test@test.com" 2>/dev/null
        git add . 2>/dev/null
        git commit -m "initial" 2>/dev/null
    )

    # Clone with file:// — WASI paths are under mapped root "/"
    git2_run clone "file:///src" /dst
    is "$_EXIT" "0" "git clone file:// succeeds"
    [[ -d "$_TEST_TMPDIR/dst/.git" ]] && pass "clone creates .git directory" || fail "clone creates .git directory"
    [[ -f "$_TEST_TMPDIR/dst/README.md" ]] && pass "clone checks out files" || fail "clone checks out files"

    # Verify content
    local content
    content="$(cat "$_TEST_TMPDIR/dst/README.md")"
    is "$content" "# Test Project" "cloned file content matches"

    # Clone into second dir
    git2_run clone "file:///src" /dst2
    is "$_EXIT" "0" "clone into new dir succeeds"
}

# ========================= Clone (HTTPS) =========================

test_clone_https() {
    plan 4

    if [[ "${SKIP_NET:-}" == "y" ]]; then
        skip "network tests disabled" 4
        return
    fi

    local repo_url="https://gitee.com/yusangeng/packi-template-tslib.git"

    git2_run_net clone "$repo_url" /https-clone
    is "$_EXIT" "0" "git clone https:// succeeds"
    [[ -d "$_TEST_TMPDIR/https-clone/.git" ]] && pass "HTTPS clone creates .git" || fail "HTTPS clone creates .git"
    [[ -d "$_TEST_TMPDIR/https-clone/.git/objects" ]] && pass "HTTPS clone has objects" || fail "HTTPS clone has objects"

    # Verify files were checked out
    if [[ -d "$_TEST_TMPDIR/https-clone" ]]; then
        local file_count
        file_count="$(find "$_TEST_TMPDIR/https-clone" -not -path '*/.git/*' -type f 2>/dev/null | wc -l | tr -d ' ')"
        cmp_ok "$file_count" ">" "0" "HTTPS clone checks out files"
    else
        fail "HTTPS clone checks out files"
    fi
}

# ========================= Blame =========================

test_blame() {
    plan 3

    if ! command -v git &>/dev/null; then
        skip "native git not available" 3
        return
    fi

    # Create repo with history using native git
    mkdir -p "$_TEST_TMPDIR/blame-repo"
    printf "line 1\nline 2\nline 3\n" > "$_TEST_TMPDIR/blame-repo/file.txt"
    (
        cd "$_TEST_TMPDIR/blame-repo"
        git init --initial-branch=main 2>/dev/null
        git config user.name "test" 2>/dev/null
        git config user.email "test@test.com" 2>/dev/null
        git add . 2>/dev/null
        git commit -m "initial" 2>/dev/null
    )

    git2_run_in blame-repo blame file.txt
    is "$_EXIT" "0" "git blame succeeds"
    like "$_STDOUT" "[0-9a-f]" "blame output contains commit hashes"

    git2_run_in blame-repo blame -p file.txt
    is "$_EXIT" "0" "git blame -p (porcelain) succeeds"
}

# ========================= Runner =========================

list_categories() {
    echo "Available test categories:"
    for cat in "${CATEGORIES[@]}"; do
        echo "  $cat"
    done
}

run_category() {
    local cat="$1"
    echo "# === $cat ==="
    case "$cat" in
        version)      test_version ;;
        init)         test_init ;;
        config)       test_config ;;
        hash-object)  test_hash_object ;;
        cat-file)     test_cat_file ;;
        add)          test_add ;;
        status)       test_status ;;
        clone-local)  test_clone_local ;;
        clone-https)  test_clone_https ;;
        blame)        test_blame ;;
        *)
            echo "BAIL OUT! unknown category: $cat" >&2
            exit 1
            ;;
    esac
}

main() {
    setup

    if [[ "${1:-}" == "--list" ]]; then
        list_categories
        teardown
        exit 0
    fi

    if [[ "${1:-}" == "--category" ]]; then
        if [[ -z "${2:-}" ]]; then
            echo "Usage: $0 --category <name>" >&2
            list_categories
            teardown
            exit 1
        fi
        run_category "$2"
        teardown
        exit 0
    fi

    echo "TAP version 14"
    echo "# git2.wasm verification"
    echo "# wasmtime: $WASMTIME"
    echo "# git2:     $GIT2_WASM"
    echo ""

    if [[ ! -x "$WASMTIME" ]]; then
        echo "BAIL OUT! wasmtime not found at $WASMTIME" >&2
        exit 1
    fi
    if [[ ! -f "$GIT2_WASM" ]]; then
        echo "BAIL OUT! git2.wasm not found at $GIT2_WASM" >&2
        exit 1
    fi

    for cat in "${CATEGORIES[@]}"; do
        echo ""
        run_category "$cat"
    done

    echo ""
    echo "# All tests complete."
}

main "$@"
