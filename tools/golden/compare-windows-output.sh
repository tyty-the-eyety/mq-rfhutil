#!/usr/bin/env bash
# compare-windows-output.sh [--all] DIR [GOLDEN_DIR]
#
# Compares each file in DIR (the files RFHUtil on Windows wrote, named as the
# goldens) byte for byte with the golden of the same name in GOLDEN_DIR (default:
# tests/core/golden of this repository). For each file it prints
#
#     <name>: same
#     <name>: DIFFERENT (expected: <reason>); <n> bytes differ, length <l> (golden <g>), first offsets: <a> <b> ...
#     <name>: DIFFERENT; <n> bytes differ, length <l> (golden <g>), first offsets: <a> <b> ...
#     <name>: not a golden file (ignored)
#
# (offsets are 0-based; the count and offsets cover the bytes both files have) and a
# summary line. A known difference from Windows counts as expected only if its shape
# is exactly the one MANIFEST.md derives from the Windows source (the whole expected
# file is rebuilt from the golden here); any other difference in such a file is an
# unexpected DIFFERENT. --all also fails when a golden file is missing from DIR.
#
# Exit status: 0 when there is no unexpected difference (and, with --all, no missing
# file), 1 when there is one, 2 on a usage error or when nothing could be compared.
# The PowerShell twin, compare-windows-output.ps1, prints the same text and exits the
# same way.
set -u
LC_ALL=C
export LC_ALL

usage()
{
    echo "usage: compare-windows-output.sh [--all] DIR [GOLDEN_DIR]" >&2
    exit 2
}

all=0
if [ $# -ge 1 ] && [ "$1" = "--all" ]; then
    all=1
    shift
fi
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
        echo "Windows writes the MQMD little endian with Encoding 529 and swaps the bytes inside each 4-byte word of MsgId, CorrelId, AccountingToken and GroupId; the core keeps the ids as they are" ;;
    mqmd-ebcdic-host.bin)
        echo "Windows rebuilds the MQMD Encoding without the S/390 float format (785 becomes 529), because reading a file does not set the float format; the core does the same" ;;
    mqmd-mqmde-text.bin)
        echo "Windows swaps the MQMDE integers whatever the encoding and does not write the MQMDE back; the core swaps only host data and writes the MQMDE back unchanged" ;;
    esac
}

# load_golden / load_output FILE: the bytes of FILE as decimal numbers in the global
# array G (the golden) or O (the output)
load_golden()
{
    mapfile -t G < <(od -An -v -tu1 -w1 -- "$1" | tr -d ' ')
}
load_output()
{
    mapfile -t O < <(od -An -v -tu1 -w1 -- "$1" | tr -d ' ')
}

# swap_words ARRAY_OFFSET LENGTH: E gets the bytes of G with each 4-byte word reversed
swap_words()
{
    local start=$1 length=$2 w
    for ((w = start; w < start + length; w += 4)); do
        E[w]=${G[w + 3]}
        E[w + 1]=${G[w + 2]}
        E[w + 2]=${G[w + 1]}
        E[w + 3]=${G[w]}
    done
}

# build_expected NAME: E becomes the file Windows is expected to write (from the golden G);
# returns 1 for a file with no known difference. See MANIFEST.md for the derivation.
build_expected()
{
    local i off
    E=("${G[@]}")
    case $1 in
    mqmd-ebcdic-host.bin)
        E[25]=2 ;;
    mqmd-bigendian.bin)
        # the MQMD written little endian: each number is the golden's, bytes reversed
        for off in 4 8 12 16 20 28 40 44 96 272 348 352 356 360; do
            swap_words "$off" 4
        done
        # Encoding 273 comes back as 529 (0x211), little endian 11 02 00 00
        E[24]=17; E[25]=2; E[26]=0; E[27]=0
        # MsgId, CorrelId, AccountingToken and GroupId: the bytes inside each 4-byte word swapped
        swap_words 48 24
        swap_words 72 24
        swap_words 208 32
        swap_words 324 24 ;;
    mqmd-mqmde-text.bin)
        # no MQMDE (bytes 364 to 435); the MQMD takes the group id as it is and the sequence number,
        # offset and original length of the MQMDE with their bytes swapped, and no flags
        E=("${G[@]:0:364}")
        for ((i = 0; i < 24; i++)); do E[324 + i]=${G[364 + 32 + i]}; done
        E[348]=0; E[349]=0; E[350]=0; E[351]=2
        E[352]=0; E[353]=0; E[354]=0; E[355]=10
        E[356]=0; E[357]=0; E[358]=0; E[359]=0
        E[360]=0; E[361]=0; E[362]=0; E[363]=42
        E+=("${G[@]:436}") ;;
    *)
        return 1 ;;
    esac
    return 0
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
        continue
    fi

    load_golden "$golden/$name"
    load_output "$path"
    common=${#G[@]}
    [ "${#O[@]}" -lt "$common" ] && common=${#O[@]}
    count=0
    first=""
    for ((i = 0; i < common; i++)); do
        if [ "${O[i]}" != "${G[i]}" ]; then
            count=$((count + 1))
            [ "$count" -le 5 ] && first="$first $i"
        fi
    done
    if [ "$count" -eq 1 ]; then noun="byte differs"; else noun="bytes differ"; fi
    detail="$count $noun, length ${#O[@]} (golden ${#G[@]}), first offsets:${first:- none}"

    reason=$(reason_for "$name")
    shape_ok=0
    if [ -n "$reason" ] && build_expected "$name" && [ "${E[*]}" = "${O[*]}" ]; then
        shape_ok=1
    fi
    if [ "$shape_ok" -eq 1 ]; then
        echo "$name: DIFFERENT (expected: $reason); $detail"
        expected=$((expected + 1))
    else
        echo "$name: DIFFERENT; $detail"
        unexpected=$((unexpected + 1))
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
if [ "$all" -eq 1 ] && [ "$missing" -gt 0 ]; then
    echo "compare-windows-output.sh: --all: $missing golden files are missing from $dir" >&2
    exit 1
fi
exit 0
