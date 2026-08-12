#!/usr/bin/env pwsh
#
# scripts/gen_corpus.ps1 — PowerShell mirror of scripts/gen_corpus.sh
# (deterministic fixture synthesis skeleton, BUILD-08, locked decision D-08).
# Same two Phase 1 responsibilities as the shell variant: gate on the
# generating ffmpeg's version, and record that generator's identity into a
# manifest carrying the same key set in the same order, so a fixture
# generated on Windows carries provenance comparable to one generated on
# Linux/macOS. Zero fixtures are generated — see the shell variant's header
# comment for the full determinism rationale (D-08).
#
# Executed on the Windows CI leg wired in a later plan; on a POSIX developer
# machine this script's shape is asserted by inspection rather than by
# running it.

$ErrorActionPreference = 'Stop'

# The version floor, held as named constants rather than inlined into the
# comparison below — mirrors the shell variant exactly.
$MIN_MAJOR = 6
$MIN_MINOR = 1

# Resolve which binary to invoke from MEDIADIFF_FFMPEG (defaulting to the
# bare name "ffmpeg" on PATH), same override convention as the shell variant.
$FfmpegBin = $env:MEDIADIFF_FFMPEG
if ([string]::IsNullOrEmpty($FfmpegBin)) {
    $FfmpegBin = 'ffmpeg'
}

$resolved = Get-Command $FfmpegBin -ErrorAction SilentlyContinue
if (-not $resolved) {
    Write-Error "gen_corpus requires a system ffmpeg >= $MIN_MAJOR.$MIN_MINOR on PATH (or MEDIADIFF_FFMPEG pointing at one); '$FfmpegBin' was not found."
    exit 1
}

$versionOutput = & $FfmpegBin -version
$ffmpegVersionLine = ($versionOutput | Select-Object -First 1)
$ffmpegConfigLine = ($versionOutput | Where-Object { $_ -match '^configuration:' } | Select-Object -First 1)
if (-not $ffmpegConfigLine) {
    $ffmpegConfigLine = ''
}

# "ffmpeg version <TOKEN> Copyright (c) ..." — pull just the version token.
$versionToken = ''
if ($ffmpegVersionLine -match '^ffmpeg version (\S+)') {
    $versionToken = $Matches[1]
}

$versionOk = $false
if ($versionToken -match '^[nN]-\d+-g[0-9a-fA-F]+') {
    # A git-describe "N-<commits-since-tag>-g<hash>" snapshot build (e.g.
    # "N-126086-ge5ecfe8970-20260812") — always newer than the release tag
    # it is offset from. Treat it as satisfying the floor. See the shell
    # variant's matching branch for the full rationale.
    $versionOk = $true
} elseif ($versionToken -match '^[nN]?(\d+)\.(\d+)') {
    $major = [int]$Matches[1]
    $minor = [int]$Matches[2]
    if (($major -gt $MIN_MAJOR) -or (($major -eq $MIN_MAJOR) -and ($minor -ge $MIN_MINOR))) {
        $versionOk = $true
    }
}

if (-not $versionOk) {
    Write-Error "gen_corpus requires a system ffmpeg >= $MIN_MAJOR.$MIN_MINOR; found: $ffmpegVersionLine"
    exit 1
}

$OutDir = 'tests/fixtures'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Manifest = Join-Path $OutDir 'GENERATOR_MANIFEST.json'
$GeneratedAt = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')

# Fixed key order (generator, configuration, generated_at) — identical set
# and order to the shell variant, so provenance is comparable across
# platforms. generated_at is the only field permitted to differ between two
# runs against the same ffmpeg binary.
$manifestObject = [ordered]@{
    generator     = $ffmpegVersionLine
    configuration = $ffmpegConfigLine
    generated_at  = $GeneratedAt
}
$manifestObject | ConvertTo-Json | Set-Content -Path $Manifest -Encoding utf8NoBOM

# --- Fixture recipes (Phase 1: none yet) -----------------------------------
# Every future recipe added in a later phase follows this convention:
#   & $FfmpegBin -flags +bitexact -fflags +bitexact -y `
#     -f lavfi -i <source-filter> ... "$OutDir/<fixture-name>.<ext>"
# Write only into $OutDir, and never commit the result — .gitignore keeps
# generated media out of git while this manifest stays tracked as provenance.
# ----------------------------------------------------------------------------

# Exiting 0 with zero fixtures generated is the success case in this phase,
# not a degenerate one — mirrors the shell variant's own recorded assumption.
Write-Output "gen_corpus: manifest written to $Manifest. No fixtures generated in Phase 1 (skeleton only)."
