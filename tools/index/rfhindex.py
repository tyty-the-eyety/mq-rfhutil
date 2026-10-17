#!/usr/bin/env python3
"""rfhindex: a local code index for this repository (library and CLI).

Standard library only, plus the external tools Universal Ctags and ripgrep.
The index lives in .index/rfhutil.db at the repository root and is never
committed. See tools/index/README.md.
"""
import argparse
import bisect
import datetime
import fnmatch
import hashlib
import json
import os
import re
import sqlite3
import subprocess
import sys
import time

TOOL_VERSION = "1"
SCHEMA_VERSION = 1
MAX_FILE_BYTES = 2 * 1024 * 1024
LINE_WIDTH = 140
READ_LIMIT = 400
DB_RELPATH = os.path.join(".index", "rfhutil.db")

# Directories (by first path component or any component pattern) that are not indexed.
# packages/ holds vendored NuGet content (GoogleTest headers and binaries).
EXCLUDED_TOP = {".index", ".git", "packages"}
BINARY_EXT = {
    ".png", ".jpg", ".jpeg", ".gif", ".ico", ".bmp", ".cur", ".pdf", ".exe", ".dll", ".lib",
    ".obj", ".o", ".a", ".so", ".zip", ".gz", ".tgz", ".7z", ".pdb", ".ilk", ".res", ".bin",
    ".suo", ".db", ".ttf", ".otf", ".woff", ".woff2", ".mp4", ".wav",
}
CODE_EXT = {".c", ".cc", ".cpp", ".cxx", ".inl"}
HEADER_EXT = {".h", ".hpp", ".hh", ".hxx"}
SCRIPT_EXT = {".pl", ".pm", ".sh", ".ps1", ".py", ".cmd", ".bat"}
DOC_EXT = {".md", ".markdown", ".rst"}
BUILD_EXT = {".cmake", ".vcxproj", ".sln", ".props", ".filters", ".user"}
CTAGS_EXT = CODE_EXT | HEADER_EXT | {".pl", ".pm", ".cmake"}

# Symbol kinds that own a chunk, and kinds hidden unless --all is given.
CHUNK_KINDS = {"function", "class", "struct", "union", "subroutine", "macro_cmake", "section"}
MINOR_KINDS = {"member", "variable", "enumerator", "local", "externvar", "field"}
KIND_ORDER = {"function": 0, "subroutine": 0, "class": 1, "struct": 1, "union": 1, "section": 2,
              "prototype": 3, "enum": 4, "typedef": 4, "macro": 5, "namespace": 6}
FILE_KIND_ORDER = {"code": 0, "header": 1, "script": 2, "build": 3, "test": 4, "doc": 5, "other": 6}
GTEST_MACROS = {"TEST", "TEST_F", "TEST_P", "TYPED_TEST", "TYPED_TEST_P"}

SCHEMA = """
CREATE TABLE files(path TEXT PRIMARY KEY, sha256 TEXT, mtime INTEGER, size INTEGER,
                   lines INTEGER, kind TEXT, skip TEXT);
CREATE TABLE symbols(name TEXT, kind TEXT, file TEXT, line INTEGER, end_line INTEGER,
                     scope TEXT, signature TEXT, language TEXT);
CREATE INDEX symbols_name ON symbols(name COLLATE NOCASE);
CREATE INDEX symbols_file ON symbols(file, line);
CREATE TABLE chunks(id INTEGER PRIMARY KEY, file TEXT, start_line INTEGER, end_line INTEGER,
                    symbol TEXT, kind TEXT);
CREATE INDEX chunks_file ON chunks(file);
CREATE VIRTUAL TABLE chunk_fts USING fts5(body, ident, symbol, path,
                                          tokenize = 'unicode61 remove_diacritics 0');
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE vectors(chunk_id INTEGER PRIMARY KEY, model TEXT, vector BLOB);
"""


class IndexError_(Exception):
    pass


# ----------------------------------------------------------------------------
# File discovery and classification

def find_root(explicit=None):
    if explicit:
        return os.path.abspath(explicit)
    env = os.environ.get("RFHINDEX_ROOT")
    if env:
        return os.path.abspath(env)
    here = os.path.dirname(os.path.realpath(__file__))
    r = subprocess.run(["git", "-C", here, "rev-parse", "--show-toplevel"],
                       capture_output=True, text=True)
    if r.returncode == 0 and r.stdout.strip():
        return r.stdout.strip()
    return os.path.abspath(os.path.join(here, "..", ".."))


def file_kind(path):
    base = os.path.basename(path)
    ext = os.path.splitext(base)[1].lower()
    in_tests = path.startswith("tests/")
    if ext in CODE_EXT:
        return "test" if in_tests else "code"
    if ext in HEADER_EXT:
        return "test" if in_tests else "header"
    if base == "CMakeLists.txt" or base == "CMakePresets.json" or ext in BUILD_EXT or base == "Makefile":
        return "build"
    if ext in SCRIPT_EXT:
        return "script"
    if ext in DOC_EXT or ext == ".txt":
        return "doc"
    return "other"


def excluded(path):
    parts = path.split("/")
    if parts[0] in EXCLUDED_TOP:
        return True
    return any(p.startswith("build") for p in parts[:-1])


