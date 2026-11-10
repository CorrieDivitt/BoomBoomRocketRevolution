<#
  Configure + build the Boom Boom Rocket recomp (codegen runs automatically as part of the build).
  Usage:  powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-Config debug|release|relwithdebinfo] [-Target <name>]
  Examples:
    scripts\build.ps1                                  # full debug build
    scripts\build.ps1 -Target boom_boom_rocket_codegen # codegen only
    scripts\build.ps1 -Config release
#>
param([ValidateSet("debug", "release", "relwithdebinfo")][string]$Config = "debug", [string]$Target = "")
$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root
function Run([string]$what, [scriptblock]$cmd) { & $cmd; if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE). See the output above." } }

# Developer environment: MSVC libs / Windows SDK from Visual Studio, Clang 20+ from standalone LLVM.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with the C++ workload not found. Run scripts\bootstrap.ps1 first." }
Import-Module (Join-Path $vs "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
$llvm = Join-Path $env:ProgramFiles "LLVM\bin"
if (Test-Path $llvm) { $env:PATH = "$llvm;$env:PATH" }

if (-not (Test-Path (Join-Path $Root "CMakePresets.json"))) { throw "Project not initialised yet. Run scripts\bootstrap.ps1 first." }
# Use the SDK built by bootstrap.ps1, not any other ReXGlue install registered on this PC.
$Prefix = Join-Path $Root "thirdparty\rexglue-sdk\out\install\win-amd64"
$Preset = "win-amd64-$Config"
Run "Configuring ($Preset)" { cmake --preset $Preset "-DCMAKE_PREFIX_PATH=$Prefix" }
if ($Target) {
  Run "Building target $Target" { cmake --build --preset $Preset --target $Target }
} else {
  Run "Building ($Preset)" { cmake --build --preset $Preset }
}
Write-Host "`nBuild finished. Output is under out\build\$Preset" -ForegroundColor Green
