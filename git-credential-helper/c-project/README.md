# git-credential-helper

A full-featured Git credential helper written in C, using
**[argtable3](https://github.com/argtable/argtable3)** — the most actively
maintained ANSI-C CLI argument-parsing library — bundled as a single-file
amalgamation so there are zero external dependencies.

---

## Features

| Feature | Detail |
|---|---|
| Git protocol | Full `get` / `store` / `erase` support |
| File store | `~/.git-credentials-store` (mode 0600) |
| Base64 passwords | Special characters handled safely |
| Atomic writes | `rename()` ensures no partial files |
| Rich CLI | `add`, `list`, `remove`, `config` subcommands |
| Password prompt | `termios`-based echo-off prompt |
| Filtering | `list --filter` by hostname |
| Custom store | `--store-path` or `GIT_CREDENTIAL_STORE` env |
| No deps | argtable3 bundled, C11, POSIX only |

---

## Build

```bash
make          # produces ./git-credential-helper
make install  # copies to /usr/local/bin
make test     # runs integration tests
```

Requires: `gcc` (or any C11 compiler), POSIX system.

---

## Register with Git

```bash
git config --global credential.helper '/usr/local/bin/git-credential-helper'
```

Git will now call `git-credential-helper get|store|erase` automatically.

---

## Direct Management Commands

### Add / update a credential

```bash
git-credential-helper add --host github.com --username alice
# prompts for password interactively (echo off)

git-credential-helper add -H github.com -u alice -p mytoken --protocol https
```

### List stored credentials

```bash
git-credential-helper list
git-credential-helper list --show-passwords
git-credential-helper list --filter github.com
```

### Remove a credential

```bash
git-credential-helper remove --host github.com
git-credential-helper remove --host github.com --username alice --force
```

### Configuration

```bash
git-credential-helper config --show
git-credential-helper config --store-path /secure/path/my-creds
```

Custom path can also be set via environment:

```bash
export GIT_CREDENTIAL_STORE=/secure/path/my-creds
```

---

## Store Format

One entry per line, tab-separated:

```
PROTOCOL<TAB>HOST<TAB>USERNAME<TAB>PASSWORD_BASE64<TAB>PATH
```

The file is created with mode **0600** (owner read/write only).

---

## Architecture

```
git-credential-helper/
├── include/
│   ├── argtable3.h          # argtable3 public API
│   └── credential_store.h   # credential store API
├── lib/
│   └── argtable3.c          # argtable3 amalgamation
├── src/
│   ├── credential_store.c   # file-backed CRUD + git I/O
│   └── main.c               # CLI subcommands
└── Makefile
```

---

## Why argtable3?

- **Most maintained** C CLI library — active GitHub repo, semantic versioning
- Official **single-file amalgamation** distribution (no install needed)
- Generates **GNU-style** `--help` output automatically
- Supports `--long`, `-s`, positional args, types, min/max counts
- Used by many production C projects
