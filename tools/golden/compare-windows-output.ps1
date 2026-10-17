# compare-windows-output.ps1 -Dir DIR [-GoldenDir GOLDEN_DIR] [-All]
#
# The PowerShell twin of compare-windows-output.sh, for the Windows machine where
# RFHUtil writes the files. Compares each file in DIR byte for byte with the golden
# of the same name in GOLDEN_DIR (default: tests\core\golden of this repository) and
# prints
#
#     <name>: same
#     <name>: DIFFERENT (expected: <reason>); <n> bytes differ, length <l> (golden <g>), first offsets: <a> <b> ...
#     <name>: DIFFERENT; <n> bytes differ, length <l> (golden <g>), first offsets: <a> <b> ...
#     <name>: not a golden file (ignored)
#
# and a summary line, exactly as the shell script does (offsets are 0-based). A known
# difference counts as expected only if its shape is exactly the one MANIFEST.md derives
# from the Windows source (the whole expected file is rebuilt from the golden here).
# -All also fails when a golden file is missing from DIR.
#
# Exit status: 0 when there is no unexpected difference (and, with -All, no missing
# file), 1 when there is one, 2 on a usage error or when nothing could be compared.
# Run it as: powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File <this file> -Dir <folder>
# Works in Windows PowerShell 5.1. NOT RUN by its author (written on Linux, where only
# the shell version could be tested).
param(
    [string]$Dir,
    [string]$GoldenDir,
    [switch]$All
)

Set-StrictMode -Version 2.0

function Fail([string]$Message) {
    [Console]::Error.WriteLine("compare-windows-output.ps1: $Message")
    exit 2
}

if (-not $Dir) {
    [Console]::Error.WriteLine('usage: compare-windows-output.ps1 -Dir DIR [-GoldenDir GOLDEN_DIR] [-All]')
    exit 2
}
if (-not $GoldenDir) {
    $GoldenDir = Join-Path $PSScriptRoot '..\..\tests\core\golden'
}
if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { Fail "$Dir is not a directory" }
if (-not (Test-Path -LiteralPath $GoldenDir -PathType Container)) { Fail "$GoldenDir is not a directory" }

# ReadAllBytes resolves relative paths against the process directory, not PowerShell's
$Dir = (Resolve-Path -LiteralPath $Dir).ProviderPath
$GoldenDir = (Resolve-Path -LiteralPath $GoldenDir).ProviderPath

# The reason a file is expected to differ from what Windows writes.
# Keep these texts identical in compare-windows-output.sh.
$reasons = @{
    'mqmd-bigendian.bin'   = 'Windows writes the MQMD little endian with Encoding 529 and swaps the bytes inside each 4-byte word of MsgId, CorrelId, AccountingToken and GroupId; the core keeps the ids as they are'
    'mqmd-ebcdic-host.bin' = 'Windows rebuilds the MQMD Encoding without the S/390 float format (785 becomes 529), because reading a file does not set the float format; the core does the same'
    'mqmd-mqmde-text.bin'  = 'Windows swaps the MQMDE integers whatever the encoding and does not write the MQMDE back; the core swaps only host data and writes the MQMDE back unchanged'
}

# E gets the bytes of G with each 4-byte word in [start, start + length) reversed
function Swap-Words([byte[]]$E, [byte[]]$G, [int]$start, [int]$length) {
    for ($w = $start; $w -lt $start + $length; $w += 4) {
        $E[$w] = $G[$w + 3]
        $E[$w + 1] = $G[$w + 2]
        $E[$w + 2] = $G[$w + 1]
        $E[$w + 3] = $G[$w]
    }
}

