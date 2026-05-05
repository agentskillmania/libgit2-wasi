# libgit2-wasi

libgit2 v1.9.2 compiled for **WASI Preview2** (wasm32-wasip2 target).

This is a fork of [libgit2/libgit2](https://github.com/libgit2/libgit2) with patches to build as a WASM library and CLI, used for Git operations in sandboxed environments. HTTPS is backed by [mbedtls-wasi](https://github.com/agentskillmania/mbedtls-wasi).

## What changed

14 commits on top of upstream `v1.9.2`:

### WASI platform adaptation

| Commit | Description |
|--------|-------------|
| `wasi-preview2` | Disable file ownership validation (no user IDs in WASI). Exclude `unix/process.c` (needs fork/pipe). Add stub headers `pwd.h`, `sys/wait.h`. Inline `lock_file()` to work around WASM memory fault. |
| `rmdir EINVAL/ENOSYS` | Handle `rmdir()` returning `EINVAL` or `ENOSYS` on WASI. |
| `HTTPS clone` | Skip certificate verification (no CA store in WASI). Preserve mbedTLS error messages. |
| `hashfd crash` | Fix `hashfd` crash on WASI by handling missing `mmap` gracefully. |

### Git CLI commands (23 commands)

| Phase | Commands |
|-------|----------|
| Basic | `status`, `add` |
| Commit | `commit` (with `-m`, `-q`, `--amend`) |
| History | `log` (default and `--oneline`), `show`, `diff`, `blame` |
| Branch/Tag | `branch`, `tag`, `checkout` |
| Remote | `remote`, `fetch`, `push`, `pull` |
| Advanced | `reset`, `stash`, `clone`, `init`, `config`, `cat-file`, `hash-object` |

### Component Model

| Commit | Description |
|--------|-------------|
| `rename main()` | Rename `main()` to `git2_cli_main()` so the CLI can be embedded as a library. |
| `component model` | Add `git-guest.wasm` — a WASI Component exporting the WIT `subcommand` interface. Any host runner can compose and invoke it. |
| `WIT definition` | Self-contained `wit/subcommand.wit` defining the guest/host contract. |

### mbedTLS entropy

| Commit | Description |
|--------|-------------|
| `entropy registration` | *(removed)* Previously required manual entropy source registration because `mbedtls_entropy_init()` did not auto-register under `MBEDTLS_NO_PLATFORM_ENTROPY`. Fixed upstream in [mbedtls-wasi](https://github.com/agentskillmania/mbedtls-wasi); libgit2 now relies on the automatic registration. |

## Build

### Prerequisites

- [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) 22+ at `$HOME/wasi-sdk`
- [mbedtls-wasi](https://github.com/agentskillmania/mbedtls-wasi) built at `../mbedtls/` (run `mbedtls/build_wasi.sh` first)
- cmake, make
- (Optional) [wit-bindgen](https://github.com/bytecodealliance/wit-bindgen) 0.57+ for Component Model build

### CLI build

```bash
bash build_wasi.sh
```

Output: `build-wasi/`

- `git2.wasm` — standalone WASI CLI (~6 MB)
- `libgit2.a` — static library for linking into other WASM modules

Run:
```bash
wasmtime -S inherit-network=yes build-wasi/git2.wasm help
wasmtime -S inherit-network=yes build-wasi/git2.wasm clone https://github.com/example/repo.git
```

### Component Model build

```bash
bash build_component.sh
```

Output: `build-component/git-guest.wasm` (~5.3 MB)

A WASM Component exporting the WIT `subcommand` interface:
```wit
interface subcommand {
    execute: func(args: list<string>) -> s32;
}
```

Any host that imports `subcommand` can compose and run git commands:
```bash
wasmtime -W component-model -S inherit-network=yes build-component/git-guest.wasm help
```

## Branch

`main` — based on upstream `v1.9.2` tag + WASI patches.

## Related projects

- [agentskillmania/mbedtls-wasi](https://github.com/agentskillmania/mbedtls-wasi) — TLS library for HTTPS support
- [agentskillmania/busybox-wasi](https://github.com/agentskillmania/busybox-wasi) — Shell and component model host runner

## Upstream docs

See [README.libgit2.md](README.libgit2.md) for the original libgit2 documentation.
