"""Unit tests for rfhindex on a small fixture tree (no dependence on the real repository)."""
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import rfhindex  # noqa: E402

FILES = {
    "src/parser.h": (
        "#pragma once\n"
        "class Parser\n"
        "{\n"
        "public:\n"
        "\tvoid getParsedData(int charFormat);\n"
        "\tint count;\n"
        "\tint getParsedCount;\n"
        "};\n"
    ),
    "src/parser.cpp": (
        "#include \"parser.h\"\n"
        "\n"
        "void Parser::getParsedData(int charFormat)\n"
        "{\n"
        "\tcount = charFormat;\n"
        "\tprocessAttributes(count);\n"
        "}\n"
        "\n"
        "void processAttributes(int n)\n"
        "{\n"
        "\t// the xml_attr_str buffer\n"
        "}\n"
    ),
    "tests/parser_tests.cpp": (
        "#include \"gtest/gtest.h\"\n"
        "\n"
        "TEST(ParserSuite, CallsProcessAttributes)\n"
        "{\n"
        "    processAttributes(3);\n"
        "}\n"
        "\n"
        "TEST(ParserSuite, DoesNotMentionIt)\n"
        "{\n"
        "    EXPECT_EQ(1, 1);\n"
        "}\n"
    ),
    "CMakeLists.txt": (
        "cmake_minimum_required(VERSION 3.21)\n"
        "function(add_fixture_test name)\n"
        "\tadd_test(NAME ${name} COMMAND ${name})\n"
        "endfunction()\n"
    ),
    "tools/tool.pl": (
        "#!/usr/bin/perl\n"
        "use strict;\n"
        "sub first_helper {\n"
        "\treturn 1;\n"
        "}\n"
        "sub second_helper {\n"
        "\treturn 2;\n"
        "}\n"
    ),
    "docs/NOTES.md": (
        "# Notes\n"
        "\n"
        "intro text\n"
        "\n"
        "## Alpha topic\n"
        "\n"
        "```\n"
        "# not a heading\n"
        "```\n"
        "\n"
        "### Alpha detail\n"
        "\n"
        "detail text about widgets\n"
        "\n"
        "## Beta topic\n"
        "\n"
        "beta text\n"
    ),
}

CRLF_FILE = ("src/crlf.cpp", b"int crlfOne()\r\n{\r\n\treturn 1;\r\n}\r\n\r\nint crlfTwo()\r\n{\r\n\treturn 2;\r\n}\r\n")
BOM_FILE = ("src/bom.cpp", b"\xef\xbb\xbfint bomFunction()\n{\n\treturn 3;\n}\n")


def run_q(root, *args):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stderr(err):
        rc = rfhindex.main(list(args) + ["--root", root], out=out)
    return rc, out.getvalue(), err.getvalue()


class FixtureCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = tempfile.mkdtemp(prefix="rfhindex-test-")
        subprocess.run(["git", "init", "-q", cls.root], check=True)
        for path, text in FILES.items():
            cls.write(path, text.encode())
        cls.write(*CRLF_FILE)
        cls.write(*BOM_FILE)
        with open(os.path.join(cls.root, "blob.bin"), "wb") as f:
            f.write(b"\x00\x01\x02binary")
        subprocess.run(["git", "-C", cls.root, "add", "-A"], check=True)
        # untracked but not ignored; ignored files must stay out of the index
        cls.write("src/untracked.cpp", b"int untrackedFunction() { return 4; }\n")
        cls.write(".gitignore", b"ignored.cpp\nbuild-x/\n")
        cls.write("ignored.cpp", b"int ignoredFunction() { return 5; }\n")
        cls.write("build-x/gen.cpp", b"int generatedFunction() { return 6; }\n")
        subprocess.run(["git", "-C", cls.root, "add", "-f", "build-x/gen.cpp"], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.root, ignore_errors=True)

    @classmethod
    def write(cls, path, data):
        full = os.path.join(cls.root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as f:
            f.write(data)

    def q(self, *args):
        return run_q(self.root, *args)


class TestQueries(FixtureCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        rc, _, err = run_q(cls.root, "rebuild")
        assert rc == 0, err

    def test_sym_orders_definitions_and_declarations(self):
        rc, out, _ = self.q("sym", "getParsedData")
        self.assertEqual(rc, 0)
        lines = out.splitlines()
        self.assertIn("src/parser.cpp:3-7  function  Parser::getParsedData", lines[0])
        self.assertIn("src/parser.h:5-5  prototype  Parser::getParsedData", lines[1])

    def test_sym_exact_then_prefix_then_substring(self):
        rc, out, _ = self.q("sym", "second_helper")
        self.assertEqual(rc, 0)
        self.assertIn("tools/tool.pl:6-8  subroutine  second_helper", out)
        rc, out, _ = self.q("sym", "helper")
        names = [l.split()[2] for l in out.splitlines()]
        self.assertEqual(set(names), {"first_helper", "second_helper"})
        rc, out, _ = self.q("sym", "process")
        self.assertIn("processAttributes", out)

    def test_sym_members_hidden_unless_all(self):
        rc, out, _ = self.q("sym", "getParsed")
        self.assertEqual(rc, 0)
        self.assertNotIn("member  Parser", out)
        self.assertIn("hidden (use --all)", out)
        rc, out, _ = self.q("sym", "getParsed", "--all")
        self.assertIn("member  Parser::getParsedCount", out)

    def test_sym_shows_members_when_nothing_else_matches(self):
        rc, out, _ = self.q("sym", "count")
        self.assertEqual(rc, 0)
        self.assertIn("member  Parser::count", out)

    def test_sym_kind_and_scope_qualified(self):
        rc, out, _ = self.q("sym", "Parser::getParsedData", "--kind", "prototype")
        self.assertEqual(len(out.splitlines()), 1)
        self.assertIn("src/parser.h", out)

    def test_markdown_and_cmake_and_perl_symbols(self):
        _, out, _ = self.q("sym", "add_fixture_test")
        self.assertIn("CMakeLists.txt:2-4  function  add_fixture_test", out)
        _, out, _ = self.q("sym", "Alpha", "--kind", "section")
        self.assertIn("docs/NOTES.md:5-13  section  Alpha topic", out)
        self.assertIn("docs/NOTES.md:11-13  section  Alpha detail", out)

    def test_outline_code_in_order(self):
        _, out, _ = self.q("outline", "src/parser.cpp")
        lines = out.splitlines()
        self.assertEqual(len(lines), 2)
        self.assertIn(":3-7  function  Parser::getParsedData", lines[0])
        self.assertIn(":9-12  function  processAttributes", lines[1])

    def test_outline_markdown_headings_with_ranges(self):
        _, out, _ = self.q("outline", "docs/NOTES.md")
        lines = out.splitlines()
        self.assertEqual(len(lines), 4)  # the fenced '# not a heading' is not a heading
        self.assertIn("docs/NOTES.md:1-17  Notes", lines[0])
        self.assertIn("docs/NOTES.md:5-13    Alpha topic", lines[1])
        self.assertIn("docs/NOTES.md:15-17    Beta topic", lines[3])

    def test_outline_accepts_a_unique_suffix(self):
        _, out, _ = self.q("outline", "parser.cpp")
        self.assertIn("src/parser.cpp", out)

    def test_perl_subs_get_ranges(self):
        _, out, _ = self.q("outline", "tools/tool.pl")
        self.assertIn(":3-5  subroutine  first_helper", out)
        self.assertIn(":6-8  subroutine  second_helper", out)

    def test_search_splits_camel_case(self):
        rc, out, _ = self.q("search", "parsed", "data")
        self.assertEqual(rc, 0)
        self.assertIn("src/parser.cpp", out.splitlines()[0])
        self.assertIn("Parser::getParsedData", out)

    def test_search_finds_the_whole_identifier_and_snake_case(self):
        _, out, _ = self.q("search", "getParsedData", "--brief")
        self.assertIn("Parser::getParsedData", out)
        _, out, _ = self.q("search", "xml_attr_str")
        self.assertIn("src/parser.cpp:9-12", out)
        _, out, _ = self.q("search", "xmlAttrStr")
        self.assertIn("src/parser.cpp:9-12", out)

    def test_search_filters_and_snippets(self):
        _, out, _ = self.q("search", "processAttributes", "--kind", "test")
        self.assertIn("tests/parser_tests.cpp", out)
        self.assertNotIn("src/parser.cpp", out)
        _, out, _ = self.q("search", "processAttributes", "--in", "src/*")
        self.assertNotIn("tests/", out)
        hits = [l for l in out.splitlines() if l.startswith("    ")]
        self.assertTrue(0 < len(hits) <= 3 * 2)
        _, out, _ = self.q("search", "widgets", "--kind", "doc")
        self.assertIn("docs/NOTES.md", out)
        self.assertIn("detail text about widgets", out)

    def test_search_lines_are_compact(self):
        _, out, _ = self.q("search", "processAttributes")
        for line in out.splitlines():
            self.assertLessEqual(len(line), 140)

    def test_refs_group_by_file_and_skip_definitions(self):
        rc, out, _ = self.q("refs", "processAttributes")
        self.assertEqual(rc, 0)
        self.assertIn("not semantic", out)
        self.assertIn("src/parser.cpp  x1  6", out)  # line 9 is the definition
        self.assertIn("tests/parser_tests.cpp  x1  5", out)

    def test_refs_is_word_bounded(self):
        rc, out, _ = self.q("refs", "count")
        self.assertIn("src/parser.cpp  x2  5,6", out)  # not 'processAttributes(count)' misses
        rc, out, _ = self.q("refs", "Attributes")
        self.assertEqual(rc, 1)

    def test_tests_for(self):
        rc, out, _ = self.q("tests-for", "processAttributes")
        self.assertEqual(rc, 0)
        self.assertIn("tests/parser_tests.cpp:3-6  test  ParserSuite.CallsProcessAttributes", out)
        self.assertNotIn("DoesNotMentionIt", out)
        rc, out, _ = self.q("tests-for", "neverMentioned")
        self.assertEqual(rc, 1)

    def test_read_exact_lines(self):
        rc, out, _ = self.q("read", "src/parser.cpp", "3", "5")
        self.assertEqual(out.splitlines(), [
            "3  void Parser::getParsedData(int charFormat)", "4  {", "5  \tcount = charFormat;"])

    def test_read_refuses_large_ranges(self):
        rc, out, err = self.q("read", "src/parser.cpp", "1", "500")
        self.assertEqual(rc, 2)
        self.assertIn("--force", err)
        rc, out, err = self.q("read", "src/parser.cpp", "1", "500", "--force")
        self.assertEqual(rc, 0)
        self.assertEqual(len(out.splitlines()), 12)

    def test_read_refuses_paths_outside_the_repository(self):
        rc, _, err = self.q("read", "../etc/passwd", "1", "2")
        self.assertEqual(rc, 2)

    def test_crlf_keeps_line_numbers(self):
        _, out, _ = self.q("sym", "crlfTwo")
        self.assertIn("src/crlf.cpp:6-9  function  crlfTwo", out)
        _, out, _ = self.q("read", "src/crlf.cpp", "6", "6")
        self.assertEqual(out, "6  int crlfTwo()\n")

    def test_bom_is_stripped_and_line_one_is_exact(self):
        _, out, _ = self.q("sym", "bomFunction")
        self.assertIn("src/bom.cpp:1-4  function  bomFunction", out)
        _, out, _ = self.q("read", "src/bom.cpp", "1", "1")
        self.assertEqual(out, "1  int bomFunction()\n")

    def test_what_is_indexed(self):
        _, out, _ = self.q("sym", "untrackedFunction")
        self.assertIn("src/untracked.cpp", out)
        for name in ("ignoredFunction", "generatedFunction"):
            rc, out, _ = self.q("sym", name)
            self.assertEqual(rc, 1, name)
        rc, out, _ = self.q("stats")
        self.assertIn("1 skipped", out)  # blob.bin

    def test_stats(self):
        rc, out, _ = self.q("stats")
        self.assertEqual(rc, 0)
        for key in ("files:", "symbols:", "chunks:", "updated:", "git HEAD:"):
            self.assertIn(key, out)

    def test_where_prefers_codemap_rows_then_docs(self):
        self.write("docs/CODEMAP.md", b"# Map\n\n| Question | Where |\n|---|---|\n"
                   b"| How are widgets rendered | src/parser.cpp |\n")
        _, out, err = self.q("where", "widgets", "rendered")
        self.assertIn("docs/CODEMAP.md:5  row", out)
        self.assertIn("refreshed", err)
        os.remove(os.path.join(self.root, "docs/CODEMAP.md"))
        self.q("stats")


class TestFreshness(FixtureCase):
    def setUp(self):
        run_q(self.root, "rebuild")

    def bump(self, path, text):
        full = os.path.join(self.root, path)
        with open(full, "ab" if text.startswith("+") else "wb") as f:
            f.write(text.lstrip("+").encode())
        t = time.time() + 5
        os.utime(full, (t, t))

    def tearDown(self):
        for p in ("src/new.cpp",):
            if os.path.exists(os.path.join(self.root, p)):
                os.remove(os.path.join(self.root, p))
        self.write("src/parser.cpp", FILES["src/parser.cpp"].encode())

    def test_no_change_prints_no_freshness_line(self):
        rc, _, err = self.q("sym", "processAttributes")
        self.assertEqual(err, "")

    def test_edit_is_refreshed_before_answering(self):
        self.bump("src/parser.cpp", "+\nint addedByEdit()\n{\n\treturn 1;\n}\n")
        rc, out, err = self.q("sym", "addedByEdit")
        self.assertEqual(rc, 0)
        self.assertIn("[refreshed 1 files]", err)
        self.assertIn("src/parser.cpp", out)
        rc, _, err = self.q("sym", "addedByEdit")
        self.assertEqual(err, "")

    def test_add_and_delete_are_refreshed(self):
        self.write("src/new.cpp", b"int brandNew()\n{\n\treturn 1;\n}\n")
        rc, out, err = self.q("sym", "brandNew")
        self.assertEqual(rc, 0)
        self.assertIn("[refreshed 1 files]", err)
        os.remove(os.path.join(self.root, "src/new.cpp"))
        rc, out, err = self.q("sym", "brandNew")
        self.assertEqual(rc, 1)
        self.assertIn("[refreshed 1 files]", err)
        rc, out, _ = self.q("search", "brandNew")
        self.assertEqual(rc, 1)

    def test_touch_without_content_change_is_not_a_refresh(self):
        full = os.path.join(self.root, "src/parser.cpp")
        t = time.time() + 20
        os.utime(full, (t, t))
        rc, _, err = self.q("sym", "processAttributes")
        self.assertEqual(err, "")
        rc, _, err = self.q("sym", "processAttributes")
        self.assertEqual(err, "")

    def test_no_refresh_reports_stale_and_does_not_update(self):
        self.bump("src/parser.cpp", "+\nint staleOne()\n{\n}\n")
        self.write("src/new.cpp", b"int staleTwo()\n{\n}\n")
        rc, out, err = self.q("sym", "staleOne", "--no-refresh")
        self.assertEqual(rc, 1)
        self.assertIn("[index may be stale: 2 files changed since ", err)
        rc, out, err = self.q("sym", "staleOne")
        self.assertEqual(rc, 0)
        self.assertIn("[refreshed 2 files]", err)

    def test_no_refresh_without_an_index_fails(self):
        empty = tempfile.mkdtemp(prefix="rfhindex-empty-")
        try:
            rc, _, err = run_q(empty, "sym", "x", "--no-refresh")
            self.assertEqual(rc, 2)
            self.assertIn("q rebuild", err)
        finally:
            shutil.rmtree(empty, ignore_errors=True)

    def test_missing_index_is_built_on_first_use(self):
        os.remove(os.path.join(self.root, rfhindex.DB_RELPATH))
        rc, out, err = self.q("sym", "processAttributes")
        self.assertEqual(rc, 0)
        self.assertIn("[built index:", err)

    def test_old_schema_is_rebuilt(self):
        db = os.path.join(self.root, rfhindex.DB_RELPATH)
        import sqlite3
        c = sqlite3.connect(db)
        c.execute("PRAGMA user_version=0")
        c.commit()
        c.close()
        rc, out, err = self.q("sym", "processAttributes")
        self.assertEqual(rc, 0)
        self.assertIn("[built index:", err)


class TestChunking(unittest.TestCase):
    def test_gaps_become_windows_of_about_sixty_lines(self):
        lines = ["x = %d;" % i for i in range(1, 181)]
        chunks = rfhindex.build_chunks(lines, [])
        self.assertTrue(all(k == "window" for _, _, _, k in chunks))
        self.assertEqual(chunks[0][0], 1)
        self.assertEqual(chunks[-1][1], 180)
        self.assertTrue(all(b - a + 1 <= 80 for a, b, _, _ in chunks))
        self.assertGreaterEqual(len(chunks), 3)

    def test_long_symbols_split_at_blank_lines_and_keep_the_name(self):
        body = []
        for i in range(10):
            body += ["stmt%d;" % j for j in range(35)] + [""]
        lines = ["void big()", "{"] + body + ["}"]
        syms = [{"name": "big", "kind": "function", "line": 1, "end": len(lines), "scope": "",
                 "signature": "()", "language": "C++"}]
        chunks = rfhindex.build_chunks(lines, syms)
        self.assertGreater(len(chunks), 1)
        self.assertTrue(all(q == "big" and k == "function" for _, _, q, k in chunks))
        self.assertTrue(all(b - a + 1 <= 200 for a, b, _, _ in chunks))
        self.assertEqual(sum(b - a + 1 for a, b, _, _ in chunks), len(lines))

    def test_inner_symbol_owns_its_lines(self):
        lines = ["class A", "{", "\tint f() { return 1; }", "};"]
        syms = [{"name": "A", "kind": "class", "line": 1, "end": 4, "scope": "", "signature": "",
                 "language": "C++"},
                {"name": "f", "kind": "function", "line": 3, "end": 3, "scope": "A", "signature": "",
                 "language": "C++"}]
        chunks = rfhindex.build_chunks(lines, syms)
        self.assertEqual([(a, b, q) for a, b, q, _ in chunks],
                         [(1, 2, "A"), (3, 3, "A::f"), (4, 4, "A")])

    def test_identifier_splitting(self):
        self.assertEqual(rfhindex.split_ident("getParsedData"), ["get", "parsed", "data"])
        self.assertEqual(rfhindex.split_ident("xml_attr_str"), ["xml", "attr", "str"])
        self.assertEqual(rfhindex.split_ident("MQMD"), ["mqmd"])
        self.assertEqual(rfhindex.split_ident("parseXMLData"), ["parse", "xml", "data"])


if __name__ == "__main__":
    unittest.main()