# The file Windows is expected to write for a known-difference file, from the golden
# bytes G (see MANIFEST.md for the derivation); $null for any other file.
function Get-Expected([string]$name, [byte[]]$G) {
    switch ($name) {
        'mqmd-ebcdic-host.bin' {
            $E = [byte[]]$G.Clone()
            $E[25] = 2
            return , $E
        }
        'mqmd-bigendian.bin' {
            $E = [byte[]]$G.Clone()
            foreach ($off in 4, 8, 12, 16, 20, 28, 40, 44, 96, 272, 348, 352, 356, 360) {
                Swap-Words $E $G $off 4
            }
            $E[24] = 17; $E[25] = 2; $E[26] = 0; $E[27] = 0
            Swap-Words $E $G 48 24
            Swap-Words $E $G 72 24
            Swap-Words $E $G 208 32
            Swap-Words $E $G 324 24
            return , $E
        }
        'mqmd-mqmde-text.bin' {
            if ($G.Length -lt 436) { return $null }
            $E = New-Object 'System.Collections.Generic.List[byte]'
            for ($i = 0; $i -lt 364; $i++) { $E.Add($G[$i]) }
            for ($i = 0; $i -lt 24; $i++) { $E[324 + $i] = $G[364 + 32 + $i] }
            $E[348] = 0; $E[349] = 0; $E[350] = 0; $E[351] = 2
            $E[352] = 0; $E[353] = 0; $E[354] = 0; $E[355] = 10
            $E[356] = 0; $E[357] = 0; $E[358] = 0; $E[359] = 0
            $E[360] = 0; $E[361] = 0; $E[362] = 0; $E[363] = 42
            for ($i = 436; $i -lt $G.Length; $i++) { $E.Add($G[$i]) }
            return , $E.ToArray()
        }
        default { return $null }
    }
}

function Test-SameBytes([byte[]]$a, [byte[]]$b) {
    if ($a.Length -ne $b.Length) { return $false }
    for ($i = 0; $i -lt $a.Length; $i++) {
        if ($a[$i] -ne $b[$i]) { return $false }
    }
    return $true
}

$same = 0
$expected = 0
$unexpected = 0
$compared = 0
$seen = @{}

# Ordinal order, like LC_ALL=C in the shell script
$names = [string[]]@(Get-ChildItem -LiteralPath $Dir -File | ForEach-Object { $_.Name })
[System.Array]::Sort($names, [System.StringComparer]::Ordinal)
foreach ($name in $names) {
    $goldenPath = Join-Path $GoldenDir $name
    if (($name -eq 'MANIFEST.md') -or -not (Test-Path -LiteralPath $goldenPath -PathType Leaf)) {
        Write-Output "${name}: not a golden file (ignored)"
        continue
    }
    $seen[$name] = $true
    $compared++
    $o = [System.IO.File]::ReadAllBytes((Join-Path $Dir $name))
    $g = [System.IO.File]::ReadAllBytes($goldenPath)
    if (Test-SameBytes $o $g) {
        Write-Output "${name}: same"
        $same++
        continue
    }

    $common = [Math]::Min($o.Length, $g.Length)
    $count = 0
    $first = @()
    for ($i = 0; $i -lt $common; $i++) {
        if ($o[$i] -ne $g[$i]) {
            $count++
            if ($count -le 5) { $first += $i }
        }
    }
    $noun = if ($count -eq 1) { 'byte differs' } else { 'bytes differ' }
    $offsets = if ($first.Count -gt 0) { $first -join ' ' } else { 'none' }
    $detail = "$count $noun, length $($o.Length) (golden $($g.Length)), first offsets: $offsets"

    $shapeOk = $false
    if ($reasons.ContainsKey($name)) {
        $e = Get-Expected $name $g
        if (($null -ne $e) -and (Test-SameBytes $o $e)) { $shapeOk = $true }
    }
    if ($shapeOk) {
        Write-Output ("${name}: DIFFERENT (expected: " + $reasons[$name] + "); $detail")
        $expected++
    }
    else {
        Write-Output "${name}: DIFFERENT; $detail"
        $unexpected++
    }
}

$missing = 0
foreach ($goldenFile in @(Get-ChildItem -LiteralPath $GoldenDir -File)) {
    if (($goldenFile.Name -ne 'MANIFEST.md') -and -not $seen.ContainsKey($goldenFile.Name)) { $missing++ }
}

Write-Output "summary: $same same, $expected different as expected, $unexpected DIFFERENT unexpectedly, $missing golden files not in the directory"

if ($compared -eq 0) { Fail "no golden file names found in $Dir" }
if ($unexpected -gt 0) { exit 1 }
if ($All -and ($missing -gt 0)) {
    [Console]::Error.WriteLine("compare-windows-output.ps1: -All: $missing golden files are missing from $Dir")
    exit 1
}
exit 0
