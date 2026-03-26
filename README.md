# ion

> A simple, local-first version control system.

**Version:** v0.3.2 &nbsp;|&nbsp; C++17 &nbsp;|&nbsp; No dependencies &nbsp;|&nbsp; MIT License

---

ion is built for developers who want version control that is **predictable, readable, and stays out of your way**.

No staging area. No index. No detached HEAD. You save a snapshot, you get it back.

---

## Why I built this

I wanted to understand how version control systems work internally without the complexity of Git.

ion is a learning-first system that focuses on clarity over features.

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

## Philosophy

- **Local-first** — works fully offline, no remote required
- **Predictable** — no hidden behavior, no surprising side effects
- **Transparent** — `.ion` is plain text you can read by hand
- **Reliable** — no silent failures, clear error messages everywhere

---

## Install

### Quick Install (macOS / Linux)

```bash
curl -sSL https://raw.githubusercontent.com/bharadwajsanket/ion/main/install.sh | bash
```

### Manual Build

```bash
git clone https://github.com/bharadwajsanket/ion.git
cd ion
g++ -std=c++17 -o ion src/main.cpp
sudo mv ion /usr/local/bin/ion
```

Requires any C++17-compliant compiler (GCC 8+, Clang 7+). No external libraries needed.

---

## Example Workflow

```bash
# Start a repository
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
ion checkout feature

echo "new feature" > feature.txt
ion save "add feature"

# Switch back to main
ion checkout main
```

---

## Commands

### Repository

| Command | Description |
|---|---|
| `ion init` | Initialize a new repository in the current directory |

### Snapshots

| Command | Description |
|---|---|
| `ion save <message>` | Save a snapshot of the current working directory |
| `ion save <message> --confirm` | Same, but prompts for confirmation first |
| `ion restore <commit>` | Restore working directory to a previous commit |

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
| `ion branch <n>` | Create a new branch from the current position |
| `ion branches` | List all branches with their latest commit |
| `ion checkout <branch>` | Switch to a branch (warns if you have unsaved changes) |

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
  HEAD                  ← current branch name
  branches/
    main                ← points to latest commit on main
    dev                 ← points to latest commit on dev
  commits/
    1                   ← commit record (plain text, human-readable)
    2
  objects/
    files/
      a3f9c2d1...       ← file content, deduplicated by hash
```

Commit files are plain text. Objects are content-addressable — identical files are stored once regardless of how many commits reference them. You can inspect any of this directly without special tooling.

---

## Current Status

Core functionality is stable and usable for real projects.

**Working today:**
- Init, save, restore
- Status and diff (with context)
- Branching and checkout
- Commit history and inspection (`show`, `log --oneline`)
- `.ionignore` support

**Planned:**
- Merge
- Remote sync
- Tags

---

## License

MIT — see `LICENSE` for details.