def _git_lines(root, args):
    r = subprocess.run(["git", "-C", root] + args, capture_output=True)
    if r.returncode != 0:
        return None
    return [p.decode("utf-8", "replace") for p in r.stdout.split(b"\0") if p]


def scan_tree(root):
    """Return {relpath: (size, mtime_ns)} for candidate files."""
    tracked = _git_lines(root, ["ls-files", "-z"])
    if tracked is None:
        paths = []
        for d, dirs, files in os.walk(root):
            rel = os.path.relpath(d, root)
            dirs[:] = [x for x in dirs if not (x.startswith("build") or x in EXCLUDED_TOP)]
            for f in files:
                paths.append(f if rel == "." else (rel + "/" + f).replace(os.sep, "/"))
    else:
        others = _git_lines(root, ["ls-files", "-z", "--others", "--exclude-standard"]) or []
        paths = set(tracked) | set(others)
    out = {}
    for p in paths:
        if excluded(p):
            continue
        try:
            st = os.lstat(os.path.join(root, p))
        except OSError:
            continue
        if not os.path.isfile(os.path.join(root, p)) or os.path.islink(os.path.join(root, p)):
            continue
        out[p] = (st.st_size, st.st_mtime_ns)
    return out


# ----------------------------------------------------------------------------
# Text helpers

def split_lines(data):
    text = data.decode("utf-8-sig", errors="replace")
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    return [l[:-1] if l.endswith("\r") else l for l in lines]


_ident_re = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_part_re = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|[0-9]+")


def split_ident(ident):
    parts = []
    for seg in ident.split("_"):
        parts.extend(m.group(0).lower() for m in _part_re.finditer(seg))
    return parts


def ident_column(text):
    seen = {}
    for m in _ident_re.finditer(text):
        w = m.group(0)
        if w in seen:
            continue
        parts = split_ident(w)
        seen[w] = " ".join(parts) if len(parts) > 1 else ""
    return " ".join(v for v in seen.values() if v)


def trunc(s, n=LINE_WIDTH):
    s = s.replace("\t", " ")
    return s if len(s) <= n else s[: n - 1] + "…"


def fmt_hit(path, s, e, kind, name, sig):
    head = "%s:%d-%d  %s  %s" % (path, s, e, kind, name)
    if sig:
        head += "  " + sig
    return trunc(head)


# ----------------------------------------------------------------------------
# Symbol extraction

CTAGS_CMD = ["ctags", "--output-format=json", "--fields=+neKSsl", "--sort=no",
             "--kinds-C++=+p-h", "--kinds-C=+p-h", "--kinds-CMake=fm",
             "--languages=C,C++,Perl,CMake", "-o", "-", "-L", "-"]

_anon_re = re.compile(r"__anon[0-9a-f]+")


def run_ctags(root, paths):
    """Return {path: [tag dicts]}; None if ctags is unavailable."""
    if not paths:
        return {}
    try:
        r = subprocess.run(CTAGS_CMD, cwd=root, input="\n".join(paths) + "\n",
                           capture_output=True, text=True, errors="replace")
    except FileNotFoundError:
        return None
    out = {}
    for line in r.stdout.splitlines():
        if not line.startswith("{"):
            continue
        try:
            d = json.loads(line)
        except ValueError:
            continue
        if d.get("_type") != "tag":
            continue
        out.setdefault(d["path"], []).append(d)
    return out


def symbols_from_ctags(tags, lines):
    syms = []
    seen = set()
    for d in tags:
        name = d["name"]
        if name.startswith("__anon"):
            continue
        kind = d.get("kind", "")
        lang = d.get("language", "")
        line = d.get("line", 0)
        if line < 1 or line > len(lines):
            continue
        end = d.get("end") or line
        scope = _anon_re.sub("(anon)", d.get("scope", ""))
        sig = d.get("signature", "")
        if lang == "CMake" and kind == "macro":
            kind = "macro_cmake"
        if name in GTEST_MACROS and sig:
            args = [a.strip() for a in sig.strip("()").split(",")]
            sig = "%s(%s)" % (name, ",".join(args))
            name = ".".join(args[:2])
            kind = "function"
        elif not sig:
            sig = lines[line - 1].strip()
        key = (name, kind, line)
        if key in seen:
            continue
        seen.add(key)
        syms.append({"name": name, "kind": kind, "line": line, "end": max(end, line),
                     "scope": scope, "signature": sig, "language": lang})
    # Perl subs carry no end line: run to the line before the next sub (or EOF).
    perl = sorted((s for s in syms if s["language"] == "Perl" and s["end"] == s["line"]),
                  key=lambda s: s["line"])
    for i, s in enumerate(perl):
        nxt = perl[i + 1]["line"] - 1 if i + 1 < len(perl) else len(lines)
        while nxt > s["line"] and not lines[nxt - 1].strip():
            nxt -= 1
        s["end"] = nxt
    return syms


_fence_re = re.compile(r"^\s*(```|~~~)")
_head_re = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")


