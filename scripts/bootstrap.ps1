<#
  Boom Boom Rocket Recompiled - one-time setup (Windows, PowerShell 7 or 5.1)
  Run from the repo root:   powershell -ExecutionPolicy Bypass -File scripts\bootstrap.ps1
  Steps: check tools -> submodules -> link assets -> build/install ReXGlue SDK -> scaffold app
#>
param([switch]$SkipSdkBuild, [string]$GameDir = "Game Files")
$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root
function Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Need($cmd, $hint) { if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) { throw "$cmd not found. $hint" } }

Step "Checking tools"
Need git    "Install Git for Windows."
Need python "Install Python 3 (used by tools/ and docs/requirements.py)."
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "Visual Studio 2022 not found. Install it with the 'Desktop development with C++' workload." }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio found but the C++ build tools are missing. Add the 'Desktop development with C++' workload in the VS Installer." }
Import-Module (Join-Path $vs "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
# ReXGlue needs Clang 20+. VS 2022 bundles an older Clang, so put standalone LLVM first on PATH.
$llvm = Join-Path $env:ProgramFiles "LLVM\bin"
if (Test-Path $llvm) { $env:PATH = "$llvm;$env:PATH" }
Need cmake "CMake 3.25+ (VS ships one, or install from cmake.org)."
Need ninja "Ninja (VS ships one, or 'winget install Ninja-build.Ninja')."
Need clang "Install LLVM 20+ (winget install --id LLVM.LLVM -e)."
$clangMajor = [int](((clang --version | Select-Object -First 1) -replace '.*version (\d+).*', '$1'))
if ($clangMajor -lt 20) { throw "clang $clangMajor found first on PATH; ReXGlue needs 20+. Install LLVM (winget install --id LLVM.LLVM -e)." }
$cv = [version]((cmake --version | Select-Object -First 1) -replace '[^0-9.]', '')
if ($cv -lt [version]"3.25") { throw "CMake $cv is too old; need 3.25+." }
Write-Host "  VS: $vs"; Write-Host "  CMake $cv"; clang --version | Select-Object -First 1

Step "Fetching submodules (ReXGlue SDK and its dependencies)"
git submodule update --init --recursive --depth 1

Step "Linking assets\ -> '$GameDir'"
if (-not (Test-Path (Join-Path $GameDir "default.xex"))) { throw "'$GameDir\default.xex' not found. Copy your own dumped game files there first." }
if (-not (Test-Path "assets")) { cmd /c mklink /J assets "$GameDir" | Out-Null }
Write-Host "  assets\default.xex OK"

$Sdk = Join-Path $Root "thirdparty\rexglue-sdk"
$Prefix = Join-Path $Sdk "out\install\win-amd64"
if (-not $SkipSdkBuild) {
  Step "Building and installing ReXGlue SDK (this takes a while)"
  Push-Location $Sdk
  cmake --preset win-amd64
  cmake --build --preset win-amd64-release --target install
  Pop-Location
}
$Rex = Get-ChildItem -Path $Prefix -Recurse -Filter rexglue.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $Rex) { throw "rexglue.exe not found under $Prefix - check the SDK build output." }
$env:REXSDK = $Prefix
Write-Host "  rexglue: $($Rex.FullName)"

Step "Scaffolding the recomp app (only files that don't exist yet)"
$Tmp = Join-Path $Root "build\_init"
if (Test-Path $Tmp) { Remove-Item -Recurse -Force $Tmp }
& $Rex.FullName init --app_name "boom_boom_rocket" --app_root $Tmp --app_desc "Boom Boom Rocket static recompilation" --app_author "CorrieDivitt"
Get-ChildItem $Tmp -Recurse -File | ForEach-Object {
  $rel = $_.FullName.Substring($Tmp.Length + 1)
  $dst = Join-Path $Root $rel
  if (-not (Test-Path $dst)) { New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null; Copy-Item $_.FullName $dst; Write-Host "  + $rel" }
  else { Write-Host "  = $rel (kept existing)" }
}
Step "Done"
Write-Host "Next:  cmake --preset win-amd64  then  cmake --build --preset win-amd64-debug --target boom_boom_rocket_codegen"
Write-Host "Then:  cmake --build --preset win-amd64-debug   (see docs/PLAN.md section 6)"
