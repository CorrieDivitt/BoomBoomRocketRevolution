<#
  Builds the release zip for GitHub (and from there Quiver Launcher and RecompLauncher).
  Usage:  powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -Version 1.0.0 [-Config relwithdebinfo|release] [-SkipBuild]

  The zip holds the launcher, the game and what they need to run. It never holds game
  data: no Game Files, no DLC, no generated code, no settings or saves. Players bring
  their own copy and the launcher sets it up next to itself.

  Output (in out\release):
    BoomBoomRocketRevolution-v<Version>-windows-x64.zip   the release, files at the top level
    BoomBoomRocketRevolution-v<Version>-windows-x64.zip.sha256
    BoomBoomRocketRevolution-v<Version>-symbols.zip       .pdb files, for crash reports (optional upload)
#>
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [ValidateSet("release", "relwithdebinfo")][string]$Config = "relwithdebinfo",
  [switch]$SkipBuild
)
$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root
if ($Version -notmatch '^\d+\.\d+\.\d+([-.][0-9A-Za-z.]+)?$') { throw "Version should look like 1.0.0 or 1.0.0-rc1" }

$Preset = "win-amd64-$Config"
$Build = Join-Path $Root "out\build\$Preset"
if (-not $SkipBuild) {
  # Bake the version into the launcher, then build everything.
  Remove-Item (Join-Path $Build "CMakeCache.txt") -ErrorAction SilentlyContinue
  $env:BBRR_PACKAGE_VERSION = $Version
  & (Join-Path $PSScriptRoot "build.ps1") -Config $Config -Version $Version
  if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

$Name = "BoomBoomRocketRevolution-v$Version-windows-x64"
$Out = Join-Path $Root "out\release"
$Stage = Join-Path $Out $Name
Remove-Item $Stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

# Programs and the SDK's DLLs.
foreach ($exe in @("BoomBoomRocketRevolution.exe", "boomboomrocket.exe")) {
  $path = Join-Path $Build $exe
  if (-not (Test-Path $path)) { throw "$exe is missing from $Build. Build first (or leave out -SkipBuild)." }
  Copy-Item $path $Stage
}
Get-ChildItem $Build -Filter *.dll | Copy-Item -Destination $Stage

# The port's menu scripts (its own Lua, read from a ui_scripts folder next to the game).
Copy-Item (Join-Path $Root "ui_scripts") (Join-Path $Stage "ui_scripts") -Recurse

# Documents.
Copy-Item (Join-Path $Root "LICENSE") (Join-Path $Stage "LICENSE.txt")
Copy-Item (Join-Path $Root "THIRD_PARTY_NOTICES.md") $Stage
Copy-Item (Join-Path $Root "docs\USER_GUIDE.md") $Stage
@"
Boom Boom Rocket Revolution $Version
====================================

A fan-made Windows port of Boom Boom Rocket (Xbox Live Arcade). It does not include the
game: you need your own copy.

1. Run BoomBoomRocketRevolution.exe (the launcher).
2. Setup tab: choose default.xex in your unpacked game folder, or the game's package file
   as it came from your Xbox 360. Add your DLC packages if you have any.
3. Press Install. The game files go into this folder (Game Files and DLC).
4. Press Play.

Video settings, Menu Layout and Skip Intro can be set in the launcher's Game Settings tab
or in the game under Help & Options > BBRR Settings. Everything else is in USER_GUIDE.md.

Saves, settings and profiles are kept in Documents\boomboomrocket.

Project page: https://github.com/CorrieDivitt/BoomBoomRocketRevolution
"@ | Set-Content -Encoding UTF8 (Join-Path $Stage "README.txt")

# Nothing from the game may be in the zip.
$bad = Get-ChildItem $Stage -Recurse -File | Where-Object {
  $_.Extension -in @(".xex", ".xma", ".lvl", ".spl", ".mdl", ".xpr", ".bbr", ".stfs") -or $_.FullName -match "\\(Game Files|DLC|generated)\\"
}
if ($bad) { throw "Game data found in the package: $($bad.FullName -join ', ')" }

# Zip (files at the top level) and checksum.
$Zip = Join-Path $Out "$Name.zip"
Remove-Item $Zip -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $Zip -CompressionLevel Optimal
$hash = (Get-FileHash $Zip -Algorithm SHA256).Hash.ToLower()
"$hash  $Name.zip" | Set-Content -Encoding ASCII "$Zip.sha256"

# Symbols, kept apart.
$pdbs = Get-ChildItem $Build -Filter *.pdb
if ($pdbs) {
  $Symbols = Join-Path $Out "BoomBoomRocketRevolution-v$Version-symbols.zip"
  Remove-Item $Symbols -ErrorAction SilentlyContinue
  Compress-Archive -Path $pdbs.FullName -DestinationPath $Symbols -CompressionLevel Optimal
}

$size = "{0:N1} MB" -f ((Get-Item $Zip).Length / 1MB)
Write-Host "`nPackaged $Name.zip ($size) in out\release" -ForegroundColor Green
Write-Host "SHA-256: $hash"
