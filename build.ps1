param(
    [ValidateSet('Debug','Release')]
    [string]$Configuration = 'Release',
    [switch]$Regenerate
)

$ErrorActionPreference = 'Stop'

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmake = $cmakeCommand.Source
} else {
    $cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}

if (-not (Test-Path -LiteralPath $cmake)) {
    throw 'CMake was not found. Install the Visual Studio C++ and CMake tools.'
}

if ($Regenerate -or -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'recomp\generated\generated.h'))) {
    & (Join-Path $PSScriptRoot 'generate.ps1')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$buildRoot = Join-Path $PSScriptRoot 'build'
@('bin','lib','logs','captures','dumps','traces') | ForEach-Object {
    New-Item -ItemType Directory -Force -Path (Join-Path $buildRoot $_) | Out-Null
}

$preset = if ($Configuration -eq 'Debug') { 'win-amd64-debug' } else { 'win-amd64-release' }
& $cmake --preset $preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build --preset $preset --parallel 8
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Build complete: $buildRoot\bin\botw_recomp.exe"
