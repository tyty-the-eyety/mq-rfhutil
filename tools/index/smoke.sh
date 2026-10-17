#!/bin/sh
# Smoke test: run q against the real repository and check a few long-lived facts.
# The unit tests (python3 -m unittest discover tools/index/tests) do not depend on the repo.
set -u
here=$(cd "$(dirname "$0")" && pwd)
q="$here/q"
fail=0

check() {  # description, expected text, command...
    desc=$1; want=$2; shift 2
    out=$("$@" 2>&1)
    if printf '%s\n' "$out" | grep -qF -- "$want"; then
        echo "ok   $desc"
    else
        echo "FAIL $desc (wanted: $want)"
        printf '%s\n' "$out" | head -5
        fail=1
    fi
}

check "sym getParsedData -> DataFormatter.cpp" "RFHUtil/DataFormatter.cpp" "$q" sym getParsedData
check "sym renderCobol -> rfh_format.cpp"      "core/format/rfh_format.cpp" "$q" sym renderCobol
check "sym processAttributes -> xmlsubs.cpp"   "RFHUtil/xmlsubs.cpp" "$q" sym processAttributes
check "outline lists render"                   "render" "$q" outline core/format/rfh_format.cpp
check "outline of a plan lists headings"       "Task 7" "$q" outline docs/superpowers/plans/2026-10-16-linux-port-plan-2b-data-views.md
check "search camelCase split"                 "getParsedData" "$q" search parsed data --in 'RFHUtil/*' --brief
check "refs finds a caller"                    "RFHUtil/DataArea.cpp" "$q" refs processAttributes
check "tests-for finds Xmlsubs tests"          "tests/UnitTests/XmlsubsTests.cpp" "$q" tests-for processAttributes
check "read returns numbered lines"            "render" "$q" read core/format/rfh_format.cpp 141 141
check "stats shows a git HEAD"                 "git HEAD:" "$q" stats
check "no-refresh on a fresh index is quiet"   "files:" "$q" stats --no-refresh

if [ "$fail" -eq 0 ]; then echo "smoke: all passed"; else echo "smoke: FAILURES"; fi
exit "$fail"
