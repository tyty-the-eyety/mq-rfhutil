# tools/index: local code index (fork only)

A lookup layer for agents and people. It answers "where is X defined, who uses it,
which tests cover it, which doc section covers it" with pointers
(`path:start-end`, a kind, a name, a short snippet) so nobody has to open
25,000-line files to find a function. It never prints whole files.

Standard library Python 3 plus Universal Ctags and ripgrep. The database is
`.index/rfhutil.db` at the repo root (git-ignored, never committed).

## Commands

Run `tools/index/q COMMAND ...` (from anywhere).

| Command | Answers |
|---|---|
| `q sym NAME [--kind K] [--all]` | Where `NAME` is defined (exact, then prefix, then substring). The kind column tells `function` (definition) from `prototype` (declaration). `Class::name` works. `--all` adds members and variables. |
| `q outline PATH` | Symbols of a file in order with ranges; headings for markdown. |
| `q search WORDS... [--in GLOB] [--kind code\|test\|doc] [--limit N] [--brief]` | Ranked full-text search over chunks (one per function/class/section, or about 60-line windows). `getParsedData` is found by `parsed data`. |
| `q refs NAME [--limit N]` | Word-bounded text uses (ripgrep), grouped by file, definition lines skipped. Exact text, not semantic: it cannot tell overloads, scopes or comments apart. `tools/index/` itself is left out of `refs`. |
| `q tests-for NAME` | `TEST(...)` bodies in `tests/` that mention `NAME`. Text match; a test that reaches `NAME` only through a helper is not listed. |
| `q where TOPIC` | Best matching rows and sections of `docs/CODEMAP.md`, then other docs. Start here for "which file do I read". |
| `q read PATH START END` | Exactly those lines, numbered. Refuses over 400 lines without `--force`. |
| `q stats` / `q rebuild` | Counts, age, git HEAD / full rebuild. |

Output lines look like `path:start-end  kind  name  signature`, at most 140 columns.
Use `q read` on a returned range, then read the exact lines before editing.

## Freshness

Every command first compares each indexed file's size and mtime with the working
tree and hashes (SHA-256) only files where those differ. Changed, added and
deleted files are re-indexed incrementally before the answer, so results reflect the
tree, including uncommitted and untracked-but-not-ignored files. Stderr shows
`[refreshed N files]` only when it re-indexed something, and `[built index: ...]` when it
created the database. A schema or tool version change rebuilds automatically.

`--no-refresh` skips the refresh and prints
`[index may be stale: N files changed since <date>]` on stderr when the tree differs.

Indexed: `git ls-files` plus untracked files that are not ignored. Skipped: binaries,
files over 2 MB, `build*/`, `.index/`, and `packages/` (vendored GoogleTest NuGet content).

## Tests

    python3 -m unittest discover tools/index/tests   # fixture tree in a temp dir
    tools/index/smoke.sh                             # a few facts about the real repo

`vectors(chunk_id, model, vector)` exists but is empty: embeddings are not implemented.