def markdown_symbols(lines):
    heads = []
    fence = False
    for i, l in enumerate(lines, 1):
        if _fence_re.match(l):
            fence = not fence
            continue
        if fence:
            continue
        m = _head_re.match(l)
        if m and m.group(2):
            heads.append((i, len(m.group(1)), m.group(2)))
    syms = []
    for idx, (ln, lvl, title) in enumerate(heads):
        end = len(lines)
        for ln2, lvl2, _ in heads[idx + 1:]:
            if lvl2 <= lvl:
                end = ln2 - 1
                break
        while end > ln and not lines[end - 1].strip():
            end -= 1
        syms.append({"name": title, "kind": "section", "line": ln, "end": end, "scope": "",
                     "signature": "h%d" % lvl, "language": "Markdown"})
    return syms


# ----------------------------------------------------------------------------
# Chunking

def split_range(lines, s, e, soft, hard):
    out = []
    while e - s + 1 > hard:
        cut = None
        for i in range(s + soft - 1, s + hard):
            if not lines[i - 1].strip():
                cut = i
                break
        if cut is None:
            cut = s + hard - 1
        out.append((s, cut))
        s = cut + 1
    out.append((s, e))
    return out


def build_chunks(lines, syms):
    """Return [(start, end, symbol_qualified_name, kind)] covering the file's text."""
    n = len(lines)
    owner = [None] * (n + 2)
    cands = [s for s in syms if s["kind"] in CHUNK_KINDS and s["end"] >= s["line"]]
    cands.sort(key=lambda s: -(s["end"] - s["line"]))
    for idx, s in enumerate(cands):
        for ln in range(s["line"], min(s["end"], n) + 1):
            owner[ln] = idx
    chunks = []
    ln = 1
    while ln <= n:
        o = owner[ln]
        e = ln
        while e + 1 <= n and owner[e + 1] == o:
            e += 1
        if o is None:
            a, b = ln, e
            while a <= b and not lines[a - 1].strip():
                a += 1
            while b >= a and not lines[b - 1].strip():
                b -= 1
            if a <= b:
                for x, y in split_range(lines, a, b, 60, 80):
                    chunks.append((x, y, "", "window"))
        else:
            s = cands[o]
            qn = (s["scope"] + "::" + s["name"]) if s["scope"] else s["name"]
            for x, y in split_range(lines, ln, e, 100, 200):
                chunks.append((x, y, qn, s["kind"]))
        ln = e + 1
    return chunks


# ----------------------------------------------------------------------------
# Database

def now_iso():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


def open_db(root, create=False):
    path = os.path.join(root, DB_RELPATH)
    if not create and not os.path.exists(path):
        return None
    os.makedirs(os.path.dirname(path), exist_ok=True)
    conn = sqlite3.connect(path, timeout=60)
    conn.execute("PRAGMA synchronous=OFF")
    return conn


def db_usable(conn):
    try:
        if conn.execute("PRAGMA user_version").fetchone()[0] != SCHEMA_VERSION:
            return False
        v = conn.execute("SELECT value FROM meta WHERE key='tool_version'").fetchone()
        return bool(v) and v[0] == TOOL_VERSION
    except sqlite3.Error:
        return False


def new_db(root):
    path = os.path.join(root, DB_RELPATH)
    for suffix in ("", "-journal", "-wal", "-shm"):
        try:
            os.remove(path + suffix)
        except OSError:
            pass
    conn = open_db(root, create=True)
    conn.executescript(SCHEMA)
    conn.execute("PRAGMA user_version=%d" % SCHEMA_VERSION)
    conn.execute("INSERT INTO meta VALUES('tool_version', ?)", (TOOL_VERSION,))
    conn.commit()
    return conn


def remove_file_rows(conn, path):
    ids = [r[0] for r in conn.execute("SELECT id FROM chunks WHERE file=?", (path,))]
    conn.executemany("DELETE FROM chunk_fts WHERE rowid=?", [(i,) for i in ids])
    conn.executemany("DELETE FROM vectors WHERE chunk_id=?", [(i,) for i in ids])
    conn.execute("DELETE FROM chunks WHERE file=?", (path,))
    conn.execute("DELETE FROM symbols WHERE file=?", (path,))
    conn.execute("DELETE FROM files WHERE path=?", (path,))


