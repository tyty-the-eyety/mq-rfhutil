# compare-windows-output.ps1 DIR [GOLDEN_DIR]
#
# The PowerShell twin of compare-windows-output.sh, for the Windows machine where
# RFHUtil writes the files. Compares each file in DIR byte for byte with the golden
# of the same name in GOLDEN_DIR (default: tests\core\golden of this repository) and
# prints
#
#     <name>: same
#     <name>: DIFFERENT (expected: <reason>)    a known difference from Windows
#     <name>: DIFFERENT                         anything else
#     <name>: not a golden file (ignored)
#
# and a summary line, exactly as the shell script does. Exit status: 0 when there is
# no unexpected difference, 1 when there is one, 2 on a usage error or when nothing
# could be compared. Works in Windows PowerShell 5.1. NOT RUN by its author (written
# on Linux, where only the shell version could be tested).
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Dir,
    [Parameter(Position = 1)][string]$GoldenDir
)

Set-StrictMode -Version 2.0

function Fail([string]$Message) {
    [Console]::Error.WriteLine("compare-windows-output.ps1: $Message")
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
    'mqmd-bigendian.bin'  = 'Windows reverses MsgId, CorrelId, GroupId and AccountingToken of a big-endian MQMD; the core keeps them'
    'mqmd-ebcdic-host.bin' = 'Windows rebuilds the MQMD Encoding without the S/390 float format (785 becomes 529), because reading a file does not set the float format; the core does the same'
    'mqmd-mqmde-text.bin' = 'Windows swaps the MQMDE integers whatever the encoding and does not write the MQMDE back; the core swaps only host data and writes the MQMDE back unchanged'
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
    $a = [System.IO.File]::ReadAllBytes((Join-Path $Dir $name))
    $b = [System.IO.File]::ReadAllBytes($goldenPath)
    $equal = ($a.Length -eq $b.Length)
    if ($equal) {
        for ($i = 0; $i -lt $a.Length; $i++) {
            if ($a[$i] -ne $b[$i]) { $equal = $false; break }
        }
    }
    if ($equal) {
        Write-Output "${name}: same"
        $same++
    }
    elseif ($reasons.ContainsKey($name)) {
        Write-Output ("${name}: DIFFERENT (expected: " + $reasons[$name] + ")")
        $expected++
    }
    else {
        Write-Output "${name}: DIFFERENT"
        $unexpected++
    }
}

$missing = 0
foreach ($golden in @(Get-ChildItem -LiteralPath $GoldenDir -File)) {
    if (($golden.Name -ne 'MANIFEST.md') -and -not $seen.ContainsKey($golden.Name)) { $missing++ }
}

Write-Output "summary: $same same, $expected different as expected, $unexpected DIFFERENT unexpectedly, $missing golden files not in the directory"

if ($compared -eq 0) { Fail "no golden file names found in $Dir" }
if ($unexpected -gt 0) { exit 1 }
exit 0
