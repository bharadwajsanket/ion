# ion

> A local-first version control system.

**Version:** v0.4.0 &nbsp;|&nbsp; C++17 &nbsp;|&nbsp; No dependencies &nbsp;|&nbsp; GPL-3.0-only License

---

ion is built for developers who want version control that is **predictable, honest, and stays out of your way**.

No staging area. No index. No detached HEAD. You save a snapshot, you get it back.

---

## Philosophy

- **Local-first** — works fully offline, no remote required
- **Predictable** — no hidden behavior, no surprising side effects
- **Transparent** — `.ion` is plain text you can read by hand
- **Reliable** — no silent failures, clear error messages everywhere
- **Fail loudly** — every filesystem error is checked and reported
- **Never destroy unsaved work** — restore and checkout verify safety before touching the working directory
- **No interactive prompts** — commands succeed or fail deterministically; ion never reads from stdin

---

## Why not Git?

ion is not trying to replace Git.

Git is powerful, but it comes with real complexity: staging areas, the index, rebasing, detached HEAD states, and behavior that can be genuinely difficult to reason about — especially under pressure.

ion is designed for a different use case:

- Small to medium projects
- Solo developers or small teams
- Anyone who wants a tool that behaves exactly as described

If Git sometimes feels like too much, ion is a focused alternative that stays simple and honest.

---

## Background

I built ion to understand how version control systems work internally, without the complexity of Git. Over time it became a genuinely useful tool, and v0.4 makes it ready for real projects.

---

## What's New in v0.4

- **Safe restore and checkout** — both commands refuse to run if unsaved work would be destroyed. No data is ever lost silently.
- **`ion verify`** — read-only integrity check. Walks every branch, commit, and object and reports every defect it finds.
- **`ion branch-delete`** — delete branches you no longer need. Commits and objects are always preserved.
- **`ion switch`** — a natural alias for `checkout`.
- **Filename support** — filenames containing spaces and special characters work correctly end-to-end.
- **No interactive prompts** — ion never reads from stdin. Commands succeed or fail explicitly.
- **Deterministic hashing** — FNV-1a 64-bit ensures identical hash values across all platforms and compilers.
- **Hardened error handling** — every filesystem operation is checked. ion fails loudly rather than producing silent corrupted state.

---

## Upgrading from v0.3

> **⚠️ Breaking Change — Repository Format**
>
> v0.4 changed the internal commit file format and hashing algorithm.
> Repositories created with v0.3 are **not compatible** with v0.4.

If you have existing v0.3 repositories:

1. Back up your working files.
2. Delete the `.ion` directory.
3. Run `ion init` to create a fresh v0.4 repository.
4. Run `ion save "initial"` to create your first v0.4 snapshot.

Commits from v0.3 cannot be migrated automatically.

---

## Install

### Quick Install (macOS / Linux)

```bash
curl -sSL https://raw.githubusercontent.com/bharadwajsanket/ion/main/install.sh | bash
```

Then verify the install worked:

```bash
ion help
```

### Manual Build

```bash
git clone https://github.com/bharadwajsanket/ion.git
cd ion
g++ -std=c++17 -O2 -o ion src/main.cpp
sudo mv ion /usr/local/bin/ion
```

Requires any C++17-compliant compiler (GCC 8+, Clang 7+). No external libraries needed.

---

## Example Workflow

```bash
# Initialize a repository
ion init

# Create some files and save a snapshot
echo "hello" > README.md
ion save "initial commit"

# Make changes, inspect them
echo "world" >> README.md
ion status
ion diff

# Save another snapshot
ion save "update readme"

# Review history
ion log --oneline
ion show 2 --diff

# Branch off for a new feature
ion branch feature
ion switch feature

echo "new feature" > feature.txt
ion save "add feature"

# Switch back to main
ion switch main

# Verify repository integrity
ion verify
```

---

## Commands

### Repository

| Command | Description |
|---|---|
| `ion init` | Initialize a new repository in the current directory |
| `ion verify` | Check repository integrity. Read-only — makes no changes. |

