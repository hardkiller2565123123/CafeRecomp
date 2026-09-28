param(
    [switch]$RunTests
)

$ErrorActionPreference = 'Stop'

$baseDir = Join-Path $PSScriptRoot 'game\base'
$updateDir = Join-Path $PSScriptRoot 'game\update'
$dlcDir = Join-Path $PSScriptRoot 'game\dlc'
$rpx = Join-Path $updateDir 'code\U-King.rpx'
$generatedDir = Join-Path $PSScriptRoot 'recomp\generated'
$expectedSha256 = 'BA58DA5B95CE929E005D058CEB08B9B2788D1AB2BBC8A6C189BBADCA0BB34D30'

foreach ($required in @(
    (Join-Path $baseDir 'content'),
    (Join-Path $updateDir 'content'),
    (Join-Path $dlcDir 'content\0010'),
    $rpx,
    (Join-Path $generatedDir 'generated.h'),
    (Join-Path $generatedDir 'generated.c')
)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required BOTW input is missing: $required"
    }
}

$actualSha256 = (Get-FileHash -LiteralPath $rpx -Algorithm SHA256).Hash
if ($actualSha256 -ne $expectedSha256) {
    throw "U-King.rpx hash mismatch: $actualSha256"
}

[xml]$updateApp = [System.IO.File]::ReadAllText((Join-Path $updateDir 'code\app.xml'))
[xml]$dlcApp = [System.IO.File]::ReadAllText((Join-Path $dlcDir 'code\app.xml'))
if ($updateApp.app.title_version.'#text' -ne '00D0') {
    throw "Expected BOTW update title version 00D0 (v208)."
}
if ($dlcApp.app.title_version.'#text' -ne '0050') {
    throw "Expected BOTW DLC title version 0050 (v80)."
}

$chunks = Get-ChildItem -LiteralPath (Join-Path $generatedDir 'chunks') -File -Filter '*.c'
if ($chunks.Count -lt 2200) {
    throw "Generated output is incomplete: only $($chunks.Count) chunks."
}
$generatedFiles = Get-ChildItem -LiteralPath $generatedDir -File -Recurse
$generatedBytes = ($generatedFiles | Measure-Object Length -Sum).Sum

Write-Host 'BOTW inputs verified:'
Write-Host "  update: v208 / SHA-256 $actualSha256"
Write-Host "  DLC:    v80 with content pack 0010"
Write-Host "  recomp: $($chunks.Count) chunks / $generatedBytes bytes"

if ($RunTests) {
    $ctest = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
    if (-not (Test-Path -LiteralPath $ctest)) {
        $ctest = (Get-Command ctest -ErrorAction Stop).Source
    }
    & $ctest --test-dir (Join-Path $PSScriptRoot 'build\win-amd64-release') -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
