#!/usr/bin/env bash
# compare-windows-output.sh DIR [GOLDEN_DIR]
#
# Compares each file in DIR (the files RFHUtil on Windows wrote, named as the
# goldens) byte for byte with the golden of the same name in GOLDEN_DIR (default:
# tests/core/golden next to this script's repository root). For each file it prints
#
#     <name>: same
#     <name>: DIFFERENT (expected: <reason>)    a known difference from Windows
#     <name>: DIFFERENT                         anything else
#     <name>: not a golden file (ignored)
#
# and a summary line. Exit status: 0 when no unexpected difference, 1 when there is
# one, 2 on a usage error or when nothing could be compared. The PowerShell twin,
# compare-windows-output.ps1, prints the same text and exits the same way.
set -u
LC_ALL=C
export LC_ALL

usage()
{
    echo "usage: compare-windows-output.sh DIR [GOLDEN_DIR]" >&2
    exit 2
}

[ $# -ge 1 ] && [ $# -le 2 ] || usage
dir=$1
here=$(cd "$(dirname "$0")" && pwd) || exit 2
golden=${2:-$here/../../tests/core/golden}

[ -d "$dir" ] || { echo "compare-windows-output.sh: $dir is not a directory" >&2; exit 2; }
[ -d "$golden" ] || { echo "compare-windows-output.sh: $golden is not a directory" >&2; exit 2; }

# The reason a file is expected to differ from what Windows writes; empty for none.
# Keep these texts identical in compare-windows-output.ps1.
reason_for()
{
    case $1 in
    mqmd-bigendian.bin)
        echo "Windows reverses MsgId, CorrelId, GroupId and AccountingToken of a big-endian MQMD; the core keeps them" ;;
    mqmd-ebcdic-host.bin)
        echo "Windows rebuilds the MQMD Encoding without the S/390 float format (785 becomes 529), because reading a file does not set the float format; the core does the same" ;;
    mqmd-mqmde-text.bin)
        echo "Windows swaps the MQMDE integers whatever the encoding and does not write the MQMDE back; the core swaps only host data and writes the MQMDE back unchanged" ;;
    esac
}

same=0
expected=0
unexpected=0
compared=0
seen=" "

for path in "$dir"/*; do
    [ -f "$path" ] || continue
    name=${path##*/}
    if [ "$name" = "MANIFEST.md" ] || [ ! -f "$golden/$name" ]; then
        echo "$name: not a golden file (ignored)"
        continue
    fi
    seen="$seen$name "
    compared=$((compared + 1))
    if cmp -s -- "$path" "$golden/$name"; then
        echo "$name: same"
        same=$((same + 1))
    else
        reason=$(reason_for "$name")
        if [ -n "$reason" ]; then
            echo "$name: DIFFERENT (expected: $reason)"
            expected=$((expected + 1))
        else
            echo "$name: DIFFERENT"
            unexpected=$((unexpected + 1))
        fi
    fi
done

missing=0
for path in "$golden"/*; do
    [ -f "$path" ] || continue
    name=${path##*/}
    [ "$name" = "MANIFEST.md" ] && continue
    case $seen in
    *" $name "*) ;;
    *) missing=$((missing + 1)) ;;
    esac
done

echo "summary: $same same, $expected different as expected, $unexpected DIFFERENT unexpectedly, $missing golden files not in the directory"

[ "$compared" -gt 0 ] || { echo "compare-windows-output.sh: no golden file names found in $dir" >&2; exit 2; }
[ "$unexpected" -eq 0 ] || exit 1
exit 0
