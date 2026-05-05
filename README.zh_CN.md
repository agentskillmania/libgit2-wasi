# libgit2-wasi

libgit2 v1.9.2 的 **WASI Preview2** 移植版（wasm32-wasip2 目标）。

本项目是 [libgit2/libgit2](https://github.com/libgit2/libgit2) 的分支，用于在沙箱环境中执行 Git 操作。HTTPS 由 [mbedtls-wasi](https://github.com/agentskillmania/mbedtls-wasi) 提供支持。

## 变更内容

在 upstream `v1.9.2` 基础上增加了 14 个 commit：

### WASI 平台适配

| Commit | 说明 |
|--------|------|
| `wasi-preview2` | 禁用文件所有权验证（WASI 无用户 ID）。排除 `unix/process.c`（需要 fork/pipe）。添加 stub 头文件 `pwd.h`、`sys/wait.h`。内联 `lock_file()` 以规避 WASM 内存故障。 |
| `rmdir EINVAL/ENOSYS` | 处理 WASI 上 `rmdir()` 返回 `EINVAL` 或 `ENOSYS` 的情况。 |
| `HTTPS clone` | 跳过证书验证（WASI 无 CA 存储）。保留 mbedTLS 错误信息。 |
| `hashfd crash` | 修复 WASI 上 `hashfd` 因缺失 `mmap` 导致的崩溃。 |

### Git CLI 命令（23 个）

| 阶段 | 命令 |
|------|------|
| 基础 | `status`、`add` |
| 提交 | `commit`（支持 `-m`、`-q`、`--amend`） |
| 历史 | `log`（默认和 `--oneline`）、`show`、`diff`、`blame` |
| 分支/标签 | `branch`、`tag`、`checkout` |
| 远程 | `remote`、`fetch`、`push`、`pull` |
| 高级 | `reset`、`stash`、`clone`、`init`、`config`、`cat-file`、`hash-object` |

### Component Model

| Commit | 说明 |
|--------|------|
| `rename main()` | 将 `main()` 重命名为 `git2_cli_main()`，使 CLI 可作为库嵌入。 |
| `component model` | 生成 `git-guest.wasm` —— 导出 WIT `subcommand` 接口的 WASI Component，可被任意 host runner 组合调用。 |
| `WIT 定义` | 自包含的 `wit/subcommand.wit`，定义 guest/host 契约。 |

### mbedTLS 熵源

| Commit | 说明 |
|--------|------|
| `entropy registration` | *(已移除)* 早期因 `mbedtls_entropy_init()` 在 `MBEDTLS_NO_PLATFORM_ENTROPY` 下不会自动注册熵源，需要手动添加。现已在 [mbedtls-wasi](https://github.com/agentskillmania/mbedtls-wasi) 中修复，libgit2 依赖自动注册即可。 |

## 构建

### 前置条件

- [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) 22+，位于 `$HOME/wasi-sdk`
- [mbedtls-wasi](https://github.com/agentskillmania/mbedtls-wasi) 已在 `../mbedtls/` 构建完成（先运行 `mbedtls/build_wasi.sh`）
- cmake、make
-（可选）[wit-bindgen](https://github.com/bytecodealliance/wit-bindgen) 0.57+，用于 Component Model 构建

### CLI 构建

```bash
bash build_wasi.sh
```

输出：`build-wasi/`

- `git2.wasm` —— 独立 WASI CLI（约 6 MB）
- `libgit2.a` —— 静态库，可链接到其他 WASM 模块

运行：
```bash
wasmtime -S inherit-network=yes build-wasi/git2.wasm help
wasmtime -S inherit-network=yes build-wasi/git2.wasm clone https://github.com/example/repo.git
```

### Component Model 构建

```bash
bash build_component.sh
```

输出：`build-component/git-guest.wasm`（约 5.3 MB）

导出 WIT `subcommand` 接口的 WASM Component：
```wit
interface subcommand {
    execute: func(args: list<string>) -> s32;
}
```

任何导入 `subcommand` 的 host 都可以组合并运行 git 命令：
```bash
wasmtime -W component-model -S inherit-network=yes build-component/git-guest.wasm help
```

## 分支

`main` —— 基于 upstream `v1.9.2` tag + WASI 补丁。

## 相关项目

- [agentskillmania/mbedtls-wasi](https://github.com/agentskillmania/mbedtls-wasi) —— HTTPS 所需的 TLS 库
- [agentskillmania/busybox-wasi](https://github.com/agentskillmania/busybox-wasi) —— Shell 和 component model host runner

## 上游文档

原始 libgit2 文档见 [README.libgit2.md](README.libgit2.md)。