### Snapshots

| Command | Description |
|---|---|
| `ion save <message>` | Save a snapshot of the current working directory |
| `ion restore <commit>` | Restore working directory to a commit. Fails if unsaved work would be lost. |

### Inspection

| Command | Description |
|---|---|
| `ion status` | Show modified, untracked, and deleted files |
| `ion status --ignored` | Also show files matched by `.ionignore` |
| `ion diff` | Show line-by-line changes since the last commit |
| `ion diff <file>` | Diff a specific file |
| `ion history` | Full commit history for the current branch |
| `ion log --oneline` | Compact one-line history view |
| `ion show <commit>` | Show metadata and file list for a commit |
| `ion show <commit> --diff` | Same, with an inline line diff |

### Branches

| Command | Description |
|---|---|
| `ion branch <name>` | Create a new branch from the current position |
| `ion branches` | List all branches with their latest commit |
| `ion checkout <branch>` | Switch to a branch. Fails if unsaved work would be lost. |
| `ion switch <branch>` | Alias for `checkout` |
| `ion branch-delete <name>` | Delete a branch. Commits and objects are kept. |

**Commit IDs** are sequential integers starting from `1`. Use them with `ion show`, `ion restore`, and `ion log`.

---

## Branch Names

Branch names may only contain letters, digits, `-`, `_`, and `.`.
Names must not start with `.` or contain `..`.

| Valid | Invalid |
|---|---|
| `main` | `feat/login` (forward slashes not allowed) |
| `feature-login` | `.hidden` (cannot start with `.`) |
| `v0.4` | `a..b` (cannot contain `..`) |
| `hotfix_2` | `my branch` (spaces not allowed) |

---

## Safe Restore and Checkout

Both `ion restore` and `ion checkout` perform a safety check before modifying the working directory.

If any file has unsaved changes that would be destroyed by the operation, the command prints a list of those files and exits without making any changes:

```
error: restore would destroy unsaved work:
  notes.txt
  draft.md
Save your changes first: ion save "..."
```

No files are ever removed before the check completes. To proceed, save your work first with `ion save`, then retry.

---

## Ignoring Files

Create a `.ionignore` file in your project root:

```
# Ignore a directory
build/

# Ignore by extension
*.log
*.o

# Ignore a specific file
secret.txt
```

---

## How ion Stores Data

Everything lives in a `.ion` folder at the root of your project:

```
.ion/
  HEAD                     ← current branch name
  branches/
    main                   ← points to latest commit on main
    dev                    ← points to latest commit on dev
  commits/
    1                      ← commit record (plain text, human-readable)
    2
  objects/
    files/
      805e0693c04b3b30     ← file content, deduplicated by hash
      29e570a4913ba3bd
```

Commit files are plain text. You can read them directly:

```
id: 2
parent: 1
timestamp: 1780432542
message: update readme
files:
	782e1488cd5a68b7 README.md
	29e570a4913ba3bd config.txt
```

Objects are content-addressable — identical files are stored once regardless of how many commits reference them. Hashes are 16-character FNV-1a 64-bit values, deterministic across all platforms.

---

## Limitations

- **Local only** — no remotes, no push, no pull, no sync.
- **No merge** — branching is supported; merging branches is not yet implemented.
- **Large files** — the diff engine uses O(N×M) memory. Very large text files (10,000+ lines) may use significant memory during `diff` and `show --diff`.
- **Binary files** — tracked and restored correctly, but diff output is suppressed (no meaningful line diff for binary content).

---

## Current Status

Core functionality is stable and usable for real projects.

**Working in v0.4:**

- Init, save, restore (with safe pre-flight check)
- Status and diff (with context lines)
- Branching, checkout, switch, branch-delete
- Commit history and inspection (`history`, `log --oneline`, `show`, `show --diff`)
- `.ionignore` support
- Repository integrity verification (`verify`)
- Full support for filenames with spaces and special characters

**Planned:**

- Merge
- Tags

---

## License

GPL-3.0-only — see `LICENSE` for details.