def index_files(conn, root, stats, warn=None):
    """stats: {path: (size, mtime_ns)}. (Re)index each path, replacing old rows."""
    loaded = {}
    for p, (size, mtime) in stats.items():
        remove_file_rows(conn, p)
        kind = file_kind(p)
        ext = os.path.splitext(p)[1].lower()
        skip = None
        data = b""
        if size > MAX_FILE_BYTES:
            skip = "large"
        elif ext in BINARY_EXT:
            skip = "binary"
        else:
            try:
                with open(os.path.join(root, p), "rb") as f:
                    data = f.read()
            except OSError:
                continue
            if b"\0" in data[:8192]:
                skip = "binary"
        if skip:
            conn.execute("INSERT INTO files VALUES(?,?,?,?,?,?,?)",
                         (p, None, mtime, size, 0, kind, skip))
            continue
        lines = split_lines(data)
        sha = hashlib.sha256(data).hexdigest()
        conn.execute("INSERT INTO files VALUES(?,?,?,?,?,?,NULL)",
                     (p, sha, mtime, size, len(lines), kind))
        loaded[p] = lines
    tag_paths = [p for p in loaded if os.path.splitext(p)[1].lower() in CTAGS_EXT
                 or os.path.basename(p) == "CMakeLists.txt"]
    tags = run_ctags(root, tag_paths)
    if tags is None and warn:
        warn("[ctags not found: symbols skipped]")
    tags = tags or {}
    for p, lines in loaded.items():
        ext = os.path.splitext(p)[1].lower()
        if ext in DOC_EXT:
            syms = markdown_symbols(lines)
        else:
            syms = symbols_from_ctags(tags.get(p, []), lines)
        conn.executemany(
            "INSERT INTO symbols VALUES(?,?,?,?,?,?,?,?)",
            [(s["name"], s["kind"], p, s["line"], s["end"], s["scope"], s["signature"],
              s["language"]) for s in syms])
        for (a, b, qn, kind) in build_chunks(lines, syms):
            cur = conn.execute("INSERT INTO chunks(file,start_line,end_line,symbol,kind) "
                               "VALUES(?,?,?,?,?)", (p, a, b, qn, kind))
            body = "\n".join(lines[a - 1:b])
            symcol = qn + " " + " ".join(split_ident(qn.split("::")[-1])) if qn else ""
            conn.execute("INSERT INTO chunk_fts(rowid, body, ident, symbol, path) VALUES(?,?,?,?,?)",
                         (cur.lastrowid, body, ident_column(body), symcol, p))


def compute_changes(conn, root, current):
    """Compare the index with the tree. Returns (changed, added, deleted, touched)."""
    rows = {r[0]: r[1:] for r in conn.execute("SELECT path, sha256, mtime, size FROM files")}
    changed, added, touched = {}, {}, {}
    for p, (size, mtime) in current.items():
        old = rows.get(p)
        if old is None:
            added[p] = (size, mtime)
            continue
        sha, omtime, osize = old
        if size == osize and mtime == omtime:
            continue
        if sha is not None and size == osize:
            try:
                with open(os.path.join(root, p), "rb") as f:
                    same = hashlib.sha256(f.read()).hexdigest() == sha
            except OSError:
                same = False
            if same:
                touched[p] = mtime
                continue
        changed[p] = (size, mtime)
    deleted = [p for p in rows if p not in current]
    return changed, added, deleted, touched


def set_meta(conn, root):
    head = subprocess.run(["git", "-C", root, "rev-parse", "HEAD"], capture_output=True, text=True)
    conn.execute("INSERT OR REPLACE INTO meta VALUES('git_head', ?)",
                 (head.stdout.strip() if head.returncode == 0 else "",))
    conn.execute("INSERT OR REPLACE INTO meta VALUES('built_at', ?)", (now_iso(),))
    conn.execute("INSERT OR REPLACE INTO meta VALUES('built_epoch', ?)", (str(int(time.time())),))


def full_build(root, warn=None):
    t0 = time.time()
    conn = new_db(root)
    current = scan_tree(root)
    index_files(conn, root, current, warn)
    set_meta(conn, root)
    conn.commit()
    return conn, len(current), time.time() - t0


def refresh(conn, root, warn=None):
    """Bring the index up to date. Returns (conn, files_touched_count)."""
    current = scan_tree(root)
    changed, added, deleted, touched = compute_changes(conn, root, current)
    n = len(changed) + len(added) + len(deleted)
    if n or touched:
        conn.execute("BEGIN IMMEDIATE")
        # Re-diff inside the write lock so two concurrent refreshes do not duplicate work.
        for p in deleted:
            remove_file_rows(conn, p)
        for p, m in touched.items():
            conn.execute("UPDATE files SET mtime=? WHERE path=?", (m, p))
        batch = dict(changed)
        batch.update(added)
        index_files(conn, root, batch, warn)
        if n:
            set_meta(conn, root)
        conn.commit()
    return n


def stale_count(conn, root):
    changed, added, deleted, _ = compute_changes(conn, root, scan_tree(root))
    return len(changed) + len(added) + len(deleted)


def meta_get(conn, key, default=""):
    r = conn.execute("SELECT value FROM meta WHERE key=?", (key,)).fetchone()
    return r[0] if r else default


def get_conn(root, no_refresh=False, err=None):
    """Open the index, creating or refreshing it as needed."""
    err = err or sys.stderr
    conn = open_db(root)
    if conn is not None and not db_usable(conn):
        conn.close()
        conn = None
    if conn is None:
        if no_refresh:
            raise IndexError_("no usable index at %s; run: q rebuild" % DB_RELPATH)
        warn = lambda m: print(m, file=err)
        conn, n, secs = full_build(root, warn)
        print("[built index: %d files in %.1fs]" % (n, secs), file=err)
        return conn
    if no_refresh:
        n = stale_count(conn, root)
        if n:
            print("[index may be stale: %d files changed since %s]"
                  % (n, meta_get(conn, "built_at")), file=err)
        return conn
    n = refresh(conn, root, lambda m: print(m, file=err))
    if n:
        print("[refreshed %d files]" % n, file=err)
    return conn


# ----------------------------------------------------------------------------
# Queries

