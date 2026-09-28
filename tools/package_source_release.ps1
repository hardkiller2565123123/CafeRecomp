param([string]$Version = (Get-Date -Format 'yyyyMMdd-HHmmss'))
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[A-Za-z0-9._-]+$') { throw 'Invalid version label' }
$root = Split-Path $PSScriptRoot -Parent
$release = Join-Path $root "build\releases\CafeRecomp-source-$Version"
$zip = "$release.zip"
if ((Test-Path -LiteralPath $release) -or (Test-Path -LiteralPath $zip)) { throw 'Release already exists' }
New-Item -ItemType Directory -Path $release | Out-Null
function Copy-ReleaseFile([string]$Source, [string]$Relative) {
    $dest = Join-Path $release $Relative
    New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $dest
}
foreach ($name in @('.gitignore','CMakeLists.txt','CMakePresets.json','build.ps1','generate.ps1','verify.ps1','RELEASE-NOTES')) {
    Copy-ReleaseFile (Join-Path $root $name) $name
}
# This source bundle has no nested Git checkout; allow its compiler source to
# be committed while the working checkout keeps third-party repositories ignored.
$ignore = Join-Path $release '.gitignore'
$ignoreText = [IO.File]::ReadAllText($ignore).Replace('/third_party/', "/third_party/*`n!/third_party/DolRecomp/`n/third_party/DolRecomp/build*/")
[IO.File]::WriteAllText($ignore,$ignoreText,[Text.UTF8Encoding]::new($false))
foreach ($name in @('README','CEMU_INTEGRATION','LICENSE-Cemu-MPL-2.0','LICENSE-DolRecomp-GPL-3.0')) {
    Copy-ReleaseFile (Join-Path $root $name) $name
}
foreach ($dir in @('src','tools')) {
    $base = Join-Path $root $dir
    foreach ($file in Get-ChildItem -LiteralPath $base -Recurse -File) {
        if ($file.Extension -in '.c','.h','.ps1','.dat') {
            Copy-ReleaseFile $file.FullName ($file.FullName.Substring($root.Length + 1))
        }
    }
}
# Bundle the locally modified compiler/runtime source, not its binaries or Git history.
$dependency = Join-Path $root 'third_party\DolRecomp'
$tracked = & git -C $dependency ls-files
if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate DolRecomp source' }
foreach ($name in $tracked) {
    $leaf = Split-Path $name -Leaf
    $extension = [IO.Path]::GetExtension($name)
    if ($extension -in '.c','.h','.cpp','.hpp','.cmake','.in','.ps1','.sh','.py' -or
        $leaf -eq 'CMakeLists.txt' -or $leaf -match '^(LICENSE|COPYING)(\..*)?$') {
        $relative = "third_party/DolRecomp/$name"
        if ($leaf -match '^(LICENSE|COPYING).*\.(txt|md)$') { $relative = $relative.Substring(0,$relative.LastIndexOf('.')) }
        Copy-ReleaseFile (Join-Path $dependency $name) $relative
    }
}
$bad = Get-ChildItem -LiteralPath $release -Recurse -File | Where-Object {
    $_.Extension -in '.md','.exe','.dll','.rpx','.rpl','.log','.bin' -or
    ($_.Extension -eq '.txt' -and $_.Name -ne 'CMakeLists.txt')
}
if ($bad) { throw "Forbidden release files: $($bad.FullName -join ', ')" }
Compress-Archive -LiteralPath $release -DestinationPath $zip
Get-FileHash -LiteralPath $zip -Algorithm SHA256
