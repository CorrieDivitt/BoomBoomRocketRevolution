<#
  Apply our patches (patches\rexglue-sdk\*.patch) to the ReXGlue SDK and rebuild/install it.
  Usage:  powershell -ExecutionPolicy Bypass -File scripts\patch-sdk.ps1
  Safe to run again: nothing is changed when the patches are already in.
#>
$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root
function Run([string]$what, [scriptblock]$cmd) { & $cmd; if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE). See the output above." } }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with the C++ workload not found. Run scripts\bootstrap.ps1 first." }
Import-Module (Join-Path $vs "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
$llvm = Join-Path $env:ProgramFiles "LLVM\bin"
if (Test-Path $llvm) { $env:PATH = "$llvm;$env:PATH" }

$Sdk = Join-Path $Root "thirdparty\rexglue-sdk"
$patches = @(Get-ChildItem (Join-Path $Root "patches\rexglue-sdk") -Filter *.patch -ErrorAction SilentlyContinue | Sort-Object Name)
if ($patches.Count -gt 0) {
  # The patches build on each other, so only the last one can be checked on its own:
  # if it is in, they all are. Otherwise put the SDK files back and apply them all in order.
  $ErrorActionPreference = "Continue"
  git -C $Sdk apply --ignore-whitespace --reverse --check $patches[-1].FullName 2>&1 | Out-Null
  $applied = ($LASTEXITCODE -eq 0)
  $ErrorActionPreference = "Stop"
  if ($applied) { Write-Host "SDK patches already applied ($($patches.Count))" }
  else {
    Run "Restoring the SDK sources" { git -C $Sdk checkout -- src include }
    # ...and remove files that patches add (checkout leaves untracked files alone).
    git -C $Sdk clean -fq -- src include
    foreach ($p in $patches) {
      Run "Applying $($p.Name)" { git -C $Sdk apply --ignore-whitespace $p.FullName }
      Write-Host "applied: $($p.Name)"
    }
  }
}
$global:LASTEXITCODE = 0

Push-Location $Sdk
if (-not (Test-Path "out\build\win-amd64")) { Run "Configuring the SDK" { cmake --preset win-amd64 } }
Run "Building the SDK" { cmake --build --preset win-amd64-release --target install }
Pop-Location
Write-Host "`nSDK patched and installed. Now run scripts\build.ps1" -ForegroundColor Green