def resolve_path(conn, path):
    path = path.replace("\\", "/")
    while path.startswith("./"):
        path = path[2:]
    r = conn.execute("SELECT path FROM files WHERE path=?", (path,)).fetchone()
    if r:
        return r[0]
    rows = conn.execute("SELECT path FROM files WHERE path LIKE ? ESCAPE '\\'",
                        ("%/" + path.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_"),)).fetchall()
    if len(rows) == 1:
        return rows[0][0]
    if len(rows) > 1:
        raise IndexError_("ambiguous path %s: %s" % (path, ", ".join(r[0] for r in rows[:5])))
    raise IndexError_("not indexed: %s" % path)


def qname(row):
    name, scope = row["name"], row["scope"]
    return scope + "::" + name if scope and "." not in name else name


def cmd_sym(conn, a, out):
    name = a.name
    scope = None
    if "::" in name:
        scope, name = name.rsplit("::", 1)
        scope = scope.split("::")[-1]
    esc = name.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_")
    sql = ("SELECT s.*, f.kind AS fkind, CASE WHEN s.name = :n THEN 0 "
           "WHEN s.name = :n COLLATE NOCASE THEN 1 "
           "WHEN s.name LIKE :p ESCAPE '\\' THEN 2 ELSE 3 END AS rank "
           "FROM symbols s JOIN files f ON f.path = s.file "
           "WHERE s.name LIKE :s ESCAPE '\\'")
    params = {"n": name, "p": esc + "%", "s": "%" + esc + "%"}
    if scope:
        sql += " AND (s.scope = :sc OR s.scope LIKE :scl)"
        params["sc"] = scope
        params["scl"] = "%::" + scope
    if a.kind:
        sql += " AND s.kind LIKE :k"
        params["k"] = a.kind + "%"
    cur = conn.execute(sql, params)
    cols = [d[0] for d in cur.description]
    rows = [dict(zip(cols, r)) for r in cur.fetchall()]
    hidden = 0
    if not a.all and not a.kind:
        kept = [r for r in rows if r["kind"] not in MINOR_KINDS]
        if kept:  # when only members/variables match, show them rather than nothing
            hidden = len(rows) - len(kept)
            rows = kept
    rows.sort(key=lambda r: (r["rank"], KIND_ORDER.get(r["kind"], 7),
                             FILE_KIND_ORDER.get(r["fkind"], 9), r["file"], r["line"]))
    if not rows:
        print("no symbols match %s%s" % (a.name, " (try --all)" if hidden else ""), file=out)
        return 1
    for r in rows[:a.limit]:
        print(fmt_hit(r["file"], r["line"], r["end_line"], r["kind"], qname(r), r["signature"]),
              file=out)
    more = len(rows) - a.limit
    if more > 0:
        print("... %d more (use --limit)" % more, file=out)
    if hidden:
        print("... %d member/variable matches hidden (use --all)" % hidden, file=out)
    return 0


def cmd_outline(conn, a, out):
    path = resolve_path(conn, a.path)
    rows = conn.execute("SELECT name, kind, line, end_line, scope, signature FROM symbols "
                        "WHERE file=? ORDER BY line, end_line DESC", (path,)).fetchall()
    total = len(rows)
    if not a.all:
        rows = [r for r in rows if r[1] not in MINOR_KINDS]
    if not rows:
        print("%s: no symbols" % path, file=out)
        return 0
    for name, kind, line, end, scope, sig in rows[:a.limit or None]:
        if kind == "section":
            indent = "  " * (int(sig[1:]) - 1)
            print(trunc("%s:%d-%d  %s%s" % (path, line, end, indent, name)), file=out)
        else:
            q = scope + "::" + name if scope and "." not in name else name
            print(fmt_hit(path, line, end, kind, q, sig), file=out)
    if a.limit and len(rows) > a.limit:
        print("... %d more (use --limit 0 for all)" % (len(rows) - a.limit), file=out)
    if total > len(rows) and not a.all:
        print("... %d member/variable symbols hidden (use --all)" % (total - len(rows)), file=out)
    return 0


def query_terms(words):
    """Return (phrase_query_or_None, and_query, or_query, plain terms for snippet scoring)."""
    groups = []
    plain = []
    toks = []
    for w in words:
        for tok in re.findall(r"[A-Za-z0-9_]+", w):
            parts = split_ident(tok)
            toks.append(tok)
            alts = []
            if "_" not in tok and len(parts) <= 1:
                alts.append('"%s"' % tok.lower())
            else:
                alts.append('"%s"' % " ".join(parts))
                if "_" not in tok:
                    alts.append('"%s"' % tok.lower())
            groups.append("(" + " OR ".join(dict.fromkeys(alts)) + ")")
            plain.append((tok.lower(), " ".join(parts)))
    phrase = None
    if len(toks) > 1:
        phrase = '"%s"' % " ".join(p for t in toks for p in (split_ident(t) or [t.lower()]))
    return phrase, " AND ".join(groups), " OR ".join(groups), plain


def snippet_for(body, start, plain):
    lines = body.split("\n")
    best, best_score = 0, -1
    for i, l in enumerate(lines):
        low = l.lower()
        flat = " ".join(split_ident(" ".join(_ident_re.findall(l)))) if low.strip() else ""
        score = sum(1 for t, parts in plain if t in low or (parts and parts in flat))
        if score > best_score:
            best, best_score = i, score
    picked = [best]
    lo, hi = best - 1, best + 1
    while len(picked) < 3 and (lo >= 0 or hi < len(lines)):
        if lo >= 0:
            if lines[lo].strip():
                picked.append(lo)
            lo -= 1
        if len(picked) < 3 and hi < len(lines):
            if lines[hi].strip():
                picked.append(hi)
            hi += 1
    return [(start + i, lines[i]) for i in sorted(picked)]


def fts_search(conn, words, glob=None, kinds=None, limit=20, exclude=None, any_ok=True):
    """Ranked chunk search. Tries the adjacent-words phrase first, then all terms, then any term."""
    phrase, and_q, or_q, plain = query_terms(words)
    if not and_q:
        return [], plain, False
    stages = [q for q in (phrase, and_q, or_q if any_ok else None) if q]
    seen, rows, fell_back = set(), [], False
    for q in stages:
        if len(rows) >= limit:
            break
        sql = ("SELECT c.id, c.file, c.start_line, c.end_line, c.symbol, c.kind, f.kind, "
               "chunk_fts.body FROM chunk_fts JOIN chunks c ON c.id = chunk_fts.rowid "
               "JOIN files f ON f.path = c.file WHERE chunk_fts MATCH ?")
        params = [q]
        if glob:
            sql += " AND c.file GLOB ?"
            params.append(glob)
        if kinds:
            sql += " AND f.kind IN (%s)" % ",".join("?" * len(kinds))
            params.extend(kinds)
        if exclude:
            sql += " AND c.file != ?"
            params.append(exclude)
        sql += " ORDER BY bm25(chunk_fts, 1.0, 1.0, 8.0, 2.0) LIMIT ?"
        params.append(limit)
        if q == or_q and q != and_q and rows:
            break  # any-term matching is only a fallback when nothing has every term
        try:
            found = conn.execute(sql, params).fetchall()
        except sqlite3.OperationalError as e:
            raise IndexError_("bad search query: %s" % e)
        if q == or_q and q != and_q and found and not rows:
            fell_back = True
        for r in found:
            if r[0] not in seen and len(rows) < limit:
                seen.add(r[0])
                rows.append(r)
    return rows, plain, fell_back


KIND_FILTER = {"code": ["code", "header"], "test": ["test"], "doc": ["doc"], "header": ["header"],
               "script": ["script"], "build": ["build"], "other": ["other"]}


def cmd_search(conn, a, out):
    kinds = KIND_FILTER.get(a.kind) if a.kind else None
    rows, plain, fell_back = fts_search(conn, a.query, a.in_glob, kinds, a.limit)
    if not rows:
        print("no matches", file=out)
        return 1
    if fell_back:
        print("[no chunk has every term; showing chunks with any term]", file=out)
    for _id, file, s, e, sym, ckind, _fk, body in rows:
        snip = snippet_for(body, s, plain)
        print(fmt_hit(file, s, e, ckind, sym or "-", ""), file=out)
        if not a.brief:
            for ln, text in snip:
                print(trunc("    %d: %s" % (ln, text.strip()), 120), file=out)
    return 0


def run_rg(root, name):
    cmd = ["rg", "-nw", "-F", "--no-heading", "--color", "never", "--no-messages",
           "-g", "!build*", "-g", "!.index", "-g", "!packages", "-g", "!.git",
           "-g", "!tools/index", "--", name, "."]
    try:
        r = subprocess.run(cmd, cwd=root, capture_output=True, text=True, errors="replace",
                           stdin=subprocess.DEVNULL)
    except FileNotFoundError:
        raise IndexError_("ripgrep (rg) is not installed")
    hits = {}
    for line in r.stdout.splitlines():
        m = re.match(r"^(?:\./)?(.*?):(\d+):", line)
        if m:
            hits.setdefault(m.group(1), []).append(int(m.group(2)))
    return hits


def cmd_refs(conn, a, out, root):
    hits = run_rg(root, a.name)
    defs = {(r[0], r[1]) for r in conn.execute(
        "SELECT file, line FROM symbols WHERE name=?", (a.name.split("::")[-1],))}
    kinds = {r[0]: r[1] for r in conn.execute("SELECT path, kind FROM files")}
    files, skipped, total = [], 0, 0
    for path, lns in hits.items():
        keep = [l for l in sorted(set(lns)) if (path, l) not in defs]
        skipped += len(set(lns)) - len(keep)
        if keep:
            files.append((path, keep))
            total += len(keep)
    if not files:
        print("no uses of %s found (%d definition lines skipped)" % (a.name, skipped), file=out)
        return 1
    files.sort(key=lambda f: (FILE_KIND_ORDER.get(kinds.get(f[0], "other"), 9), f[0]))
    print("%s: %d use lines in %d files; %d definition lines skipped "
          "[exact word match via ripgrep, not semantic]" % (a.name, total, len(files), skipped),
          file=out)
    for path, lns in files[:a.limit]:
        shown = ",".join(str(l) for l in lns[:12])
        more = " +%d" % (len(lns) - 12) if len(lns) > 12 else ""
        print(trunc("%s  x%d  %s%s" % (path, len(lns), shown, more)), file=out)
    if len(files) > a.limit:
        print("... %d more files (use --limit)" % (len(files) - a.limit), file=out)
    return 0


def cmd_tests_for(conn, a, out):
    name = a.name.split("::")[-1]
    rows, _, _ = fts_search(conn, [name], kinds=["test"], limit=5000)
    rx = re.compile(r"(?<![A-Za-z0-9_])%s(?![A-Za-z0-9_])" % re.escape(name))
    tests, others = {}, {}
    for _id, file, s, e, sym, ckind, _fk, body in rows:
        m = rx.search(body)
        if not m:
            continue
        ln = s + body.count("\n", 0, m.start())
        if "." in sym and ckind == "function":
            tests[(file, sym)] = min(tests.get((file, sym), ln), ln)
        else:
            others[file] = others.get(file, 0) + 1
    if not tests and not others:
        print("no test mentions %s" % name, file=out)
        return 1
    ranges = {}
    for (file, sym) in tests:
        r = conn.execute("SELECT line, end_line, signature FROM symbols WHERE file=? AND name=? "
                         "AND kind='function' ORDER BY line", (file, sym)).fetchall()
        for line, end, sig in r:
            if line <= tests[(file, sym)] <= end:
                ranges[(file, sym)] = (line, end, sig)
    items = sorted(tests, key=lambda k: (k[0], ranges.get(k, (0,))[0]))
    for k in items[:a.limit]:
        line, end, sig = ranges.get(k, (tests[k], tests[k], ""))
        print(fmt_hit(k[0], line, end, "test", k[1], "(first use line %d)" % tests[k]), file=out)
    if len(items) > a.limit:
        print("... %d more tests (use --limit)" % (len(items) - a.limit), file=out)
    if others:
        print(trunc("also in non-TEST test code (helpers/fixtures): " +
                    ", ".join("%s x%d" % kv for kv in sorted(others.items()))), file=out)
    return 0


def cmd_where(conn, a, out, root):
    words = [w.lower() for t in a.topic for w in re.findall(r"[A-Za-z0-9_]+", t) if len(w) > 1]
    if not words:
        print("give a topic", file=out)
        return 1
    found = 0
    heads = conn.execute("SELECT name, line, end_line, signature FROM symbols "
                         "WHERE file='docs/CODEMAP.md' AND kind='section' ORDER BY line").fetchall()
    scored = []
    for name, line, end, sig in heads:
        sc = sum(1 for w in words if w in name.lower())
        if sc:
            scored.append((-sc, line, name, end))
    scored.sort()
    for _, line, name, end in scored[:4]:
        print(trunc("docs/CODEMAP.md:%d-%d  section  %s" % (line, end, name)), file=out)
        found += 1
    path = os.path.join(root, "docs", "CODEMAP.md")
    if os.path.exists(path):
        with open(path, "rb") as f:
            lines = split_lines(f.read())
        need = max(1, (len(words) * 2 + 2) // 3)
        rows = []
        for i, l in enumerate(lines, 1):
            if not l.startswith("|"):
                continue
            low = l.lower()
            sc = sum(1 for w in words if w in low)
            if sc >= need:
                rows.append((-sc, i, l))
        rows.sort()
        for _, i, l in rows[:6]:
            print(trunc("docs/CODEMAP.md:%d  row  %s" % (i, l.strip("| ")), 600), file=out)
            found += 1
    rows, plain, _ = fts_search(conn, words, kinds=["doc"], limit=3 if found else 6,
                                exclude="docs/CODEMAP.md", any_ok=not found)
    for _id, file, s, e, sym, ckind, _fk, body in rows:
        print(fmt_hit(file, s, e, ckind, sym or "-", ""), file=out)
        found += 1
    if not found:
        print("nothing found for: %s (try q search)" % " ".join(a.topic), file=out)
        return 1
    return 0


def cmd_read(root, a, out):
    path = a.path.replace("\\", "/")
    while path.startswith("./"):
        path = path[2:]
    full = os.path.realpath(os.path.join(root, path))
    if not full.startswith(os.path.realpath(root) + os.sep):
        raise IndexError_("path outside the repository: %s" % a.path)
    if a.start < 1 or a.end < a.start:
        raise IndexError_("bad range %d-%d" % (a.start, a.end))
    if a.end - a.start + 1 > READ_LIMIT and not a.force:
        raise IndexError_("range is %d lines (limit %d); narrow it or use --force"
                          % (a.end - a.start + 1, READ_LIMIT))
    try:
        with open(full, "rb") as f:
            lines = split_lines(f.read())
    except OSError as e:
        raise IndexError_("cannot read %s: %s" % (a.path, e.strerror))
    if a.start > len(lines):
        raise IndexError_("%s has only %d lines" % (path, len(lines)))
    end = min(a.end, len(lines))
    w = len(str(end))
    for i in range(a.start, end + 1):
        print("%*d  %s" % (w, i, lines[i - 1]), file=out)
    return 0


def cmd_stats(conn, root, out):
    q = lambda s: conn.execute(s).fetchone()[0]
    print("files:    %d indexed, %d skipped (binary/large)" % (
        q("SELECT count(*) FROM files WHERE skip IS NULL"),
        q("SELECT count(*) FROM files WHERE skip IS NOT NULL")), file=out)
    print("lines:    %d" % q("SELECT coalesce(sum(lines),0) FROM files"), file=out)
    print("symbols:  %d" % q("SELECT count(*) FROM symbols"), file=out)
    print("chunks:   %d" % q("SELECT count(*) FROM chunks"), file=out)
    by = conn.execute("SELECT kind, count(*) FROM files WHERE skip IS NULL GROUP BY kind "
                      "ORDER BY 2 DESC").fetchall()
    print("by kind:  " + ", ".join("%s %d" % r for r in by), file=out)
    built = meta_get(conn, "built_at")
    epoch = int(meta_get(conn, "built_epoch", "0") or 0)
    age = int(time.time()) - epoch if epoch else 0
    print("updated:  %s (%ds ago)" % (built, age), file=out)
    print("git HEAD: %s" % meta_get(conn, "git_head")[:12], file=out)
    print("tool:     rfhindex %s, schema %d" % (meta_get(conn, "tool_version"), SCHEMA_VERSION),
          file=out)
    size = os.path.getsize(os.path.join(root, DB_RELPATH))
    print("db size:  %.1f MB" % (size / 1048576.0), file=out)
    return 0


# ----------------------------------------------------------------------------
# CLI

def build_parser():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--no-refresh", action="store_true",
                        help="do not refresh; report staleness instead")
    common.add_argument("--root", help="repository root (default: the repo containing this tool)")
    p = argparse.ArgumentParser(
        prog="q", description="Lookup layer over the repository: returns pointers "
        "(path:start-end plus short snippets), never whole files. The index refreshes "
        "itself before every command.")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("sym", parents=[common], help="where is NAME defined (exact, prefix, substring)")
    s.add_argument("name")
    s.add_argument("--kind", help="restrict to a kind prefix, e.g. function, prototype, class")
    s.add_argument("--all", action="store_true", help="include member/variable/enumerator symbols")
    s.add_argument("--limit", type=int, default=20)

    s = sub.add_parser("outline", parents=[common], help="symbols of a file in order, with ranges")
    s.add_argument("path")
    s.add_argument("--all", action="store_true", help="include member/variable symbols")
    s.add_argument("--limit", type=int, default=0, help="0 = no limit")

    s = sub.add_parser("search", parents=[common], help="ranked full-text search over chunks")
    s.add_argument("query", nargs="+")
    s.add_argument("--in", dest="in_glob", metavar="PATH_GLOB", help="e.g. 'core/*' or 'RFHUtil/*.cpp'")
    s.add_argument("--kind", choices=sorted(KIND_FILTER))
    s.add_argument("--limit", type=int, default=20)
    s.add_argument("--brief", action="store_true", help="no snippet lines")

    s = sub.add_parser("refs", parents=[common],
                       help="word-bounded text uses of NAME (ripgrep; exact text match, "
                            "not semantic analysis: no overload, scope or comment handling)")
    s.add_argument("name")
    s.add_argument("--limit", type=int, default=20)

    s = sub.add_parser("tests-for", parents=[common],
                       help="TEST(...) bodies in tests/ that mention NAME (text match)")
    s.add_argument("name")
    s.add_argument("--limit", type=int, default=20)

    s = sub.add_parser("where", parents=[common], help="which doc section/file covers TOPIC")
    s.add_argument("topic", nargs="+")

    s = sub.add_parser("read", parents=[common], help="print exact lines with line numbers")
    s.add_argument("path")
    s.add_argument("start", type=int)
    s.add_argument("end", type=int)
    s.add_argument("--force", action="store_true", help="allow more than %d lines" % READ_LIMIT)

    sub.add_parser("stats", parents=[common], help="index counts, age and git HEAD")
    sub.add_parser("rebuild", parents=[common], help="full rebuild")
    return p


def main(argv=None, out=None):
    out = out or sys.stdout
    a = build_parser().parse_args(argv)
    root = find_root(a.root)
    try:
        if a.cmd == "rebuild":
            conn, n, secs = full_build(root, lambda m: print(m, file=sys.stderr))
            print("[rebuilt index: %d files in %.1fs]" % (n, secs), file=sys.stderr)
            return 0
        if a.cmd == "read":
            return cmd_read(root, a, out)
        conn = get_conn(root, a.no_refresh)
        if a.cmd == "sym":
            return cmd_sym(conn, a, out)
        if a.cmd == "outline":
            return cmd_outline(conn, a, out)
        if a.cmd == "search":
            return cmd_search(conn, a, out)
        if a.cmd == "refs":
            return cmd_refs(conn, a, out, root)
        if a.cmd == "tests-for":
            return cmd_tests_for(conn, a, out)
        if a.cmd == "where":
            return cmd_where(conn, a, out, root)
        if a.cmd == "stats":
            return cmd_stats(conn, root, out)
    except IndexError_ as e:
        print("error: %s" % e, file=sys.stderr)
        return 2
    except BrokenPipeError:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
